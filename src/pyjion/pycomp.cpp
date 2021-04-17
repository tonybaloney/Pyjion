/*
* The MIT License (MIT)
*
* Copyright (c) Microsoft Corporation
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included
* in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
* OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
* OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
* OTHER DEALINGS IN THE SOFTWARE.
*
*/


#include <windows.h>
#include <corjit.h>

#ifdef WINDOWS
#include <libloaderapi.h>
typedef void(__cdecl* JITSTARTUP)(ICorJitHost*);
#endif

#include <Python.h>
#include "pycomp.h"
#include "pyjit.h"
#include "pyjitmath.h"

using namespace std;


CCorJitHost g_jitHost;

void CeeInit() {
#ifdef WINDOWS
    auto clrJitHandle = LoadLibrary(TEXT("clrjit.dll"));
    if (clrJitHandle == nullptr) {
        printf("Failed to load clrjit.dll");
        exit(40);
    }
    auto jitStartup = (JITSTARTUP)GetProcAddress(GetModuleHandle(TEXT("clrjit.dll")), "jitStartup");
    if (jitStartup != nullptr)
        jitStartup(&g_jitHost);
    else {
        printf("Failed to load jitStartup() from clrjit.dll");
        exit(41);
    }
#else
	jitStartup(&g_jitHost);
#endif
}

class InitHolder {
public:
    InitHolder() {
        CeeInit();
    }
};

InitHolder g_initHolder;

BaseModule g_module;
ICorJitCompiler* g_jit;

PythonCompiler::PythonCompiler(PyCodeObject *code) :
    m_il(m_module = new UserModule(g_module),
        CORINFO_TYPE_NATIVEINT,
        std::vector < Parameter > {
        Parameter(CORINFO_TYPE_NATIVEINT), // PyjionJittedCode*
        Parameter(CORINFO_TYPE_NATIVEINT), // struct _frame*
        Parameter(CORINFO_TYPE_NATIVEINT), // PyThreadState*
        Parameter(CORINFO_TYPE_NATIVEINT),}) // PyjionCodeProfile*
{
    this->m_code = code;
    m_lasti = m_il.define_local(Parameter(CORINFO_TYPE_NATIVEINT));
}

void PythonCompiler::load_frame() {
    m_il.ld_arg(1);
}

void PythonCompiler::load_tstate() {
    m_il.ld_arg(2);
}

void PythonCompiler::emit_load_frame_locals(){
    for (int i = 0 ; i < this->m_code->co_nlocals; i++){
        m_frameLocals[i] = m_il.define_local_no_cache(Parameter(CORINFO_TYPE_NATIVEINT));
        load_frame();
        m_il.ld_i(offsetof(PyFrameObject, f_localsplus) + i * sizeof(size_t));
        m_il.add();
        m_il.ld_ind_i();
        m_il.st_loc(m_frameLocals[i]);
    }
}

void PythonCompiler::emit_push_frame() {
    if (OPT_ENABLED(inlineFramePushPop)) {
        load_tstate();
        LD_FIELDA(PyThreadState, frame);
        load_frame();
        m_il.st_ind_i();
    } else {
        load_frame();
        m_il.emit_call(METHOD_PY_PUSHFRAME);
    }
}

void PythonCompiler::emit_pop_frame() {
    if (OPT_ENABLED(inlineFramePushPop)) {
        load_tstate();
        LD_FIELDA(PyThreadState, frame);

        load_frame();
        LD_FIELD(PyFrameObject, f_back);

        m_il.st_ind_i();
    } else {
        load_frame();
        m_il.emit_call(METHOD_PY_POPFRAME);
    }
}

void PythonCompiler::emit_eh_trace() {
    load_frame();
    m_il.emit_call(METHOD_EH_TRACE);
}

void PythonCompiler::emit_lasti_init() {
    load_frame();
    m_il.ld_i(offsetof(PyFrameObject, f_lasti));
    m_il.add();
    m_il.st_loc(m_lasti);
}

void PythonCompiler::emit_lasti_update(int index) {
    m_il.ld_loc(m_lasti);
    m_il.ld_i4(index);
    m_il.st_ind_i4();
}

void PythonCompiler::load_local(int oparg) {
    if (OPT_ENABLED(nativeLocals)) {
        m_il.ld_loc(m_frameLocals[oparg]);
    } else {
        load_frame();
        m_il.ld_i(offsetof(PyFrameObject, f_localsplus) + oparg * sizeof(size_t));
        m_il.add();
        m_il.ld_ind_i();
    }
}

void PythonCompiler::emit_breakpoint(){
    // Emits a breakpoint in the IL. useful for debugging
    m_il.brk();
}

void PythonCompiler::emit_trace_line(Local lowerBound, Local upperBound, Local lastInstr) {
    load_frame();
    emit_load_local_addr(lowerBound);
    emit_load_local_addr(upperBound);
    emit_load_local_addr(lastInstr);
    m_il.emit_call(METHOD_TRACE_LINE);
}

void PythonCompiler::emit_trace_frame_entry() {
    load_frame();
    m_il.emit_call(METHOD_TRACE_FRAME_ENTRY);
}

void PythonCompiler::emit_trace_frame_exit() {
    load_frame();
    m_il.emit_call(METHOD_TRACE_FRAME_EXIT);
}

void PythonCompiler::emit_profile_frame_entry() {
    load_frame();
    m_il.emit_call(METHOD_PROFILE_FRAME_ENTRY);
}

void PythonCompiler::emit_profile_frame_exit() {
    load_frame();
    m_il.emit_call(METHOD_PROFILE_FRAME_EXIT);
}

void PythonCompiler::emit_trace_exception() {
    load_frame();
    m_il.emit_call(METHOD_TRACE_EXCEPTION);
}

void PythonCompiler::emit_incref() {
    LD_FIELDA(PyObject, ob_refcnt);
    m_il.dup();
    m_il.ld_ind_i();
    m_il.load_one();
    m_il.add();
    m_il.st_ind_i();
}

void PythonCompiler::emit_list_shrink(size_t by) {
    LD_FIELDA(PyVarObject, ob_size);
    m_il.dup();
    m_il.ld_ind_i();
    emit_int(by);
    m_il.sub();
    m_il.st_ind_i();
}

void PythonCompiler::decref(bool noopt) {
    /*
     * PyObject* is on the top of the stack
     * Should decrement obj->ob_refcnt
     * by either doing it inline, or calling PyJit_Decref
     */
    if (OPT_ENABLED(inlineDecref) && !noopt){ // obj
        Label done = emit_define_label();
        Label popAndGo = emit_define_label();
        m_il.dup();                     // obj, obj
        emit_branch(BranchFalse, popAndGo);

        m_il.dup(); m_il.dup();         // obj, obj, obj
        LD_FIELDA(PyObject, ob_refcnt); // obj, obj, refcnt
        m_il.dup();                     // obj, obj, refcnt, refcnt
        m_il.ld_ind_i();               // obj, obj, refcnt, *refcnt
        m_il.load_one();                 // obj, obj, refcnt,  *refcnt, 1
        m_il.sub();                    // obj, obj, refcnt, (*refcnt - 1)
        m_il.st_ind_i();              // obj, obj
        LD_FIELD(PyObject, ob_refcnt); // obj, refcnt
        m_il.load_null();                 // obj, refcnt, 0
        emit_branch(BranchGreaterThan, popAndGo);

        m_il.emit_call(METHOD_DEALLOC_OBJECT); // _Py_Dealloc
        emit_branch(BranchAlways, done);

        emit_mark_label(popAndGo);
        emit_pop();

        emit_mark_label(done);
    } else {
        m_il.emit_call(METHOD_DECREF_TOKEN);
    }
}

void PythonCompiler::emit_unpack_tuple(size_t size, AbstractValueWithSources iterable) {
    Label passedGuard, failedGuard;
    if (iterable.Value->needsGuard()){
        passedGuard = emit_define_label(), failedGuard = emit_define_label();
        m_il.dup();
        LD_FIELD(PyObject, ob_type);
        emit_ptr(iterable.Value->pythonType());
        emit_branch(BranchEqual, passedGuard);
        emit_unpack_generic(size, iterable);
        emit_branch(BranchAlways, failedGuard);
        emit_mark_label(passedGuard);
    }

    Local t_value = emit_define_local(LK_NativeInt);
    Label raiseValueError = emit_define_label();
    Label returnValues = emit_define_label();
    size_t idx = size, idx2 = size;
    emit_store_local(t_value);

    emit_load_local(t_value);
    emit_tuple_length();
    emit_int(size);
    emit_branch(BranchNotEqual,raiseValueError);

        while (idx--) {
            emit_load_local(t_value);
            emit_tuple_load(idx);
            emit_dup();
            emit_incref();
        }
        emit_int(0);

        emit_branch(BranchAlways, returnValues);

    emit_mark_label(raiseValueError);

        while (idx2--){
            emit_null();
        }
        emit_pyerr_setstring(PyExc_ValueError, "Cannot unpack tuple due to size mismatch");
        emit_int(-1);

    emit_mark_label(returnValues);
    emit_load_and_free_local(t_value);
    decref();

    if (iterable.Value->needsGuard()){
        emit_mark_label(failedGuard);
    }
}

void PythonCompiler::emit_unpack_list(size_t size, AbstractValueWithSources iterable) {
    Label passedGuard, failedGuard;
    if (iterable.Value->needsGuard()){
        passedGuard = emit_define_label(), failedGuard = emit_define_label();
        m_il.dup();
        LD_FIELD(PyObject, ob_type);
        emit_ptr(iterable.Value->pythonType());
        emit_branch(BranchEqual, passedGuard);
        emit_unpack_generic(size, iterable);
        emit_branch(BranchAlways, failedGuard);
        emit_mark_label(passedGuard);
    }
    Local t_value = emit_define_local(LK_NativeInt);
    Label raiseValueError = emit_define_label();
    Label returnValues = emit_define_label();
    size_t idx = size, idx2 = size;

    emit_store_local(t_value);

    emit_load_local(t_value);
    emit_list_length();
    emit_int(size);
    emit_branch(BranchNotEqual,raiseValueError);

        while (idx--) {
            emit_load_local(t_value);
            emit_list_load(idx);
            emit_dup();
            emit_incref();
        }
        emit_int(0);
        emit_branch(BranchAlways, returnValues);

    emit_mark_label(raiseValueError);

        while (idx2--) {
            emit_null();
        }
        emit_pyerr_setstring(PyExc_ValueError, "Cannot unpack list due to size mismatch");
        emit_int(-1);

    emit_mark_label(returnValues);
    emit_load_and_free_local(t_value);
    decref();

    if (iterable.Value->needsGuard()){
        emit_mark_label(failedGuard);
    }
}

void PythonCompiler::emit_unpack_generic(size_t size, AbstractValueWithSources iterable) {
    vector<Local> iterated(size);
    Local t_iter = emit_define_local(LK_NativeInt), t_object = emit_define_local(LK_NativeInt);
    Local result = emit_define_local(LK_Int);

    m_il.ld_i4(0);
    emit_store_local(result);

    m_il.dup();
    emit_getiter();
    emit_store_local(t_iter);
    emit_store_local(t_object);

    size_t idx = size;
    while (idx--) {
        iterated[idx] = emit_define_local(LK_NativeInt);
        Label successOrStopIter = emit_define_label(), endbranch = emit_define_label();
        emit_load_local(t_iter);
        emit_for_next();

        m_il.dup();
        emit_branch(BranchTrue, successOrStopIter);
            // Failure
            emit_int(1);
            emit_store_local(result);
            emit_branch(BranchAlways, endbranch);

        emit_mark_label(successOrStopIter);
            // Either success or received stopiter (0xff)
            m_il.dup();
            emit_ptr((void *) 0xff);
            emit_branch(BranchNotEqual, endbranch);
                m_il.pop();
                emit_null();
                emit_pyerr_setstring(PyExc_ValueError, "Cannot unpack object due to size mismatch");
                emit_int(1);
                emit_store_local(result);

        emit_mark_label(endbranch);
        emit_store_local(iterated[idx]);
    }
    for (size_t i = 0; i < size ; i++)
        emit_load_and_free_local(iterated[i]);
    emit_load_and_free_local(t_iter);
    decref();
    emit_free_local(t_object);
    emit_load_and_free_local(result);
}

void PythonCompiler::emit_unpack_sequence(size_t size, AbstractValueWithSources iterable) {
    if (iterable.Value->known()) {
        switch (iterable.Value->kind()) {
            case AVK_Tuple:
                return emit_unpack_tuple(size, iterable);
            case AVK_List:
                return emit_unpack_list(size, iterable);
            default:
                return emit_unpack_generic(size, iterable);
        }
    } else {
        return emit_unpack_generic(size, iterable);
    }
}

void PythonCompiler::fill_local_vector(vector<Local> & vec, size_t len){
    for (size_t i = 0 ; i < len; i++)
        vec[i] = emit_define_local(LK_NativeInt);
}

void PythonCompiler::emit_unpack_sequence_ex(size_t leftSize, size_t rightSize, AbstractValueWithSources iterable) {
    vector<Local> leftLocals(leftSize), rightLocals(rightSize);
    Local t_iter = emit_define_local(LK_NativeInt), t_object = emit_define_local(LK_NativeInt);
    Local result = emit_define_local(LK_Int);
    Local resultList = emit_define_local(LK_NativeInt);
    Label raiseValueError = emit_define_label(), returnValues = emit_define_label();
    fill_local_vector(leftLocals, leftSize);
    fill_local_vector(rightLocals, rightSize);
    m_il.ld_i4(0);
    emit_store_local(result);

    m_il.dup();
    emit_getiter();
    emit_store_local(t_iter);
    emit_store_local(t_object);

    // Step 1 : Iterate the first number of values
    size_t idx = leftSize;
    while (idx--) {
        Label successOrStopIter = emit_define_label(), endbranch = emit_define_label();
            emit_load_local(t_iter);
            emit_for_next();

            m_il.dup();
        emit_branch(BranchTrue, successOrStopIter);
            // Failure
            emit_int(1);
            emit_store_local(result);
            emit_debug_msg("cannot unpack left");
            emit_branch(BranchAlways, endbranch);

        emit_mark_label(successOrStopIter);
            // Either success or received stopiter (0xff)
            m_il.dup();
            emit_ptr((void *) 0xff);
            emit_branch(BranchNotEqual, endbranch);
                m_il.pop();
                emit_null();
                emit_pyerr_setstring(PyExc_ValueError, "Cannot unpack due to size mismatch");
                emit_int(1);
                emit_debug_msg("cannot unpack left - mismatch");
                emit_store_local(result);

        emit_mark_label(endbranch);
        emit_store_local(leftLocals[idx]);
    }

    // If the first part already failed, don't try the second part
    emit_load_local(result);
    emit_branch(BranchTrue, returnValues);

    // If this cant be iterated, return  (should already have exception set on frame
    emit_load_local(t_iter);
    emit_branch(BranchFalse, returnValues);

    // Step 2: Convert the rest of the iterator to a list
    emit_load_local(t_iter);
    m_il.emit_call(METHOD_SEQUENCE_AS_LIST);
    emit_store_local(resultList);

    // Step 3: Yield the right-hand values off the back of the list
    size_t j_idx = rightSize;
    emit_load_local(resultList);
    emit_list_length();
    emit_int(rightSize);
    emit_branch(BranchLessThan, raiseValueError);

        while (j_idx--) {
            emit_load_local(resultList);
            emit_int(j_idx);
            m_il.emit_call(METHOD_LIST_ITEM_FROM_BACK);
            emit_dup();
            emit_incref();
            emit_store_local(rightLocals[j_idx]);
        }
        emit_load_local(resultList);
        emit_list_shrink(rightSize);
        emit_branch(BranchAlways, returnValues);

    emit_mark_label(raiseValueError);
        emit_debug_msg("cannot unpack right");
        emit_pyerr_setstring(PyExc_ValueError, "Cannot unpack due to size mismatch");
        emit_int(1);
        emit_store_local(result);

    emit_mark_label(returnValues);

    // Finally: Return
    for (size_t i = 0; i < rightSize ; i++)
        emit_load_and_free_local(rightLocals[i]);
    emit_load_and_free_local(resultList);
    for (size_t i = 0; i < leftSize ; i++)
        emit_load_and_free_local(leftLocals[i]);

    emit_load_and_free_local(t_iter);
    decref();
    emit_free_local(t_object);
    emit_load_and_free_local(result);
}

/************************************************************************
 * Compiler interface implementation
 */

void PythonCompiler::emit_unbound_local_check() {
    m_il.emit_call(METHOD_UNBOUND_LOCAL);
}

void PythonCompiler::emit_load_fast(int local) {
    load_local(local);
}

CorInfoType PythonCompiler::to_clr_type(LocalKind kind) {
    switch (kind) {
        case LK_Float: return CORINFO_TYPE_DOUBLE;
        case LK_Int: return CORINFO_TYPE_INT;
        case LK_Bool: return CORINFO_TYPE_BOOL;
        case LK_Pointer: return CORINFO_TYPE_PTR;
        case LK_NativeInt: return CORINFO_TYPE_NATIVEINT;
    }
    return CORINFO_TYPE_NATIVEINT;
}


void PythonCompiler::emit_store_fast(int local) {
    if (OPT_ENABLED(nativeLocals)){
        // decref old value and store new value.
        m_il.ld_loc(m_frameLocals[local]);
        decref();
        m_il.st_loc(m_frameLocals[local]);
    } else {
        auto valueTmp = m_il.define_local(Parameter(CORINFO_TYPE_NATIVEINT));
        m_il.st_loc(valueTmp);

        // load the value onto the IL stack, we'll decref it after we replace the
        // value in the frame object so that we never have a freed object in the
        // frame object.
        load_local(local);

        load_frame();
        m_il.ld_i(offsetof(PyFrameObject, f_localsplus) + local * sizeof(size_t));
        m_il.add();

        m_il.ld_loc(valueTmp);

        m_il.st_ind_i();

        m_il.free_local(valueTmp);

        // now dec ref the old value potentially freeing it.
        decref();
    }
}

void PythonCompiler::emit_rot_two(LocalKind kind) {
    auto top = m_il.define_local(Parameter(to_clr_type(kind)));
    auto second = m_il.define_local(Parameter(to_clr_type(kind)));

    m_il.st_loc(top);
    m_il.st_loc(second);

    m_il.ld_loc(top);
    m_il.ld_loc(second);

    m_il.free_local(top);
    m_il.free_local(second);
}

void PythonCompiler::emit_rot_three(LocalKind kind) {
    auto top = m_il.define_local(Parameter(to_clr_type(kind)));
    auto second = m_il.define_local(Parameter(to_clr_type(kind)));
    auto third = m_il.define_local(Parameter(to_clr_type(kind)));

    m_il.st_loc(top);
    m_il.st_loc(second);
    m_il.st_loc(third);

    m_il.ld_loc(top);
    m_il.ld_loc(third);
    m_il.ld_loc(second);

    m_il.free_local(top);
    m_il.free_local(second);
    m_il.free_local(third);
}

void PythonCompiler::emit_rot_four(LocalKind kind) {
    auto top = m_il.define_local(Parameter(to_clr_type(kind)));
    auto second = m_il.define_local(Parameter(to_clr_type(kind)));
    auto third = m_il.define_local(Parameter(to_clr_type(kind)));
    auto fourth = m_il.define_local(Parameter(to_clr_type(kind)));

    m_il.st_loc(top);
    m_il.st_loc(second);
    m_il.st_loc(third);
    m_il.st_loc(fourth);

    m_il.ld_loc(top);
    m_il.ld_loc(fourth);
    m_il.ld_loc(third);
    m_il.ld_loc(second);

    m_il.free_local(top);
    m_il.free_local(second);
    m_il.free_local(third);
    m_il.free_local(fourth);
}

void PythonCompiler::lift_n_to_second(int pos){
    if (pos == 1)
        return ; // already is second
    vector<Local> tmpLocals (pos-1);

    auto top = m_il.define_local(Parameter(to_clr_type(LK_Pointer)));
    m_il.st_loc(top);

    // dump stack up to n
    for (int i = 0 ; i < pos - 1 ; i ++){
        auto loc = m_il.define_local(Parameter(to_clr_type(LK_Pointer)));
        tmpLocals[i] = loc;
        m_il.st_loc(loc);
    }

    // pop n
    auto n = m_il.define_local(Parameter(to_clr_type(LK_Pointer)));
    m_il.st_loc(n);

    // recover stack
    for (auto& loc: tmpLocals){
        m_il.ld_loc(loc);
        m_il.free_local(loc);
    }

    // push n (so its second)
    m_il.ld_loc(n);
    m_il.free_local(n);

    // push top
    m_il.ld_loc(top);
    m_il.free_local(top);
}

void PythonCompiler::lift_n_to_third(int pos){
    if (pos == 1)
        return ; // already is third
    vector<Local> tmpLocals (pos-2);

    auto top = m_il.define_local(Parameter(to_clr_type(LK_Pointer)));
    m_il.st_loc(top);

    auto second = m_il.define_local(Parameter(to_clr_type(LK_Pointer)));
    m_il.st_loc(second);

    // dump stack up to n
    for (int i = 0 ; i < pos - 2 ; i ++){
        auto loc = m_il.define_local(Parameter(to_clr_type(LK_Pointer)));
        tmpLocals[i] = loc;
        m_il.st_loc(loc);
    }

    // pop n
    auto n = m_il.define_local(Parameter(to_clr_type(LK_Pointer)));
    m_il.st_loc(n);

    // recover stack
    for (auto& loc: tmpLocals){
        m_il.ld_loc(loc);
        m_il.free_local(loc);
    }

    // push n (so its third)
    m_il.ld_loc(n);
    m_il.free_local(n);

    // push second
    m_il.ld_loc(second);
    m_il.free_local(second);

    // push top
    m_il.ld_loc(top);
    m_il.free_local(top);
}

void PythonCompiler::sink_top_to_n(int pos){
    if (pos == 0)
        return ; // already is at the correct position
    vector<Local> tmpLocals (pos);

    auto top = m_il.define_local(Parameter(to_clr_type(LK_Pointer)));
    m_il.st_loc(top);

    // dump stack up to n
    for (int i = 0 ; i < pos ; i ++){
        auto loc = m_il.define_local(Parameter(to_clr_type(LK_Pointer)));
        tmpLocals[i] = loc;
        m_il.st_loc(loc);
    }

    // push n
    m_il.ld_loc(top);
    m_il.free_local(top);

    // recover stack
    for (auto& loc: tmpLocals){
        m_il.ld_loc(loc);
        m_il.free_local(loc);
    }
}

void PythonCompiler::lift_n_to_top(int pos){
    vector<Local> tmpLocals (pos);

    // dump stack up to n
    for (int i = 0 ; i < pos ; i ++){
        auto loc = m_il.define_local(Parameter(to_clr_type(LK_Pointer)));
        tmpLocals[i] = loc;
        m_il.st_loc(loc);
    }

    // pop n
    auto n = m_il.define_local(Parameter(to_clr_type(LK_Pointer)));
    m_il.st_loc(n);


    // recover stack
    for (auto& loc: tmpLocals){
        m_il.ld_loc(loc);
        m_il.free_local(loc);
    }

    // push n (so its at the top)
    m_il.ld_loc(n);
    m_il.free_local(n);
}

void PythonCompiler::emit_pop_top() {
    decref();
}
// emit_pop_top is for the POP_TOP opcode, which should pop the stack AND decref. pop_top is just for pop'ing the value.
void PythonCompiler::pop_top() {
    m_il.pop();
}

void PythonCompiler::emit_dup_top() {
    // Dup top and incref
    m_il.dup();
    m_il.dup();
    emit_incref();
}

void PythonCompiler::emit_dup_top_two() {
    auto top = m_il.define_local(Parameter(CORINFO_TYPE_NATIVEINT));
    auto second = m_il.define_local(Parameter(CORINFO_TYPE_NATIVEINT));

    m_il.st_loc(top);
    m_il.st_loc(second);

    m_il.ld_loc(second);
    m_il.ld_loc(top);
    m_il.ld_loc(second);
    m_il.ld_loc(top);

    m_il.ld_loc(top);
    emit_incref();
    m_il.ld_loc(second);
    emit_incref();

    m_il.free_local(top);
    m_il.free_local(second);
}

void PythonCompiler::emit_dict_build_from_map() {
    m_il.emit_call(METHOD_BUILD_DICT_FROM_TUPLES);
}

void PythonCompiler::emit_new_list(size_t argCnt) {
    m_il.ld_i(argCnt);
    m_il.emit_call(METHOD_PYLIST_NEW);
}

void PythonCompiler::emit_load_assertion_error() {
    m_il.emit_call(METHOD_LOAD_ASSERTION_ERROR);
}

void PythonCompiler::emit_list_store(size_t argCnt) {
    auto valueTmp = m_il.define_local(Parameter(CORINFO_TYPE_NATIVEINT));
    auto listTmp = m_il.define_local(Parameter(CORINFO_TYPE_NATIVEINT));
    auto listItems = m_il.define_local(Parameter(CORINFO_TYPE_NATIVEINT));

    m_il.dup();
    m_il.st_loc(listTmp);

    // load the address of the list item...
    m_il.ld_i(offsetof(PyListObject, ob_item));
    m_il.add();
    m_il.ld_ind_i();

    m_il.st_loc(listItems);

    for (size_t i = 0, arg = argCnt - 1; i < argCnt; i++, arg--) {
        // save the argument into a temporary...
        m_il.st_loc(valueTmp);

        // load the address of the list item...
        m_il.ld_loc(listItems);
        m_il.ld_i(arg * sizeof(size_t));
        m_il.add();

        // reload the value
        m_il.ld_loc(valueTmp);

        // store into the array
        m_il.st_ind_i();
    }

    // update the size of the list...
    m_il.ld_loc(listTmp);
    m_il.dup();
    m_il.ld_i(offsetof(PyVarObject, ob_size));
    m_il.add();
    m_il.ld_i(argCnt);
    m_il.st_ind_i();

    m_il.free_local(valueTmp);
    m_il.free_local(listTmp);
    m_il.free_local(listItems);
}

void PythonCompiler::emit_list_extend() {
    m_il.emit_call(METHOD_EXTENDLIST_TOKEN);
}

void PythonCompiler::emit_list_to_tuple() {
    m_il.emit_call(METHOD_LISTTOTUPLE_TOKEN);
}

void PythonCompiler::emit_new_set() {
    m_il.load_null();
    m_il.emit_call(METHOD_PYSET_NEW);
}

void PythonCompiler::emit_pyobject_str() {
    m_il.emit_call(METHOD_PYOBJECT_STR);
}

void PythonCompiler::emit_pyobject_repr() {
    m_il.emit_call(METHOD_PYOBJECT_REPR);
}

void PythonCompiler::emit_pyobject_ascii() {
    m_il.emit_call(METHOD_PYOBJECT_ASCII);
}

void PythonCompiler::emit_pyobject_format() {
    m_il.emit_call(METHOD_FORMAT_OBJECT);
}

void PythonCompiler::emit_unicode_joinarray() {
    m_il.emit_call(METHOD_PYUNICODE_JOINARRAY);
}

void PythonCompiler::emit_format_value() {
    m_il.emit_call(METHOD_FORMAT_VALUE);
}

void PythonCompiler::emit_set_extend() {
    m_il.emit_call(METHOD_SETUPDATE_TOKEN);
}

void PythonCompiler::emit_new_dict(size_t size) {
    m_il.ld_i(size);
    m_il.emit_call(METHOD_PYDICT_NEWPRESIZED);
}

void PythonCompiler::emit_dict_store() {
    m_il.emit_call(METHOD_STOREMAP_TOKEN);
}

void PythonCompiler::emit_dict_store_no_decref() {
    m_il.emit_call(METHOD_STOREMAP_NO_DECREF_TOKEN);
}

void PythonCompiler::emit_map_extend() {
    m_il.emit_call(METHOD_DICTUPDATE_TOKEN);
}

void PythonCompiler::emit_is_true() {
    m_il.emit_call(METHOD_PYOBJECT_ISTRUE);
}

void PythonCompiler::emit_load_name(PyObject* name) {
    load_frame();
    m_il.ld_i(name);
    m_il.emit_call(METHOD_LOADNAME_TOKEN);
}

void PythonCompiler::emit_load_name_hashed(PyObject* name, ssize_t name_hash) {
    load_frame();
    m_il.ld_i(name);
    m_il.ld_i((void*)name_hash);
    m_il.emit_call(METHOD_LOADNAME_HASH);
}

void PythonCompiler::emit_store_name(PyObject* name) {
    load_frame();
    m_il.ld_i(name);
    m_il.emit_call(METHOD_STORENAME_TOKEN);
}

void PythonCompiler::emit_delete_name(PyObject* name) {
    load_frame();
    m_il.ld_i(name);
    m_il.emit_call(METHOD_DELETENAME_TOKEN);
}

void PythonCompiler::emit_store_attr(PyObject* name) {
    m_il.ld_i(name);
    m_il.emit_call(METHOD_STOREATTR_TOKEN);
}

void PythonCompiler::emit_delete_attr(PyObject* name) {
    m_il.ld_i(name);
    m_il.emit_call(METHOD_DELETEATTR_TOKEN);
}

void PythonCompiler::emit_load_attr(PyObject* name, AbstractValueWithSources obj) {
    if (!obj.hasValue() || !obj.Value->known()) {
        m_il.ld_i(name);
        m_il.emit_call(METHOD_LOADATTR_TOKEN);
        return;
    }
    bool guard = obj.Value->needsGuard();
    Local objLocal = emit_define_local(LK_Pointer);
    emit_store_local(objLocal);
    Label skip_guard = emit_define_label(), execute_guard = emit_define_label();
    if (guard) {
        emit_load_local(objLocal);
        LD_FIELD(PyObject, ob_type);
        emit_ptr(obj.Value->pythonType());
        emit_branch(BranchNotEqual, execute_guard);
        emit_load_local(objLocal);
        LD_FIELD(PyObject, ob_type);
        LD_FIELD(PyTypeObject, tp_getattro);
        emit_ptr((void*)obj.Value->pythonType()->tp_getattro);
        emit_branch(BranchNotEqual, execute_guard);
    }

    if (obj.Value->pythonType() != nullptr && obj.Value->pythonType()->tp_getattro){
        // Often its just PyObject_GenericGetAttr to instead of recycling, use that.
        if (obj.Value->pythonType()->tp_getattro == PyObject_GenericGetAttr){
            emit_load_local(objLocal);
            m_il.ld_i(name);
            m_il.emit_call(METHOD_GENERIC_GETATTR);
            emit_load_local(objLocal);
            decref();
        } else {
            auto getattro_token = g_module.AddMethod(CORINFO_TYPE_NATIVEINT,
                                                     vector<Parameter>{
                                                             Parameter(CORINFO_TYPE_NATIVEINT),
                                                             Parameter(CORINFO_TYPE_NATIVEINT)},
                                                     (void *) obj.Value->pythonType()->tp_getattro);
            emit_load_local(objLocal);
            m_il.ld_i(name);
            m_il.emit_call(getattro_token);
            emit_load_local(objLocal);
            decref();
        }
    } else if (obj.Value->pythonType() != nullptr && obj.Value->pythonType()->tp_getattr) {
        auto getattr_token = g_module.AddMethod(CORINFO_TYPE_NATIVEINT,
                                                 vector<Parameter>{
                                                         Parameter(CORINFO_TYPE_NATIVEINT),
                                                         Parameter(CORINFO_TYPE_NATIVEINT)},
                                                 (void *) obj.Value->pythonType()->tp_getattr);
        emit_load_local(objLocal);
        m_il.ld_i((void*)PyUnicode_AsUTF8((PyObject*)name));
        m_il.emit_call(getattr_token);
        emit_load_local(objLocal);
        decref();
    } else {
        emit_load_local(objLocal);
        m_il.ld_i(name);
        m_il.emit_call(METHOD_LOADATTR_TOKEN);
    }

    if (guard){
        emit_branch(BranchAlways, skip_guard);
        emit_mark_label(execute_guard);
        emit_load_local(objLocal);
        m_il.ld_i(name);
        m_il.emit_call(METHOD_LOADATTR_TOKEN);
        emit_mark_label(skip_guard);
    }
    emit_free_local(objLocal);
}

void PythonCompiler::emit_load_attr(PyObject* name) {
    m_il.ld_i(name);
    m_il.emit_call(METHOD_LOADATTR_TOKEN);
}

void PythonCompiler::emit_store_global(PyObject* name) {
    // value is on the stack
    load_frame();
    m_il.ld_i(name);
    m_il.emit_call(METHOD_STOREGLOBAL_TOKEN);
}

void PythonCompiler::emit_delete_global(PyObject* name) {
    load_frame();
    m_il.ld_i(name);
    m_il.emit_call(METHOD_DELETEGLOBAL_TOKEN);
}

void PythonCompiler::emit_load_global(PyObject* name) {
    load_frame();
    m_il.ld_i(name);
    m_il.emit_call(METHOD_LOADGLOBAL_TOKEN);
}

void PythonCompiler::emit_load_global_hashed(PyObject* name, ssize_t name_hash) {
    load_frame();
    m_il.ld_i(name);
    m_il.ld_i(name_hash);
    m_il.emit_call(METHOD_LOADGLOBAL_HASH);
}

void PythonCompiler::emit_delete_fast(int index) {
    if (OPT_ENABLED(nativeLocals)) {
        m_il.ld_loc(m_frameLocals[index]);
        decref();
        m_il.load_null();
        m_il.st_loc(m_frameLocals[index]);
    } else {
        load_local(index);
        load_frame();
        m_il.ld_i(offsetof(PyFrameObject, f_localsplus) + index * sizeof(size_t));
        m_il.add();
        m_il.load_null();
        m_il.st_ind_i();
        decref();
    }
}

void PythonCompiler::emit_new_tuple(size_t size) {
    if (size == 0) {
        m_il.ld_i(PyTuple_New(0));
        m_il.dup();
        // incref 0-tuple so it never gets freed
        emit_incref();
    }
    else {
        m_il.ld_i8(size);
        m_il.emit_call(METHOD_PYTUPLE_NEW);
    }
}

// Loads the specified index from a tuple that's already on the stack
void PythonCompiler::emit_tuple_load(size_t index) {
	m_il.ld_i(index * sizeof(size_t) + offsetof(PyTupleObject, ob_item));
	m_il.add();
	m_il.ld_ind_i();
}

void PythonCompiler::emit_tuple_length(){
    m_il.ld_i(offsetof(PyVarObject, ob_size));
    m_il.add();
    m_il.ld_ind_i();
}

void PythonCompiler::emit_list_load(size_t index) {
    LD_FIELD(PyListObject, ob_item);
    if (index > 0) {
        m_il.ld_i(index * sizeof(size_t));
        m_il.add();
    }
    m_il.ld_ind_i();
}

void PythonCompiler::emit_list_length(){
    m_il.ld_i(offsetof(PyVarObject, ob_size));
    m_il.add();
    m_il.ld_ind_i();
}


void PythonCompiler::emit_tuple_store(size_t argCnt) {
    /// This function emits a tuple from the stack, only using borrowed references.
    auto valueTmp = m_il.define_local(Parameter(CORINFO_TYPE_NATIVEINT));
    auto tupleTmp = m_il.define_local(Parameter(CORINFO_TYPE_NATIVEINT));
    m_il.st_loc(tupleTmp);

    for (size_t i = 0, arg = argCnt - 1; i < argCnt; i++, arg--) {
        // save the argument into a temporary...
        m_il.st_loc(valueTmp);

        // load the address of the tuple item...
        m_il.ld_loc(tupleTmp);
        m_il.ld_i(arg * sizeof(size_t) + offsetof(PyTupleObject, ob_item));
        m_il.add();

        // reload the value
        m_il.ld_loc(valueTmp);

        // store into the array
        m_il.st_ind_i();
    }
    m_il.ld_loc(tupleTmp);

    m_il.free_local(valueTmp);
    m_il.free_local(tupleTmp);
}

void PythonCompiler::emit_store_subscr() {
    // stack is value, container, index
    m_il.emit_call(METHOD_STORE_SUBSCR_OBJ);
}

void PythonCompiler::emit_store_subscr(AbstractValueWithSources value, AbstractValueWithSources container, AbstractValueWithSources key) {
    bool constIndex = false;
    bool hasValidIndex = false;
    ConstSource* constSource = nullptr;
    if (key.Sources != nullptr && key.Sources->hasConstValue()){
        constIndex = true;
        constSource = dynamic_cast<ConstSource*>(key.Sources);
        hasValidIndex = (constSource->hasNumericValue() && constSource->getNumericValue() >= 0);
    }
    switch(container.Value->kind()){
        case AVK_Dict:
            if (constIndex){
                if (constSource->hasHashValue())
                {
                    m_il.ld_i8(constSource->getHash());
                    m_il.emit_call(METHOD_STORE_SUBSCR_DICT_HASH);
                } else {
                    m_il.emit_call(METHOD_STORE_SUBSCR_DICT);
                }
            } else {
                m_il.emit_call(METHOD_STORE_SUBSCR_DICT);
            }
            break;
        case AVK_List:
            if (constIndex){
                if (hasValidIndex){
                    m_il.ld_i8(constSource->getNumericValue());
                    m_il.emit_call(METHOD_STORE_SUBSCR_LIST_I);
                } else {
                    m_il.emit_call(METHOD_STORE_SUBSCR_LIST);
                }
            } else if (key.hasValue() && key.Value->kind() == AVK_Slice){
                // TODO : Optimize storing a list subscript
                m_il.emit_call(METHOD_STORE_SUBSCR_OBJ);
            } else {
                m_il.emit_call(METHOD_STORE_SUBSCR_LIST);
            }
            break;
        default:
            if (constIndex){
                if (hasValidIndex && constSource->hasHashValue()){
                    m_il.ld_i8(constSource->getNumericValue());
                    m_il.ld_i8(constSource->getHash());
                    m_il.emit_call(METHOD_STORE_SUBSCR_OBJ_I_HASH);
                } else if (!hasValidIndex && constSource->hasHashValue()) {
                    m_il.ld_i8(constSource->getHash());
                    m_il.emit_call(METHOD_STORE_SUBSCR_DICT_HASH);
                } else if (hasValidIndex && !constSource->hasHashValue()){
                    m_il.ld_i8(constSource->getNumericValue());
                    m_il.emit_call(METHOD_STORE_SUBSCR_OBJ_I);
                } else {
                    m_il.emit_call(METHOD_STORE_SUBSCR_OBJ);
                }
            } else {
                m_il.emit_call(METHOD_STORE_SUBSCR_OBJ);
            }
    }
}

void PythonCompiler::emit_delete_subscr() {
    // stack is container, index
    m_il.emit_call(METHOD_DELETESUBSCR_TOKEN);
}

void PythonCompiler::emit_binary_subscr(AbstractValueWithSources container, AbstractValueWithSources key) {
    bool constIndex = false;
    ConstSource* constSource = nullptr;
    bool hasValidIndex = false;

    if (key.hasSource() && key.Sources->hasConstValue()){
        constIndex = true;
        constSource = dynamic_cast<ConstSource*>(key.Sources);
        hasValidIndex = (constSource->hasNumericValue() && constSource->getNumericValue() >= 0);
    }
    switch(container.Value->kind()){
        case AVK_Dict:
            if (constIndex){
                if (constSource->hasHashValue())
                {
                    m_il.ld_i8(constSource->getHash());
                    m_il.emit_call(METHOD_SUBSCR_DICT_HASH);
                } else {
                    m_il.emit_call(METHOD_SUBSCR_DICT);
                }
            } else {
                m_il.emit_call(METHOD_SUBSCR_DICT);
            }
            break;
        case AVK_List:
            if (constIndex) {
                if (hasValidIndex) {
                    m_il.ld_i8(constSource->getNumericValue());
                    m_il.emit_call(METHOD_SUBSCR_LIST_I);
                } else {
                    m_il.emit_call(METHOD_SUBSCR_LIST);
                }
            } else if (key.hasValue() && key.Value->kind() == AVK_Slice){
                // TODO : Further optimize getting a slice subscript when the values are dynamic
                m_il.emit_call(METHOD_SUBSCR_OBJ);
            } else {
                m_il.emit_call(METHOD_SUBSCR_LIST);
            }
            break;
        case AVK_Tuple:
            if (constIndex){
                if (hasValidIndex){
                    m_il.ld_i8(constSource->getNumericValue());
                    m_il.emit_call(METHOD_SUBSCR_TUPLE_I);
                } else {
                    m_il.emit_call(METHOD_SUBSCR_TUPLE);
                }
            } else if (key.hasValue() && key.Value->kind() == AVK_Slice){
                m_il.emit_call(METHOD_SUBSCR_OBJ);
            } else {
                m_il.emit_call(METHOD_SUBSCR_TUPLE);
            }
            break;
        default:
            if (constIndex){
                if (hasValidIndex && constSource->hasHashValue()){
                    m_il.ld_i8(constSource->getNumericValue());
                    m_il.ld_i8(constSource->getHash());
                    m_il.emit_call(METHOD_SUBSCR_OBJ_I_HASH);
                } else if (!hasValidIndex && constSource->hasHashValue()) {
                    m_il.ld_i8(constSource->getHash());
                    m_il.emit_call(METHOD_SUBSCR_DICT_HASH);
                } else if (hasValidIndex && !constSource->hasHashValue()){
                    m_il.ld_i8(constSource->getNumericValue());
                    m_il.emit_call(METHOD_SUBSCR_OBJ_I);
                } else {
                    m_il.emit_call(METHOD_SUBSCR_OBJ);
                }
            } else {
                m_il.emit_call(METHOD_SUBSCR_OBJ);
            }
    }
}

bool PythonCompiler::emit_binary_subscr_slice(AbstractValueWithSources container, AbstractValueWithSources start, AbstractValueWithSources stop) {
    bool startIndex = false, stopIndex = false;
    Py_ssize_t start_i, stop_i;

    if (start.hasSource() && start.Sources->hasConstValue()) {
        if (start.Value->kind() == AVK_None) {
            start_i = PY_SSIZE_T_MIN;
            startIndex = true;
        } else if (start.Value->kind() == AVK_Integer) {
            start_i = dynamic_cast<ConstSource *>(start.Sources)->getNumericValue();
            startIndex = true;
        }
    }
    if (stop.hasSource() && stop.Sources->hasConstValue()) {
        if (stop.Value->kind() == AVK_None) {
            stop_i = PY_SSIZE_T_MAX;
            stopIndex = true;
        } else if (stop.Value->kind() == AVK_Integer) {
            stop_i = dynamic_cast<ConstSource *>(stop.Sources)->getNumericValue();
            stopIndex = true;
        }
    }
    switch (container.Value->kind()) {
        case AVK_List:
            if (startIndex && stopIndex) {
                decref(); decref(); // will also pop the values
                m_il.ld_i8(start_i);
                m_il.ld_i8(stop_i);
                m_il.emit_call(METHOD_SUBSCR_LIST_SLICE);
                return true;
            }
            break;
    }
    return false;
}

bool PythonCompiler::emit_binary_subscr_slice(AbstractValueWithSources container, AbstractValueWithSources start, AbstractValueWithSources stop, AbstractValueWithSources step) {
    bool startIndex = false, stopIndex = false, stepIndex = false;
    Py_ssize_t start_i, stop_i, step_i;

    if (start.hasSource() && start.Sources->hasConstValue()) {
        if (start.Value->kind() == AVK_None) {
            start_i = PY_SSIZE_T_MIN;
            startIndex = true;
        } else if (start.Value->kind() == AVK_Integer) {
            start_i = dynamic_cast<ConstSource *>(start.Sources)->getNumericValue();
            startIndex = true;
        }
    }
    if (stop.hasSource() && stop.Sources->hasConstValue()) {
        if (stop.Value->kind() == AVK_None) {
            stop_i = PY_SSIZE_T_MAX;
            stopIndex = true;
        } else if (stop.Value->kind() == AVK_Integer) {
            stop_i = dynamic_cast<ConstSource *>(stop.Sources)->getNumericValue();
            stopIndex = true;
        }
    }
    if (step.hasSource() && step.Sources->hasConstValue()) {
        if (step.Value->kind() == AVK_None) {
            step_i = 1;
            stepIndex = true;
        } else if (step.Value->kind() == AVK_Integer) {
            step_i = dynamic_cast<ConstSource *>(step.Sources)->getNumericValue();
            stepIndex = true;
        }
    }
    switch (container.Value->kind()) {
        case AVK_List:
            if (start_i == PY_SSIZE_T_MIN && stop_i == PY_SSIZE_T_MAX && step_i == -1){
                m_il.pop(); m_il.pop(); m_il.pop(); // NB: Don't bother decref'ing None or -1 since they're permanent values anyway
                m_il.emit_call(METHOD_SUBSCR_LIST_SLICE_REVERSED);
                return true;
            }
            else if (startIndex && stopIndex && stepIndex) {
                decref(); decref(); decref(); // will also pop the values
                m_il.ld_i8(start_i);
                m_il.ld_i8(stop_i);
                m_il.ld_i8(step_i);
                m_il.emit_call(METHOD_SUBSCR_LIST_SLICE_STEPPED);
                return true;
            }
            break;
    }
    return false;
}


void PythonCompiler::emit_build_slice() {
    m_il.emit_call(METHOD_BUILD_SLICE);
}

void PythonCompiler::emit_unary_positive() {
    m_il.emit_call(METHOD_UNARY_POSITIVE);
}

void PythonCompiler::emit_unary_negative() {
    m_il.emit_call(METHOD_UNARY_NEGATIVE);
}

void PythonCompiler::emit_unary_not_push_int() {
    m_il.emit_call(METHOD_UNARY_NOT_INT);
}

void PythonCompiler::emit_unary_not() {
    m_il.emit_call(METHOD_UNARY_NOT);
}

void PythonCompiler::emit_unary_negative_float() {
    m_il.neg();
}

void PythonCompiler::emit_unary_invert() {
    m_il.emit_call(METHOD_UNARY_INVERT);
}

void PythonCompiler::emit_import_name(void* name) {
    m_il.ld_i(name);
    load_frame();
    m_il.emit_call(METHOD_PY_IMPORTNAME);
}

void PythonCompiler::emit_import_from(void* name) {
    m_il.dup();
    m_il.ld_i(name);
    m_il.emit_call(METHOD_PY_IMPORTFROM);
}

void PythonCompiler::emit_import_star() {
    load_frame();
    m_il.emit_call(METHOD_PY_IMPORTSTAR);
}

void PythonCompiler::emit_load_build_class() {
    load_frame();
    m_il.emit_call(METHOD_GETBUILDCLASS_TOKEN);
}

Local PythonCompiler::emit_define_local(LocalKind kind) {
    return m_il.define_local(Parameter(to_clr_type(kind)));
}

Local PythonCompiler::emit_define_local(bool cache) {
    if (cache) {
        return m_il.define_local(Parameter(CORINFO_TYPE_NATIVEINT));
    }
    else {
        return m_il.define_local_no_cache(Parameter(CORINFO_TYPE_NATIVEINT));
    }
}

void PythonCompiler::emit_call_args() {
    m_il.emit_call(METHOD_CALL_ARGS);
}

void PythonCompiler::emit_call_kwargs() {
    m_il.emit_call(METHOD_CALL_KWARGS);
}

bool PythonCompiler::emit_call_function(size_t argCnt) {
    switch (argCnt) {
        case 0: m_il.emit_call(METHOD_CALL_0_TOKEN); return true;
        case 1: m_il.emit_call(METHOD_CALL_1_TOKEN); return true;
        case 2: m_il.emit_call(METHOD_CALL_2_TOKEN); return true;
        case 3: m_il.emit_call(METHOD_CALL_3_TOKEN); return true;
        case 4: m_il.emit_call(METHOD_CALL_4_TOKEN); return true;
        case 5: m_il.emit_call(METHOD_CALL_5_TOKEN); return true;
        case 6: m_il.emit_call(METHOD_CALL_6_TOKEN); return true;
        case 7: m_il.emit_call(METHOD_CALL_7_TOKEN); return true;
        case 8: m_il.emit_call(METHOD_CALL_8_TOKEN); return true;
        case 9: m_il.emit_call(METHOD_CALL_9_TOKEN); return true;
        case 10: m_il.emit_call(METHOD_CALL_10_TOKEN); return true;
        default:
            return false;
    }
    return false;
}

bool PythonCompiler::emit_method_call(size_t argCnt) {
    switch (argCnt) {
        case 0: m_il.emit_call(METHOD_METHCALL_0_TOKEN); return true;
        case 1: m_il.emit_call(METHOD_METHCALL_1_TOKEN); return true;
        case 2: m_il.emit_call(METHOD_METHCALL_2_TOKEN); return true;
        case 3: m_il.emit_call(METHOD_METHCALL_3_TOKEN); return true;
        case 4: m_il.emit_call(METHOD_METHCALL_4_TOKEN); return true;
        case 5: m_il.emit_call(METHOD_METHCALL_5_TOKEN); return true;
        case 6: m_il.emit_call(METHOD_METHCALL_6_TOKEN); return true;
        case 7: m_il.emit_call(METHOD_METHCALL_7_TOKEN); return true;
        case 8: m_il.emit_call(METHOD_METHCALL_8_TOKEN); return true;
        case 9: m_il.emit_call(METHOD_METHCALL_9_TOKEN); return true;
        case 10: m_il.emit_call(METHOD_METHCALL_10_TOKEN); return true;
        default:
            return false;
    }
    return false;
}

void PythonCompiler::emit_method_call_n(){
    m_il.emit_call(METHOD_METHCALLN_TOKEN);
}

void PythonCompiler::emit_call_with_tuple() {
    m_il.emit_call(METHOD_CALLN_TOKEN);
}

void PythonCompiler::emit_kwcall_with_tuple() {
    m_il.emit_call(METHOD_KWCALLN_TOKEN);
}

void PythonCompiler::emit_store_local(Local local) {
    m_il.st_loc(local);
}

Local PythonCompiler::emit_spill() {
    auto tmp = m_il.define_local(Parameter(CORINFO_TYPE_NATIVEINT));
    m_il.st_loc(tmp);
    return tmp;
}

void PythonCompiler::emit_load_and_free_local(Local local) {
    m_il.ld_loc(local);
    m_il.free_local(local);
}

void PythonCompiler::emit_load_local(Local local) {
    m_il.ld_loc(local);
}

void PythonCompiler::emit_load_local_addr(Local local) {
    m_il.ld_loca(local);
}

void PythonCompiler::emit_pop() {
    m_il.pop();
}

void PythonCompiler::emit_dup() {
    m_il.dup();
}

void PythonCompiler::emit_free_local(Local local) {
    m_il.free_local(local);
}

void PythonCompiler::emit_branch(BranchType branchType, Label label) {
    m_il.branch(branchType, label);
}

void PythonCompiler::emit_restore_err() {
    m_il.emit_call(METHOD_PYERR_RESTORE);
}

void PythonCompiler::emit_compare_exceptions() {
    m_il.emit_call(METHOD_COMPARE_EXCEPTIONS);
}

void PythonCompiler::emit_pyerr_setstring(void* exception, const char*msg) {
    emit_ptr(exception);
    emit_ptr((void*)msg);
    m_il.emit_call(METHOD_PYERR_SETSTRING);
}

void PythonCompiler::emit_unwind_eh(Local prevExc, Local prevExcVal, Local prevTraceback) {
    m_il.ld_loc(prevExc);
    m_il.ld_loc(prevExcVal);
    m_il.ld_loc(prevTraceback);
    m_il.emit_call(METHOD_UNWIND_EH);
}

void PythonCompiler::emit_prepare_exception(Local prevExc, Local prevExcVal, Local prevTraceback) {
    auto excType = m_il.define_local(Parameter(CORINFO_TYPE_NATIVEINT));
    auto ehVal = m_il.define_local(Parameter(CORINFO_TYPE_NATIVEINT));
    auto tb = m_il.define_local(Parameter(CORINFO_TYPE_NATIVEINT));
    m_il.ld_loca(excType);
    m_il.ld_loca(ehVal);
    m_il.ld_loca(tb);

    m_il.ld_loca(prevExc);
    m_il.ld_loca(prevExcVal);
    m_il.ld_loca(prevTraceback);

    m_il.emit_call(METHOD_PREPARE_EXCEPTION);
    m_il.ld_loc(tb);
    m_il.ld_loc(ehVal);
    m_il.ld_loc(excType);

    m_il.free_local(excType);
    m_il.free_local(ehVal);
    m_il.free_local(tb);
}

void PythonCompiler::emit_int(int value) {
    m_il.ld_i4(value);
}

void PythonCompiler::emit_long_long(long long value) {
    m_il.ld_i8(value);
}

void PythonCompiler::emit_reraise() {
    m_il.emit_call(METHOD_UNWIND_EH);
}

void PythonCompiler::emit_float(double value) {
    m_il.ld_r8(value);
}

void PythonCompiler::emit_ptr(void* value) {
    m_il.ld_i(value);
}

void PythonCompiler::emit_bool(bool value) {
    m_il.ld_i4(value);
}

// Emits a call to create a new function, consuming the code object and
// the qualified name.
void PythonCompiler::emit_new_function() {
    load_frame();
    m_il.emit_call(METHOD_NEWFUNCTION_TOKEN);
}

void PythonCompiler::emit_setup_annotations() {
    load_frame();
    m_il.emit_call(METHOD_SETUP_ANNOTATIONS);
}

void PythonCompiler::emit_set_closure() {
	auto func = emit_spill();
	m_il.ld_i(offsetof(PyFunctionObject, func_closure));
	m_il.add();
	emit_load_and_free_local(func);
	m_il.st_ind_i();
}

void PythonCompiler::emit_set_annotations() {
	auto tmp = emit_spill();
	m_il.ld_i(offsetof(PyFunctionObject, func_annotations));
	m_il.add();
	emit_load_and_free_local(tmp);
    m_il.st_ind_i();
}

void PythonCompiler::emit_set_kw_defaults() {
	auto tmp = emit_spill();
	m_il.ld_i(offsetof(PyFunctionObject, func_kwdefaults));
	m_il.add();
	emit_load_and_free_local(tmp);
	m_il.st_ind_i();
}

void PythonCompiler::emit_set_defaults() {
	auto tmp = emit_spill();
	m_il.ld_i(offsetof(PyFunctionObject, func_defaults));
	m_il.add();
	emit_load_and_free_local(tmp);
	m_il.st_ind_i();
}

void PythonCompiler::emit_load_deref(int index) {
    load_frame();
    m_il.ld_i4(index);
    m_il.emit_call(METHOD_PYCELL_GET);
}

void PythonCompiler::emit_store_deref(int index) {
    load_frame();
    m_il.ld_i4(index);
    m_il.emit_call(METHOD_PYCELL_SET_TOKEN);
}

void PythonCompiler::emit_delete_deref(int index) {
    m_il.load_null();
    load_frame();
    m_il.ld_i4(index);
    m_il.emit_call(METHOD_PYCELL_SET_TOKEN);
}

void PythonCompiler::emit_load_closure(int index) {
    load_frame();
    m_il.ld_i4(index);
    m_il.emit_call(METHOD_LOAD_CLOSURE);
}

void PythonCompiler::emit_set_add() {
    // due to FOR_ITER magic we store the
    // iterable off the stack, and oparg here is based upon the stacking
    // of the generator indexes, so we don't need to spill anything...
    m_il.emit_call(METHOD_SET_ADD_TOKEN);
}

void PythonCompiler::emit_set_update() {
    m_il.emit_call(METHOD_SETUPDATE_TOKEN);
}

void PythonCompiler::emit_dict_merge() {
    m_il.emit_call(METHOD_DICT_MERGE);
}

void PythonCompiler::emit_map_add() {
    m_il.emit_call(METHOD_MAP_ADD_TOKEN);
}

void PythonCompiler::emit_list_append() {
    m_il.emit_call(METHOD_LIST_APPEND_TOKEN);
}

void PythonCompiler::emit_null() {
    m_il.load_null();
}

void PythonCompiler::emit_raise_varargs() {
    // raise exc
    m_il.emit_call(METHOD_DO_RAISE);
}

void PythonCompiler::emit_print_expr() {
    m_il.emit_call(METHOD_PRINT_EXPR_TOKEN);
}

void PythonCompiler::emit_dict_update() {
    m_il.emit_call(METHOD_DICTUPDATE_TOKEN);
}

void PythonCompiler::emit_load_classderef(int index) {
    load_frame();
    m_il.ld_i(index);
    m_il.emit_call(METHOD_LOAD_CLASSDEREF_TOKEN);
}

void PythonCompiler::emit_getiter() {
    m_il.emit_call(METHOD_GETITER_TOKEN);
}

Label PythonCompiler::emit_define_label() {
    return m_il.define_label();
}

void PythonCompiler::emit_inc_local(Local local, int value) {
    emit_int(value);
    emit_load_local(local);
    m_il.add();
    emit_store_local(local);
}

void PythonCompiler::emit_dec_local(Local local, int value) {
    emit_load_local(local);
    emit_int(value);
    m_il.sub();
    emit_store_local(local);
}

void PythonCompiler::emit_ret(int size) {
    m_il.ret(size);
}

void PythonCompiler::emit_mark_label(Label label) {
    m_il.mark_label(label);
}

void PythonCompiler::emit_for_next() {
    m_il.emit_call(METHOD_ITERNEXT_TOKEN);
}

void PythonCompiler::emit_varobject_iter_next(int seq_offset, int index_offset, int ob_item_offset){
    auto exhaust = emit_define_label();
    auto exhausted = emit_define_label();
    auto end = emit_define_label();
    auto it_seq = emit_define_local(LK_NativeInt);
    auto item = emit_define_local(LK_NativeInt);

    auto it = emit_spill();

    emit_load_local(it);
    m_il.ld_i(seq_offset);
    m_il.add();
    m_il.ld_ind_i();
    emit_dup();
    emit_store_local(it_seq); // it_seq = it->it_seq

    emit_null();
    emit_branch(BranchEqual, exhausted); // if (it_seq ==nullptr) goto exhausted;

    // Get next iteration
    emit_load_local(it);
    m_il.ld_i(index_offset);
    m_il.add();
    m_il.ld_ind_i();
    emit_load_local(it_seq);
    LD_FIELD(PyVarObject, ob_size);
    emit_branch(BranchGreaterThanEqual, exhaust); // if (it->it_index < it_seq->ob_size) goto exhaust;

    emit_load_local(it_seq);
    m_il.ld_i(ob_item_offset);
    m_il.add();
    m_il.ld_ind_i();
    emit_load_local(it);
    m_il.ld_i(index_offset);
    m_il.add();
    m_il.ld_ind_i();
    m_il.ld_i(sizeof(PyObject*));
    m_il.mul();
    m_il.add();
    m_il.ld_ind_i();
    emit_store_local(item);

    emit_load_local(it);
    m_il.ld_i(index_offset);
    m_il.add();
    m_il.dup();
    m_il.ld_ind_i();
    m_il.load_one();
    m_il.add();
    m_il.st_ind_i(); // it->it_index++

    emit_load_local(item);
    emit_incref(); // Py_INCREF(item);

    emit_load_and_free_local(item);
    emit_branch(BranchAlways, end); // Return item

    emit_mark_label(exhaust);
    emit_load_local(it);
    m_il.ld_i(seq_offset);
    m_il.add();
    emit_null();
    m_il.st_ind_i();   // it->it_seq = nullptr;

    emit_load_local(it_seq);
    decref();             // Py_DECREF(it->it_seq); return 0xff

    emit_mark_label(exhausted);
    emit_ptr((void *) 0xff); // Return 0xff

    emit_mark_label(end); // Clean-up
    emit_free_local(it);
    emit_free_local(it_seq);
}

void PythonCompiler::emit_for_next(AbstractValueWithSources iterator) {
    /*
     * Stack will have 1 value, the iterator object created by GET_ITER.
     * Function should leave a 64-bit ptr on the stack:
     *  - NULL (error occurred)
     *  - 0xff (StopIter/ iterator exhausted)
     *  - PyObject* (next item in iteration)
     */
    if (iterator.Value->kind() != AVK_Iterable)
        return emit_for_next();
    auto iterable = dynamic_cast<IteratorSource*>(iterator.Sources);
    switch (iterable->kind()) {
        // TODO: Implement a guard-safe iterator.
//        case AVK_List:
//            emit_varobject_iter_next(offsetof(_listiterobject, it_seq), offsetof(_listiterobject, it_index),
//                                         offsetof(PyListObject, ob_item));
//            break;
        default:
            return emit_for_next();
    }
}

void PythonCompiler::emit_debug_msg(const char* msg) {
#ifdef DEBUG
    m_il.ld_i((void*)msg);
    m_il.emit_call(METHOD_DEBUG_TRACE);
#endif
}

void PythonCompiler::emit_debug_pyobject() {
    m_il.emit_call(METHOD_DEBUG_PYOBJECT);
}

void PythonCompiler::emit_binary_float(int opcode) {
    switch (opcode) {
        case BINARY_ADD:
        case INPLACE_ADD:
            m_il.add();
            break;
        case INPLACE_TRUE_DIVIDE:
        case BINARY_TRUE_DIVIDE:
            m_il.div();
            break;
        case INPLACE_MODULO:
        case BINARY_MODULO:
            m_il.mod();
            break;
        case INPLACE_MULTIPLY:
        case BINARY_MULTIPLY:
            m_il.mul();
            break;
        case INPLACE_SUBTRACT:
        case BINARY_SUBTRACT:
            m_il.sub_with_overflow();
            break;
        case BINARY_POWER:
        case INPLACE_POWER:
            m_il.emit_call(METHOD_FLOAT_POWER_TOKEN);
            break;
        case BINARY_FLOOR_DIVIDE:
        case INPLACE_FLOOR_DIVIDE:
            m_il.div();
            m_il.emit_call(METHOD_FLOAT_FLOOR_TOKEN);
            break;
    }
}

void PythonCompiler::emit_binary_subscr(int opcode, AbstractValueWithSources left, AbstractValueWithSources right) {
    if (OPT_ENABLED(knownBinarySubscr)){
        emit_binary_subscr(left, right);
    } else {
        m_il.emit_call(METHOD_SUBSCR_OBJ);
    }
}

void PythonCompiler::emit_is(bool isNot) {
    if (OPT_ENABLED(inlineIs)){
        auto left = m_il.define_local(Parameter(CORINFO_TYPE_NATIVEINT));
        auto right = m_il.define_local(Parameter(CORINFO_TYPE_NATIVEINT));

        m_il.st_loc(left);
        m_il.st_loc(right);

        m_il.ld_loc(right);
        m_il.ld_loc(left);

        auto branchType = isNot ? BranchNotEqual : BranchEqual;
        Label match = emit_define_label();
        Label end = emit_define_label();
        emit_branch(branchType, match);
        emit_ptr(Py_False);
        emit_dup();
        emit_incref();
        emit_branch(BranchAlways, end);
        emit_mark_label(match);
        emit_ptr(Py_True);
        emit_dup();
        emit_incref();
        emit_mark_label(end);

        emit_load_and_free_local(left);
        decref();
        emit_load_and_free_local(right);
        decref();
    } else {
        m_il.emit_call(isNot ? METHOD_ISNOT : METHOD_IS);
    }
}


void PythonCompiler::emit_in() {
    m_il.emit_call(METHOD_CONTAINS_TOKEN);
}


void PythonCompiler::emit_not_in() {
    m_il.emit_call(METHOD_NOTCONTAINS_TOKEN);
}

void PythonCompiler::emit_compare_float(int compareType) {
    // TODO: Optimize compare and POP_JUMP
    // @body: If we know we're followed by the pop jump we could combine
    // and do a single branch comparison.
    switch (compareType) {
        case Py_EQ: m_il.compare_eq(); break;
        case Py_LT: m_il.compare_lt(); break;
        case Py_LE: m_il.compare_le_float(); break;
        case Py_NE: m_il.compare_ne(); break;
        case Py_GT: m_il.compare_gt(); break;
        case Py_GE: m_il.compare_ge_float(); break;
    }
}

void PythonCompiler::emit_compare_tagged_int(int compareType) {
    switch (compareType) {
        case Py_EQ:
            m_il.emit_call(METHOD_EQUALS_INT_TOKEN); break;
        case Py_LT:
            m_il.emit_call(METHOD_LESS_THAN_INT_TOKEN); break;
        case Py_LE:
            m_il.emit_call(METHOD_LESS_THAN_EQUALS_INT_TOKEN); break;
        case Py_NE:
            m_il.emit_call(METHOD_NOT_EQUALS_INT_TOKEN); break;
        case Py_GT:
            m_il.emit_call(METHOD_GREATER_THAN_INT_TOKEN); break;
        case Py_GE:
            m_il.emit_call(METHOD_GREATER_THAN_EQUALS_INT_TOKEN); break;
    }
}

void PythonCompiler::emit_compare_object(int compareType) {
    m_il.ld_i4(compareType);
    m_il.emit_call(METHOD_RICHCMP_TOKEN);
}

void PythonCompiler::emit_compare_known_object(int compareType, AbstractValueWithSources lhs, AbstractValueWithSources rhs) {
    // OPT-3 Optimize the comparison of an intern'ed const integer with an integer to an IS_OP expression.
    if ((lhs.Value->isIntern() && rhs.Value->kind() == AVK_Integer) ||
        (rhs.Value->isIntern() && lhs.Value->kind() == AVK_Integer)){
        switch(compareType) { // NOLINT(hicpp-multiway-paths-covered)
            case Py_EQ:
                emit_is(false);
                return;
            case Py_NE:
                emit_is(true);
                return;
        }
    }
    emit_compare_object(compareType);
}

void PythonCompiler::emit_load_method(void* name) {
    auto * methodLocation = reinterpret_cast<PyJitMethodLocation *>(_PyObject_New(&PyJitMethodLocation_Type));
    methodLocation->method = nullptr;
    methodLocation->object = nullptr;

    m_il.ld_i(name);
    emit_ptr(methodLocation);
    m_il.emit_call(METHOD_LOAD_METHOD);
}

void PythonCompiler::emit_init_instr_counter() {
    m_instrCount = emit_define_local(LK_Int);
    m_il.load_null();
    emit_store_local(m_instrCount);
}

void PythonCompiler::emit_pending_calls(){
    Label skipPending = emit_define_label();
    m_il.ld_loc(m_instrCount);
    m_il.load_one();
    m_il.add();
    m_il.dup();
    m_il.st_loc(m_instrCount);
    m_il.ld_i4(EMIT_PENDING_CALL_COUNTER);
    m_il.mod();
    emit_branch(BranchTrue, skipPending);
    m_il.emit_call(METHOD_PENDING_CALLS);
    m_il.pop(); // TODO : Handle error from Py_MakePendingCalls?
    emit_mark_label(skipPending);
}

void PythonCompiler::emit_builtin_method(PyObject* name, AbstractValue* typeValue) {
    auto pyType = typeValue->pythonType();

    if (pyType == nullptr)
    {
        emit_dup();
        emit_load_method(name); // Can't inline this type of method
        return;
    }

    auto meth = _PyType_Lookup(pyType, name);

    if (meth == nullptr || !PyType_HasFeature(Py_TYPE(meth), Py_TPFLAGS_METHOD_DESCRIPTOR)) {
        emit_dup();
        emit_load_method(name); // Can't inline this type of method
        return;
    }
    auto* methLocationObject = reinterpret_cast<PyJitMethodLocation *>(_PyObject_New(&PyJitMethodLocation_Type));
    methLocationObject->method = meth;
    methLocationObject->object = nullptr;

    auto obj = emit_define_local(LK_Pointer);
    emit_store_local(obj);
    emit_ptr(methLocationObject);
    auto meth_location = emit_define_local(LK_Pointer);
    emit_store_local(meth_location);

    emit_load_local(meth_location);
    emit_incref();

    emit_load_local(meth_location);
    LD_FIELDA(PyJitMethodLocation, object);
    emit_load_local(obj);
    m_il.st_ind_i();

    emit_ptr(meth);
    emit_incref();

    emit_load_and_free_local(obj);
    emit_load_and_free_local(meth_location);
}

void PythonCompiler::emit_call_function_inline(size_t n_args, AbstractValueWithSources func) {
    auto functionType = func.Value->pythonType();
    PyObject* functionObject = nullptr;
    Local argumentLocal = emit_define_local(LK_Pointer),
          functionLocal = emit_define_local(LK_Pointer),
          gstate = emit_define_local(LK_Pointer);
    Label fallback = emit_define_label(), pass = emit_define_label();

    m_il.emit_call(METHOD_GIL_ENSURE);
    emit_store_local(gstate);

    if (func.Sources->isBuiltin()){
        auto builtin = reinterpret_cast<BuiltinSource*>(func.Sources);
        functionType = builtin->getValue()->ob_type;
        functionObject = builtin->getValue();
    }
    if (func.Value->needsGuard()){
        auto vol = reinterpret_cast<VolatileValue*>(func.Value);
        functionObject = vol->lastValue();
    }
    emit_new_tuple(n_args);
    if (n_args != 0) {
        emit_tuple_store(n_args);
    }
    emit_store_local(argumentLocal);
    emit_store_local(functionLocal);
    if (functionType != &PyCFunction_Type ||
        functionObject == nullptr ||
        _PyObject_IsFreed(functionObject) ||
        !PyCFunction_Check(functionObject))
    {
        emit_load_local(functionLocal);
        emit_load_local(argumentLocal);
        emit_null(); // kwargs
        m_il.emit_call(METHOD_OBJECTCALL);
    } else {
        int flags = PyCFunction_GET_FLAGS(functionObject);
        if (!(flags & METH_VARARGS)) {
            emit_load_local(functionLocal);
            emit_load_local(argumentLocal);
            /* If this is not a METH_VARARGS function, delegate to vectorcall */
            emit_null(); // kwargs is always null
            if (func.Value->needsGuard()) {
                emit_load_local(functionLocal);
                emit_ptr(functionObject);
                emit_branch(BranchNotEqual, fallback);
                m_il.emit_call(METHOD_VECTORCALL);
                emit_branch(BranchAlways, pass);
                emit_mark_label(fallback);
                m_il.emit_call(METHOD_OBJECTCALL);
                emit_mark_label(pass);
            } else {
                m_il.emit_call(METHOD_VECTORCALL);
            }
        } else {
            PyCFunction meth = PyCFunction_GET_FUNCTION(functionObject);
            PyObject *self = PyCFunction_GET_SELF(functionObject);
            if (func.Value->needsGuard()) {
                emit_load_local(functionLocal);
                emit_ptr(functionObject);
                emit_branch(BranchNotEqual, fallback);
            }

            emit_ptr(self);
            emit_load_local(argumentLocal);

            int builtinToken;
            if (flags & METH_KEYWORDS) {
                emit_null();
                builtinToken = g_module.AddMethod(CORINFO_TYPE_NATIVEINT,
                                                  vector<Parameter>{
                                                          Parameter(CORINFO_TYPE_NATIVEINT), // Self
                                                          Parameter(CORINFO_TYPE_NATIVEINT), // Args-tuple
                                                          Parameter(CORINFO_TYPE_NATIVEINT)}, // kwargs
                                                  (void *) meth);
            } else {
                builtinToken = g_module.AddMethod(CORINFO_TYPE_NATIVEINT,
                                                  vector<Parameter>{
                                                          Parameter(CORINFO_TYPE_NATIVEINT), // Self
                                                          Parameter(CORINFO_TYPE_NATIVEINT)}, // Args-tuple
                                                  (void *) meth);
            }
            m_il.emit_call(builtinToken);

            if (func.Value->needsGuard()) {
                emit_branch(BranchAlways, pass);
                emit_mark_label(fallback);
                emit_load_local(functionLocal);
                emit_load_local(argumentLocal);
                m_il.emit_call(METHOD_OBJECTCALL);
                emit_mark_label(pass);
            }
        }
    }
    emit_load_local(gstate);
    m_il.emit_call(METHOD_GIL_RELEASE);
    // Decref all the args.
    // Because this tuple was built with borrowed references, it has the effect of decref'ing all args
    emit_load_and_free_local(argumentLocal);
    decref();
    emit_load_and_free_local(functionLocal);
    decref();
}

JittedCode* PythonCompiler::emit_compile() {
    auto* jitInfo = new CorJitInfo(m_code, m_module);
    auto addr = m_il.compile(jitInfo, g_jit, m_code->co_stacksize + 100).m_addr;
    if (addr == nullptr) {
#ifdef DEBUG
        printf("Compiling failed %s from %s line %d\r\n",
            PyUnicode_AsUTF8(m_code->co_name),
            PyUnicode_AsUTF8(m_code->co_filename),
            m_code->co_firstlineno
            );
#endif
        delete jitInfo;
        return nullptr;
    } 
#ifdef DEBUG
    else {
        printf("Compiling success %s from %s line %d\r\n",
            PyUnicode_AsUTF8(m_code->co_name),
            PyUnicode_AsUTF8(m_code->co_filename),
            m_code->co_firstlineno
            );
    }
#endif
    return jitInfo;
}

void PythonCompiler::emit_tagged_int_to_float() {
    m_il.emit_call(METHOD_INT_TO_FLOAT);
}

void PythonCompiler::emit_pgc_probe(size_t curByte, size_t stackSize) {
    vector<Local> stack;
    stack.resize(stackSize);
    Local hasProbedFlag = emit_define_local(LK_Bool);
    auto hasProbed = emit_define_label();

    emit_load_local(hasProbedFlag);
    emit_branch(BranchTrue, hasProbed);
    for (size_t i = 0; i < stackSize; i++){
        stack[i] = emit_define_local(LK_Pointer);
        emit_store_local(stack[i]);

        m_il.ld_arg(3);
        emit_load_local(stack[i]);
        m_il.ld_i8(curByte);
        emit_int(i);

        m_il.emit_call(METHOD_PGC_PROBE);
    }
    m_il.ld_i4(1);
    emit_store_local(hasProbedFlag);
    // Recover the stack in the right order
    for (size_t i = stackSize; i > 0; --i){
        emit_load_and_free_local(stack[i-1]);
    }

    emit_mark_label(hasProbed);
}



/************************************************************************
* End Compiler interface implementation
*/

class GlobalMethod {
    JITMethod m_method;
public:
    GlobalMethod(int token, JITMethod method) : m_method(method) {
        m_method = method;
        g_module.m_methods[token] = &m_method;
    }
};

#define GLOBAL_METHOD(token, addr, returnType, ...) \
    GlobalMethod g ## token(token, JITMethod(&g_module, returnType, std::vector<Parameter>{__VA_ARGS__}, (void*)addr));

GLOBAL_METHOD(METHOD_ADD_TOKEN, &PyJit_Add, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_SUBSCR_OBJ, &PyJit_Subscr, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_SUBSCR_OBJ_I, &PyJit_SubscrIndex, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_SUBSCR_OBJ_I_HASH, &PyJit_SubscrIndexHash, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_SUBSCR_DICT, &PyJit_SubscrDict, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_SUBSCR_DICT_HASH, &PyJit_SubscrDictHash, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_SUBSCR_LIST, &PyJit_SubscrList, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_SUBSCR_LIST_I, &PyJit_SubscrListIndex, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_SUBSCR_LIST_SLICE, &PyJit_SubscrListSlice, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_SUBSCR_LIST_SLICE_STEPPED, &PyJit_SubscrListSliceStepped, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_SUBSCR_LIST_SLICE_REVERSED, &PyJit_SubscrListReversed, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_SUBSCR_TUPLE, &PyJit_SubscrTuple, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_SUBSCR_TUPLE_I, &PyJit_SubscrTupleIndex, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_MULTIPLY_TOKEN, &PyJit_Multiply, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_DIVIDE_TOKEN, &PyJit_TrueDivide, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_FLOORDIVIDE_TOKEN, &PyJit_FloorDivide, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_POWER_TOKEN, &PyJit_Power, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_SUBTRACT_TOKEN, &PyJit_Subtract, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_MODULO_TOKEN, &PyJit_Modulo, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_MATRIX_MULTIPLY_TOKEN, &PyJit_MatrixMultiply, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_BINARY_LSHIFT_TOKEN, &PyJit_BinaryLShift, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_BINARY_RSHIFT_TOKEN, &PyJit_BinaryRShift, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_BINARY_AND_TOKEN, &PyJit_BinaryAnd, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_BINARY_XOR_TOKEN, &PyJit_BinaryXor, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_BINARY_OR_TOKEN, &PyJit_BinaryOr, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_PYLIST_NEW, &PyJit_NewList, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_EXTENDLIST_TOKEN, &PyJit_ExtendList, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_LISTTOTUPLE_TOKEN, &PyJit_ListToTuple, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_STOREMAP_TOKEN, &PyJit_StoreMap, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_STOREMAP_NO_DECREF_TOKEN, &PyJit_StoreMapNoDecRef, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_DICTUPDATE_TOKEN, &PyJit_DictUpdate, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_STORE_SUBSCR_OBJ, &PyJit_StoreSubscr, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_STORE_SUBSCR_OBJ_I, &PyJit_StoreSubscrIndex, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_STORE_SUBSCR_OBJ_I_HASH, &PyJit_StoreSubscrIndexHash, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_STORE_SUBSCR_DICT, &PyJit_StoreSubscrDict, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_STORE_SUBSCR_DICT_HASH, &PyJit_StoreSubscrDictHash, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_STORE_SUBSCR_LIST, &PyJit_StoreSubscrList, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_STORE_SUBSCR_LIST_I, &PyJit_StoreSubscrListIndex, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_DELETESUBSCR_TOKEN, &PyJit_DeleteSubscr, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_BUILD_DICT_FROM_TUPLES, &PyJit_BuildDictFromTuples, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_DICT_MERGE, &PyJit_DictMerge, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_PYDICT_NEWPRESIZED, &_PyDict_NewPresized, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_PYTUPLE_NEW, &PyJit_PyTuple_New, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_PYSET_NEW, &PySet_New, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_PYOBJECT_STR, &PyObject_Str, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_PYOBJECT_REPR, &PyObject_Repr, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_PYOBJECT_ASCII, &PyObject_ASCII, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_PYOBJECT_ISTRUE, &PyObject_IsTrue, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_PYITER_NEXT, &PyIter_Next, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_PYCELL_GET, &PyJit_CellGet, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_INT));
GLOBAL_METHOD(METHOD_PYCELL_SET_TOKEN, &PyJit_CellSet, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_INT));

GLOBAL_METHOD(METHOD_RICHCMP_TOKEN, &PyJit_RichCompare, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_INT));

GLOBAL_METHOD(METHOD_CONTAINS_TOKEN, &PyJit_Contains, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_NOTCONTAINS_TOKEN, &PyJit_NotContains, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_NEWFUNCTION_TOKEN, &PyJit_NewFunction, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_GETBUILDCLASS_TOKEN, &PyJit_BuildClass, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_PYSET_ADD, &PySet_Add, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_CALL_0_TOKEN, &Call0, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_CALL_1_TOKEN, &Call1, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_CALL_2_TOKEN, &Call2, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_CALL_3_TOKEN, &Call3, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_CALL_4_TOKEN, &Call4, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_CALL_5_TOKEN, &Call5, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_CALL_6_TOKEN, &Call6, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_CALL_7_TOKEN, &Call7, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_CALL_8_TOKEN, &Call8, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_CALL_9_TOKEN, &Call9, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_CALL_10_TOKEN, &Call10, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_CALLN_TOKEN, &PyJit_CallN, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_KWCALLN_TOKEN, &PyJit_KwCallN, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_VECTORCALL, &PyVectorcall_Call, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT) );
GLOBAL_METHOD(METHOD_OBJECTCALL, &PyObject_Call, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT) );

GLOBAL_METHOD(METHOD_STOREGLOBAL_TOKEN, &PyJit_StoreGlobal, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_DELETEGLOBAL_TOKEN, &PyJit_DeleteGlobal, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_LOADGLOBAL_TOKEN, &PyJit_LoadGlobal, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_LOADGLOBAL_HASH, &PyJit_LoadGlobalHash, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_LOADATTR_TOKEN, &PyJit_LoadAttr, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_GENERIC_GETATTR, &PyObject_GenericGetAttr, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_LOADATTR_HASH, &PyJit_LoadAttrHash, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_STOREATTR_TOKEN, &PyJit_StoreAttr, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_DELETEATTR_TOKEN, &PyJit_DeleteAttr, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_LOADNAME_TOKEN, &PyJit_LoadName, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_LOADNAME_HASH, &PyJit_LoadNameHash, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_STORENAME_TOKEN, &PyJit_StoreName, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_DELETENAME_TOKEN, &PyJit_DeleteName, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_GETITER_TOKEN, &PyJit_GetIter, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_ITERNEXT_TOKEN, &PyJit_IterNext, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_DECREF_TOKEN, &PyJit_DecRef, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_SET_CLOSURE, &PyJit_SetClosure, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_BUILD_SLICE, &PyJit_BuildSlice, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_UNARY_POSITIVE, &PyJit_UnaryPositive, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_UNARY_NEGATIVE, &PyJit_UnaryNegative, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_UNARY_NOT, &PyJit_UnaryNot, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_UNARY_NOT_INT, &PyJit_UnaryNot_Int, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_UNARY_INVERT, &PyJit_UnaryInvert, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_LIST_APPEND_TOKEN, &PyJit_ListAppend, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_SET_ADD_TOKEN, &PyJit_SetAdd, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_SETUPDATE_TOKEN, &PyJit_UpdateSet, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_MAP_ADD_TOKEN, &PyJit_MapAdd, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_INPLACE_POWER_TOKEN, &PyJit_InplacePower, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_INPLACE_MULTIPLY_TOKEN, &PyJit_InplaceMultiply, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_INPLACE_MATRIX_MULTIPLY_TOKEN, &PyJit_InplaceMatrixMultiply, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_INPLACE_TRUE_DIVIDE_TOKEN, &PyJit_InplaceTrueDivide, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_INPLACE_FLOOR_DIVIDE_TOKEN, &PyJit_InplaceFloorDivide, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_INPLACE_MODULO_TOKEN, &PyJit_InplaceModulo, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_INPLACE_ADD_TOKEN, &PyJit_InplaceAdd, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_INPLACE_SUBTRACT_TOKEN, &PyJit_InplaceSubtract, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_INPLACE_LSHIFT_TOKEN, &PyJit_InplaceLShift, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_INPLACE_RSHIFT_TOKEN, &PyJit_InplaceRShift, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_INPLACE_AND_TOKEN, &PyJit_InplaceAnd, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_INPLACE_XOR_TOKEN, &PyJit_InplaceXor, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_INPLACE_OR_TOKEN, &PyJit_InplaceOr, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_PRINT_EXPR_TOKEN, &PyJit_PrintExpr, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_LOAD_CLASSDEREF_TOKEN, &PyJit_LoadClassDeref, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_PREPARE_EXCEPTION, &PyJit_PrepareException, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT),
    Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_DO_RAISE, &PyJit_Raise, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_EH_TRACE, &PyJit_EhTrace, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_COMPARE_EXCEPTIONS, &PyJit_CompareExceptions, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_UNBOUND_LOCAL, &PyJit_UnboundLocal, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_PYERR_RESTORE, &PyJit_PyErrRestore, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_DEBUG_TRACE, &PyJit_DebugTrace, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_DEBUG_PTR, &PyJit_DebugPtr, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_DEBUG_TYPE, &PyJit_DebugType, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_DEBUG_PYOBJECT, &PyJit_DebugPyObject, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_PY_POPFRAME, &PyJit_PopFrame, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_PY_PUSHFRAME, &PyJit_PushFrame, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_UNWIND_EH, &PyJit_UnwindEh, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_PY_IMPORTNAME, &PyJit_ImportName, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_CALL_ARGS, &PyJit_CallArgs, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_CALL_KWARGS, &PyJit_CallKwArgs, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_PY_IMPORTFROM, &PyJit_ImportFrom, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_PY_IMPORTSTAR, &PyJit_ImportStar, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_IS, &PyJit_Is, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_ISNOT, &PyJit_IsNot, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_IS_BOOL, &PyJit_Is_Bool, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_ISNOT_BOOL, &PyJit_IsNot_Bool, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_FLOAT_POWER_TOKEN, static_cast<double(*)(double, double)>(pow), CORINFO_TYPE_DOUBLE, Parameter(CORINFO_TYPE_DOUBLE), Parameter(CORINFO_TYPE_DOUBLE));
GLOBAL_METHOD(METHOD_FLOAT_FLOOR_TOKEN, static_cast<double(*)(double)>(floor), CORINFO_TYPE_DOUBLE, Parameter(CORINFO_TYPE_DOUBLE));
GLOBAL_METHOD(METHOD_FLOAT_MODULUS_TOKEN, static_cast<double(*)(double, double)>(fmod), CORINFO_TYPE_DOUBLE, Parameter(CORINFO_TYPE_DOUBLE), Parameter(CORINFO_TYPE_DOUBLE));
GLOBAL_METHOD(METHOD_FLOAT_FROM_DOUBLE, PyFloat_FromDouble, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_DOUBLE));
GLOBAL_METHOD(METHOD_BOOL_FROM_LONG, PyBool_FromLong, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_INT));
GLOBAL_METHOD(METHOD_NUMBER_AS_SSIZET, PyNumber_AsSsize_t, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_PYERR_SETSTRING, PyErr_SetString, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_PYUNICODE_JOINARRAY, &PyJit_UnicodeJoinArray, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_FORMAT_VALUE, &PyJit_FormatValue, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_FORMAT_OBJECT, &PyJit_FormatObject, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_LOAD_METHOD, &PyJit_LoadMethod, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_METHCALL_0_TOKEN, &MethCall0, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_METHCALL_1_TOKEN, &MethCall1, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_METHCALL_2_TOKEN, &MethCall2, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_METHCALL_3_TOKEN, &MethCall3, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_METHCALL_4_TOKEN, &MethCall4, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_METHCALL_5_TOKEN, &MethCall5, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_METHCALL_6_TOKEN, &MethCall6, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_METHCALL_7_TOKEN, &MethCall7, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_METHCALL_8_TOKEN, &MethCall8, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_METHCALL_9_TOKEN, &MethCall9, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_METHCALL_10_TOKEN, &MethCall10, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_METHCALLN_TOKEN, &MethCallN, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_SETUP_ANNOTATIONS, &PyJit_SetupAnnotations, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT),);

GLOBAL_METHOD(METHOD_LOAD_ASSERTION_ERROR, &PyJit_LoadAssertionError, CORINFO_TYPE_NATIVEINT);

GLOBAL_METHOD(METHOD_DEALLOC_OBJECT, &_Py_Dealloc, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_TRACE_LINE, &PyJit_TraceLine, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_TRACE_FRAME_ENTRY, &PyJit_TraceFrameEntry, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), );
GLOBAL_METHOD(METHOD_TRACE_FRAME_EXIT, &PyJit_TraceFrameExit, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), );
GLOBAL_METHOD(METHOD_TRACE_EXCEPTION, &PyJit_TraceFrameException, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), );
GLOBAL_METHOD(METHOD_PROFILE_FRAME_ENTRY, &PyJit_ProfileFrameEntry, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), );
GLOBAL_METHOD(METHOD_PROFILE_FRAME_EXIT, &PyJit_ProfileFrameExit, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), );

GLOBAL_METHOD(METHOD_LOAD_CLOSURE, &PyJit_LoadClosure, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_INT));

GLOBAL_METHOD(METHOD_TRIPLE_BINARY_OP, &PyJitMath_TripleBinaryOp, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_INT), Parameter(CORINFO_TYPE_INT));
GLOBAL_METHOD(METHOD_PENDING_CALLS, &Py_MakePendingCalls, CORINFO_TYPE_INT, );

GLOBAL_METHOD(METHOD_PGC_PROBE, &capturePgcStackValue, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_INT));
GLOBAL_METHOD(METHOD_SEQUENCE_AS_LIST, &PySequence_List, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_LIST_ITEM_FROM_BACK, &PyJit_GetListItemReversed, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_GIL_ENSURE, &PyGILState_Ensure, CORINFO_TYPE_NATIVEINT);
GLOBAL_METHOD(METHOD_GIL_RELEASE, &PyGILState_Release, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT));
