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

#include <Python.h>
#include "pycomp.h"
#include "pyjit.h"
#include "unboxing.h"

using namespace std;


CCorJitHost g_jitHost;
BaseModule g_module;
ICorJitCompiler* g_jit;

PythonCompiler::PythonCompiler(PyCodeObject* code) : m_il(m_module = new UserModule(g_module),
                                                          CORINFO_TYPE_NATIVEINT,
                                                          std::vector<Parameter>{
                                                                  Parameter(CORINFO_TYPE_NATIVEINT),// PyjionJittedCode*
                                                                  Parameter(CORINFO_TYPE_NATIVEINT),// struct _frame*
                                                                  Parameter(CORINFO_TYPE_NATIVEINT),// PyThreadState*
                                                                  Parameter(CORINFO_TYPE_NATIVEINT),// PyjionCodeProfile*
                                                                  Parameter(CORINFO_TYPE_NATIVEINT),// PyTraceInfo
                                                          }) {
    m_code = code;
    m_lasti = m_il.define_local(Parameter(CORINFO_TYPE_NATIVEINT));
    m_compileDebug = g_pyjionSettings.debug;
}

void PythonCompiler::load_frame() {
    m_il.ld_arg(1);
}

void PythonCompiler::load_tstate() {
    m_il.ld_arg(2);
}

void PythonCompiler::load_profile() {
    m_il.ld_arg(3);
}

void PythonCompiler::load_trace_info() {
    m_il.ld_arg(4);
}

bool PythonCompiler::emit_push_frame() {
    if (OPT_ENABLED(InlineFramePushPop)) {
        load_tstate();
        LD_FIELDA(PyThreadState, frame);
        load_frame();
        m_il.st_ind_i();
        return true;
    } else {
        load_frame();
        m_il.emit_call(METHOD_PY_PUSHFRAME);
        return false;
    }
}

bool PythonCompiler::emit_pop_frame() {
    if (OPT_ENABLED(InlineFramePushPop)) {
        load_tstate();
        LD_FIELDA(PyThreadState, frame);

        load_frame();
        LD_FIELDI(PyFrameObject, f_back);

        m_il.st_ind_i();
        return true;
    } else {
        load_frame();
        m_il.emit_call(METHOD_PY_POPFRAME);
        return false;
    }
}

void PythonCompiler::emit_set_frame_state(PythonFrameState state) {
    load_frame();
    LD_FIELDA(PyFrameObject, f_state);
    m_il.ld_i4(state);
    m_il.st_ind_i4();
}

void PythonCompiler::emit_push_block(int32_t type, int32_t handler, int32_t level) {
    load_frame();
    m_il.ld_i4(type);
    m_il.ld_i4(type);
    m_il.ld_i4(type);
    m_il.emit_call(METHOD_BLOCK_PUSH);
}

void PythonCompiler::emit_pop_block() {
    load_frame();
    m_il.emit_call(METHOD_BLOCK_POP);
}

void PythonCompiler::emit_pop_except() {
    load_frame();
    m_il.emit_call(METHOD_POP_EXCEPT);
}


void PythonCompiler::emit_eh_trace() {
    load_frame();
    m_il.emit_call(METHOD_EH_TRACE);
}

void PythonCompiler::emit_lasti_init() {
    load_frame();
    LD_FIELDA(PyFrameObject, f_lasti);
    m_il.st_loc(m_lasti);
}

void PythonCompiler::emit_lasti_update(py_opindex index) {
    m_il.ld_loc(m_lasti);
    m_il.ld_u4(index / 2);
    m_il.st_ind_i4();
}

void PythonCompiler::emit_lasti() {
    m_il.ld_loc(m_lasti);
    m_il.ld_ind_i4();
}

void PythonCompiler::emit_store_in_frame_value_stack(uint32_t idx) {
    Local tmp = emit_define_local(LK_Pointer);
    emit_store_local(tmp);
    load_frame();
    LD_FIELDI(PyFrameObject, f_valuestack);
    if (idx > 0) {
        m_il.ld_i(idx * sizeof(size_t));
        m_il.add();
    }
    emit_load_and_free_local(tmp);
    m_il.st_ind_i();
}

void PythonCompiler::emit_load_from_frame_value_stack(uint32_t idx) {
    load_frame();
    LD_FIELDI(PyFrameObject, f_valuestack);
    if (idx > 0) {
        m_il.ld_i(idx * sizeof(size_t));
        m_il.add();
    }
    m_il.ld_ind_i();
}

void PythonCompiler::emit_dec_frame_stackdepth(uint32_t by) {
    load_frame();
    LD_FIELDA(PyFrameObject, f_stackdepth);
    m_il.dup();
    m_il.ld_ind_i();
    m_il.ld_u4(by);
    m_il.sub();
    m_il.st_ind_i();
}

void PythonCompiler::emit_set_frame_stackdepth(uint32_t to) {
    load_frame();
    LD_FIELDA(PyFrameObject, f_stackdepth);
    m_il.ld_u4(to);
    m_il.st_ind_i();
}

void PythonCompiler::load_local(py_oparg oparg) {
    load_frame();
    m_il.ld_i(offsetof(PyFrameObject, f_localsplus) + oparg * sizeof(size_t));
    m_il.add();
    m_il.ld_ind_i();
}

void PythonCompiler::emit_breakpoint() {
    // Emits a breakpoint in the IL. useful for debugging
    m_il.brk();
}

void PythonCompiler::emit_trace_line(Local lastInstr) {
    load_frame();
    emit_load_local(lastInstr);
    load_trace_info();
    m_il.emit_call(METHOD_TRACE_LINE);
}

void PythonCompiler::emit_trace_frame_entry() {
    load_frame();
    load_trace_info();
    m_il.emit_call(METHOD_TRACE_FRAME_ENTRY);
}

void PythonCompiler::emit_trace_frame_exit(Local returnValue) {
    load_frame();
    load_trace_info();
    emit_load_local(returnValue);
    m_il.emit_call(METHOD_TRACE_FRAME_EXIT);
}

void PythonCompiler::emit_profile_frame_entry() {
    load_frame();
    load_trace_info();

    m_il.emit_call(METHOD_PROFILE_FRAME_ENTRY);
}

void PythonCompiler::emit_profile_frame_exit(Local returnValue) {
    load_frame();
    load_trace_info();
    emit_load_local(returnValue);
    m_il.emit_call(METHOD_PROFILE_FRAME_EXIT);
}

void PythonCompiler::emit_trace_exception() {
    load_frame();
    load_trace_info();

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
    emit_sizet(by);
    m_il.sub();
    m_il.st_ind_i();
}

void PythonCompiler::decref(bool noopt) {
    /*
     * PyObject* is on the top of the stack
     * Should decrement obj->ob_refcnt
     * by either doing it inline, or calling PyJit_Decref
     */
    if (OPT_ENABLED(InlineDecref) && !noopt) {// obj
        Label done = emit_define_label();
        Label popAndGo = emit_define_label();
        m_il.dup();// obj, obj
        emit_branch(BranchFalse, popAndGo);

        m_il.dup();
        m_il.dup();                    // obj, obj, obj
        LD_FIELDA(PyObject, ob_refcnt);// obj, obj, refcnt
        m_il.dup();                    // obj, obj, refcnt, refcnt
        m_il.ld_ind_i();               // obj, obj, refcnt, *refcnt
        m_il.load_one();               // obj, obj, refcnt,  *refcnt, 1
        m_il.sub();                    // obj, obj, refcnt, (*refcnt - 1)
        m_il.st_ind_i();               // obj, obj
        LD_FIELDI(PyObject, ob_refcnt);// obj, refcnt
        m_il.load_null();              // obj, refcnt, 0
        emit_branch(BranchGreaterThan, popAndGo);

        m_il.emit_call(METHOD_DEALLOC_OBJECT);// _Py_Dealloc
        emit_branch(BranchAlways, done);

        emit_mark_label(popAndGo);
        emit_pop();

        emit_mark_label(done);
    } else {
        m_il.emit_call(METHOD_DECREF_TOKEN);
    }
}

void PythonCompiler::emit_unpack_tuple(py_oparg size, AbstractValueWithSources iterable) {
    Label passedGuard, failedGuard;
    if (iterable.Value->needsGuard()) {
        passedGuard = emit_define_label(), failedGuard = emit_define_label();
        m_il.dup();
        LD_FIELDI(PyObject, ob_type);
        emit_ptr(iterable.Value->pythonType());
        emit_branch(BranchEqual, passedGuard);
        emit_unpack_generic(size, iterable);
        emit_branch(BranchAlways, failedGuard);
        emit_mark_label(passedGuard);
    }

    Local t_value = emit_define_local(LK_NativeInt);
    Label raiseValueError = emit_define_label();
    Label returnValues = emit_define_label();
    py_oparg idx = size, idx2 = size;
    emit_store_local(t_value);

    emit_load_local(t_value);
    emit_tuple_length();
    emit_sizet(size);
    emit_branch(BranchNotEqual, raiseValueError);

    while (idx--) {
        emit_load_local(t_value);
        emit_tuple_load(idx);
        emit_dup();
        emit_incref();
    }
    emit_int(0);

    emit_branch(BranchAlways, returnValues);

    emit_mark_label(raiseValueError);

    while (idx2--) {
        emit_null();
    }
    emit_pyerr_setstring(PyExc_ValueError, "Cannot unpack tuple due to size mismatch");
    emit_int(-1);

    emit_mark_label(returnValues);
    emit_load_and_free_local(t_value);
    decref();

    if (iterable.Value->needsGuard()) {
        emit_mark_label(failedGuard);
    }
}

void PythonCompiler::emit_unpack_list(py_oparg size, AbstractValueWithSources iterable) {
    Label passedGuard, failedGuard;
    if (iterable.Value->needsGuard()) {
        passedGuard = emit_define_label(), failedGuard = emit_define_label();
        m_il.dup();
        LD_FIELDI(PyObject, ob_type);
        emit_ptr(iterable.Value->pythonType());
        emit_branch(BranchEqual, passedGuard);
        emit_unpack_generic(size, iterable);
        emit_branch(BranchAlways, failedGuard);
        emit_mark_label(passedGuard);
    }
    Local t_value = emit_define_local(LK_NativeInt);
    Label raiseValueError = emit_define_label();
    Label returnValues = emit_define_label();
    py_oparg idx = size, idx2 = size;

    emit_store_local(t_value);

    emit_load_local(t_value);
    emit_list_length();
    emit_sizet(size);
    emit_branch(BranchNotEqual, raiseValueError);

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

    if (iterable.Value->needsGuard()) {
        emit_mark_label(failedGuard);
    }
}

void PythonCompiler::emit_unpack_generic(py_oparg size, AbstractValueWithSources iterable) {
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
        emit_int(SIG_ITER_ERROR);
        emit_branch(BranchNotEqual, successOrStopIter);
        // Failure
        emit_int(1);
        emit_store_local(result);
        emit_branch(BranchAlways, endbranch);

        emit_mark_label(successOrStopIter);
        // Either success or received stopiter (0xff)
        m_il.dup();
        emit_ptr((void*) SIG_STOP_ITER);
        emit_branch(BranchNotEqual, endbranch);
        m_il.pop();
        emit_null();
        emit_pyerr_setstring(PyExc_ValueError, "Cannot unpack object due to size mismatch");
        emit_int(1);
        emit_store_local(result);

        emit_mark_label(endbranch);
        emit_store_local(iterated[idx]);
    }
    for (size_t i = 0; i < size; i++)
        emit_load_and_free_local(iterated[i]);
    emit_load_and_free_local(t_iter);
    decref();
    emit_free_local(t_object);
    emit_load_and_free_local(result);
}

void PythonCompiler::emit_unpack_sequence(py_oparg size, AbstractValueWithSources iterable) {
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

void PythonCompiler::fill_local_vector(vector<Local>& vec, size_t len) {
    for (size_t i = 0; i < len; i++)
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
        emit_int(SIG_ITER_ERROR);
        emit_branch(BranchNotEqual, successOrStopIter);
        // Failure
        emit_int(1);
        emit_store_local(result);
        emit_debug_msg("cannot unpack left");
        emit_branch(BranchAlways, endbranch);

        emit_mark_label(successOrStopIter);
        // Either success or received stopiter (0xff)
        m_il.dup();
        emit_ptr((void*) SIG_STOP_ITER);
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
    emit_sizet(rightSize);
    emit_branch(BranchLessThan, raiseValueError);

    while (j_idx--) {
        emit_load_local(resultList);
        emit_sizet(j_idx);
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
    for (size_t i = 0; i < rightSize; i++)
        emit_load_and_free_local(rightLocals[i]);
    emit_load_and_free_local(resultList);
    for (size_t i = 0; i < leftSize; i++)
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

void PythonCompiler::emit_load_fast(py_oparg local) {
    load_local(local);
}

CorInfoType PythonCompiler::to_clr_type(LocalKind kind) {
    switch (kind) {
        case LK_Float:
            return CORINFO_TYPE_DOUBLE;
        case LK_Int:
            return CORINFO_TYPE_LONG;
        case LK_Bool:
            return CORINFO_TYPE_BOOL;
        case LK_Pointer:
            return CORINFO_TYPE_PTR;
        case LK_NativeInt:
            return CORINFO_TYPE_NATIVEINT;
    }
    return CORINFO_TYPE_NATIVEINT;
}

void PythonCompiler::emit_store_fast(py_oparg local) {
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

void PythonCompiler::lift_n_to_second(uint16_t pos) {
    if (pos == 1)
        return;// already is second
    vector<Local> tmpLocals(pos - 1);

    auto top = m_il.define_local(Parameter(to_clr_type(LK_Pointer)));
    m_il.st_loc(top);

    // dump stack up to n
    for (size_t i = 0; i < pos - 1; i++) {
        auto loc = m_il.define_local(Parameter(to_clr_type(LK_Pointer)));
        tmpLocals[i] = loc;
        m_il.st_loc(loc);
    }

    // pop n
    auto n = m_il.define_local(Parameter(to_clr_type(LK_Pointer)));
    m_il.st_loc(n);

    // recover stack
    for (auto& loc : tmpLocals) {
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

void PythonCompiler::lift_n_to_third(uint16_t pos) {
    if (pos == 1)
        return;// already is third
    vector<Local> tmpLocals(pos - 2);

    auto top = m_il.define_local(Parameter(to_clr_type(LK_Pointer)));
    m_il.st_loc(top);

    auto second = m_il.define_local(Parameter(to_clr_type(LK_Pointer)));
    m_il.st_loc(second);

    // dump stack up to n
    for (size_t i = 0; i < pos - 2; i++) {
        auto loc = m_il.define_local(Parameter(to_clr_type(LK_Pointer)));
        tmpLocals[i] = loc;
        m_il.st_loc(loc);
    }

    // pop n
    auto n = m_il.define_local(Parameter(to_clr_type(LK_Pointer)));
    m_il.st_loc(n);

    // recover stack
    for (auto& loc : tmpLocals) {
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

void PythonCompiler::sink_top_to_n(uint16_t pos) {
    if (pos == 0)
        return;// already is at the correct position
    vector<Local> tmpLocals(pos);

    auto top = m_il.define_local(Parameter(to_clr_type(LK_Pointer)));
    m_il.st_loc(top);

    // dump stack up to n
    for (size_t i = 0; i < pos; i++) {
        auto loc = m_il.define_local(Parameter(to_clr_type(LK_Pointer)));
        tmpLocals[i] = loc;
        m_il.st_loc(loc);
    }

    // push n
    m_il.ld_loc(top);
    m_il.free_local(top);

    // recover stack
    for (auto& loc : tmpLocals) {
        m_il.ld_loc(loc);
        m_il.free_local(loc);
    }
}

void PythonCompiler::lift_n_to_top(uint16_t pos) {
    vector<Local> tmpLocals(pos);

    // dump stack up to n
    for (size_t i = 0; i < pos; i++) {
        auto loc = m_il.define_local(Parameter(to_clr_type(LK_Pointer)));
        tmpLocals[i] = loc;
        m_il.st_loc(loc);
    }

    // pop n
    auto n = m_il.define_local(Parameter(to_clr_type(LK_Pointer)));
    m_il.st_loc(n);


    // recover stack
    for (auto& loc : tmpLocals) {
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

void PythonCompiler::emit_new_list(py_oparg argCnt) {
    m_il.ld_i4(argCnt);
    m_il.emit_call(METHOD_PYLIST_NEW);
}

void PythonCompiler::emit_load_assertion_error() {
    m_il.emit_call(METHOD_LOAD_ASSERTION_ERROR);
}

void PythonCompiler::emit_list_store(py_oparg argCnt) {
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

    for (int32_t i = 0, arg = argCnt - 1; i < argCnt; i++, arg--) {
        // save the argument into a temporary...
        m_il.st_loc(valueTmp);

        // load the address of the list item...
        m_il.ld_loc(listItems);
        emit_sizet(arg * sizeof(size_t));
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

void PythonCompiler::emit_new_dict(py_oparg size) {
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

void PythonCompiler::emit_load_name_hashed(PyObject* name, Py_hash_t name_hash) {
    load_frame();
    m_il.ld_i(name);
    emit_sizet(name_hash);
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
        LD_FIELDI(PyObject, ob_type);
        emit_ptr(obj.Value->pythonType());
        emit_branch(BranchNotEqual, execute_guard);
        emit_load_local(objLocal);
        LD_FIELDI(PyObject, ob_type);
        LD_FIELDI(PyTypeObject, tp_getattro);
        emit_ptr((void*) obj.Value->pythonType()->tp_getattro);
        emit_branch(BranchNotEqual, execute_guard);
    }

    if (obj.Value->pythonType() != nullptr && obj.Value->pythonType()->tp_getattro) {
        // Often its just PyObject_GenericGetAttr to instead of recycling, use that.
        if (obj.Value->pythonType()->tp_getattro == PyObject_GenericGetAttr) {
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
                                                     (void*) obj.Value->pythonType()->tp_getattro,
                                                     "tp_getattro");
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
                                                (void*) obj.Value->pythonType()->tp_getattr,
                                                "tp_getattr");
        emit_load_local(objLocal);
        m_il.ld_i((void*) PyUnicode_AsUTF8((PyObject*) name));
        m_il.emit_call(getattr_token);
        emit_load_local(objLocal);
        decref();
    } else {
        emit_load_local(objLocal);
        m_il.ld_i(name);
        m_il.emit_call(METHOD_LOADATTR_TOKEN);
    }

    if (guard) {
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

void PythonCompiler::emit_load_global(PyObject* name, PyObject* last, uint64_t globals_ver, uint64_t builtins_ver) {
    if (last == nullptr) {
        // Nothing was found at compile time, just look it up now.
        load_frame();
        m_il.ld_i(name);
        m_il.emit_call(METHOD_LOADGLOBAL_TOKEN);
        return;
    }
    Label lookup = emit_define_label(), end = emit_define_label();
    // Compare frame->f_globals->ma_version_tag with version at compile-time
    load_frame();
    LD_FIELDI(PyFrameObject, f_globals);
    LD_FIELDI(PyDictObject, ma_version_tag);
    m_il.ld_i8(globals_ver);
    emit_branch(BranchNotEqual, lookup);
    // Compare frame->f_builtins->ma_version_tag with version at compile-time
    load_frame();
    LD_FIELDI(PyFrameObject, f_builtins);
    LD_FIELDI(PyDictObject, ma_version_tag);
    m_il.ld_i8(builtins_ver);
    emit_branch(BranchNotEqual, lookup);

    // Use cached version
    emit_ptr(last);
    emit_dup();
    emit_incref();

    emit_branch(BranchAlways, end);
    emit_mark_label(lookup);
    load_frame();
    m_il.ld_i(name);
    m_il.emit_call(METHOD_LOADGLOBAL_TOKEN);
    emit_mark_label(end);
}

void PythonCompiler::emit_delete_fast(py_oparg index) {
    load_local(index);
    load_frame();
    m_il.ld_i(offsetof(PyFrameObject, f_localsplus) + index * sizeof(size_t));
    m_il.add();
    m_il.load_null();
    m_il.st_ind_i();
    decref();
}

void PythonCompiler::emit_new_tuple(py_oparg size) {
    if (size == 0) {
        emit_ptr(g_emptyTuple);
        m_il.dup();
        // incref 0-tuple so it never gets freed
        emit_incref();
    } else {
        m_il.ld_i4(size);
        m_il.emit_call(METHOD_PYTUPLE_NEW);
    }
}

// Loads the specified index from a tuple that's already on the stack
void PythonCompiler::emit_tuple_load(py_oparg index) {
    emit_sizet(index * sizeof(size_t) + offsetof(PyTupleObject, ob_item));
    m_il.add();
    m_il.ld_ind_i();
}

void PythonCompiler::emit_tuple_length() {
    m_il.ld_i(offsetof(PyVarObject, ob_size));
    m_il.add();
    m_il.ld_ind_i();
}

void PythonCompiler::emit_list_load(py_oparg index) {
    LD_FIELDI(PyListObject, ob_item);
    if (index > 0) {
        emit_sizet(index * sizeof(size_t));
        m_il.add();
    }
    m_il.ld_ind_i();
}

void PythonCompiler::emit_list_length() {
    m_il.ld_i(offsetof(PyVarObject, ob_size));
    m_il.add();
    m_il.ld_ind_i();
}

void PythonCompiler::emit_tuple_store(py_oparg argCnt) {
    /// This function emits a tuple from the stack, only using borrowed references.
    auto valueTmp = m_il.define_local(Parameter(CORINFO_TYPE_NATIVEINT));
    auto tupleTmp = m_il.define_local(Parameter(CORINFO_TYPE_NATIVEINT));
    m_il.st_loc(tupleTmp);

    for (size_t i = 0, arg = argCnt - 1; i < argCnt; i++, arg--) {
        // save the argument into a temporary...
        m_il.st_loc(valueTmp);

        // load the address of the tuple item...
        m_il.ld_loc(tupleTmp);
        emit_sizet(arg * sizeof(size_t) + offsetof(PyTupleObject, ob_item));
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
    if (key.Sources != nullptr && key.Sources->hasConstValue()) {
        constIndex = true;
        constSource = dynamic_cast<ConstSource*>(key.Sources);
        hasValidIndex = (constSource->hasNumericValue() && constSource->getNumericValue() >= 0);
    }
    switch (container.Value->kind()) {
        case AVK_Dict:
            if (constIndex) {
                if (constSource->hasHashValue()) {
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
            if (constIndex) {
                if (hasValidIndex) {
                    m_il.ld_i8(constSource->getNumericValue());
                    m_il.emit_call(METHOD_STORE_SUBSCR_LIST_I);
                } else {
                    m_il.emit_call(METHOD_STORE_SUBSCR_LIST);
                }
            } else if (key.hasValue() && key.Value->kind() == AVK_Slice) {
                // TODO : Optimize storing a list subscript
                m_il.emit_call(METHOD_STORE_SUBSCR_OBJ);
            } else {
                m_il.emit_call(METHOD_STORE_SUBSCR_LIST);
            }
            break;
        default:
            if (constIndex) {
                if (hasValidIndex && constSource->hasHashValue()) {
                    m_il.ld_i8(constSource->getNumericValue());
                    m_il.ld_i8(constSource->getHash());
                    m_il.emit_call(METHOD_STORE_SUBSCR_OBJ_I_HASH);
                } else if (!hasValidIndex && constSource->hasHashValue()) {
                    m_il.ld_i8(constSource->getHash());
                    m_il.emit_call(METHOD_STORE_SUBSCR_DICT_HASH);
                } else if (hasValidIndex && !constSource->hasHashValue()) {
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

void PythonCompiler::emit_binary_subscr() {
    m_il.emit_call(METHOD_SUBSCR_OBJ);
}

void PythonCompiler::emit_binary_subscr(AbstractValueWithSources container, AbstractValueWithSources key) {
    bool constIndex = false;
    ConstSource* constSource = nullptr;
    bool hasValidIndex = false;

    if (key.hasSource() && key.Sources->hasConstValue()) {
        constIndex = true;
        constSource = dynamic_cast<ConstSource*>(key.Sources);
        hasValidIndex = (constSource->hasNumericValue() && constSource->getNumericValue() >= 0);
    }
    switch (container.Value->kind()) {
        case AVK_Dict:
            if (constIndex) {
                if (constSource->hasHashValue()) {
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
            } else if (key.hasValue() && key.Value->kind() == AVK_Slice) {
                // TODO : Further optimize getting a slice subscript when the values are dynamic
                m_il.emit_call(METHOD_SUBSCR_OBJ);
            } else {
                m_il.emit_call(METHOD_SUBSCR_LIST);
            }
            break;
        case AVK_Tuple:
            if (constIndex) {
                if (hasValidIndex) {
                    m_il.ld_i8(constSource->getNumericValue());
                    m_il.emit_call(METHOD_SUBSCR_TUPLE_I);
                } else {
                    m_il.emit_call(METHOD_SUBSCR_TUPLE);
                }
            } else if (key.hasValue() && key.Value->kind() == AVK_Slice) {
                m_il.emit_call(METHOD_SUBSCR_OBJ);
            } else {
                m_il.emit_call(METHOD_SUBSCR_TUPLE);
            }
            break;
        default:
            if (constIndex) {
                if (hasValidIndex && constSource->hasHashValue()) {
                    m_il.ld_i8(constSource->getNumericValue());
                    m_il.ld_i8(constSource->getHash());
                    m_il.emit_call(METHOD_SUBSCR_OBJ_I_HASH);
                } else if (!hasValidIndex && constSource->hasHashValue()) {
                    m_il.ld_i8(constSource->getHash());
                    m_il.emit_call(METHOD_SUBSCR_DICT_HASH);
                } else if (hasValidIndex && !constSource->hasHashValue()) {
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
            start_i = dynamic_cast<ConstSource*>(start.Sources)->getNumericValue();
            startIndex = true;
        }
    }
    if (stop.hasSource() && stop.Sources->hasConstValue()) {
        if (stop.Value->kind() == AVK_None) {
            stop_i = PY_SSIZE_T_MAX;
            stopIndex = true;
        } else if (stop.Value->kind() == AVK_Integer) {
            stop_i = dynamic_cast<ConstSource*>(stop.Sources)->getNumericValue();
            stopIndex = true;
        }
    }
    switch (container.Value->kind()) {
        case AVK_List:
            if (startIndex && stopIndex) {
                decref();
                decref();// will also pop the values
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
            start_i = dynamic_cast<ConstSource*>(start.Sources)->getNumericValue();
            startIndex = true;
        }
    }
    if (stop.hasSource() && stop.Sources->hasConstValue()) {
        if (stop.Value->kind() == AVK_None) {
            stop_i = PY_SSIZE_T_MAX;
            stopIndex = true;
        } else if (stop.Value->kind() == AVK_Integer) {
            stop_i = dynamic_cast<ConstSource*>(stop.Sources)->getNumericValue();
            stopIndex = true;
        }
    }
    if (step.hasSource() && step.Sources->hasConstValue()) {
        if (step.Value->kind() == AVK_None) {
            step_i = 1;
            stepIndex = true;
        } else if (step.Value->kind() == AVK_Integer) {
            step_i = dynamic_cast<ConstSource*>(step.Sources)->getNumericValue();
            stepIndex = true;
        }
    }
    switch (container.Value->kind()) {
        case AVK_List:
            if (start_i == PY_SSIZE_T_MIN && stop_i == PY_SSIZE_T_MAX && step_i == -1) {
                m_il.pop();
                m_il.pop();
                m_il.pop();// NB: Don't bother decref'ing None or -1 since they're permanent values anyway
                m_il.emit_call(METHOD_SUBSCR_LIST_SLICE_REVERSED);
                return true;
            } else if (startIndex && stopIndex && stepIndex) {
                decref();
                decref();
                decref();// will also pop the values
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

void PythonCompiler::emit_unary_not() {
    m_il.emit_call(METHOD_UNARY_NOT);
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

Local PythonCompiler::emit_define_local(AbstractValueKind kind) {
    switch (kind) {
        case AVK_Bool:
            return m_il.define_local(Parameter(CORINFO_TYPE_INT));
        case AVK_Float:
            return m_il.define_local(Parameter(CORINFO_TYPE_DOUBLE));
        case AVK_Integer:
            return m_il.define_local(Parameter(CORINFO_TYPE_LONG));
        default:
            return m_il.define_local(Parameter(CORINFO_TYPE_NATIVEINT));
    }
}

Local PythonCompiler::emit_define_local(LocalKind kind) {
    return m_il.define_local(Parameter(to_clr_type(kind)));
}

Local PythonCompiler::emit_define_local(bool cache) {
    if (cache) {
        return m_il.define_local(Parameter(CORINFO_TYPE_NATIVEINT));
    } else {
        return m_il.define_local_no_cache(Parameter(CORINFO_TYPE_NATIVEINT));
    }
}

void PythonCompiler::emit_call_args() {
    m_il.emit_call(METHOD_CALL_ARGS);
}

void PythonCompiler::emit_call_kwargs() {
    m_il.emit_call(METHOD_CALL_KWARGS);
}

bool PythonCompiler::emit_call_function(py_oparg argCnt) {
    switch (argCnt) {
        case 0:
            load_trace_info();
            m_il.emit_call(METHOD_CALL_0_TOKEN);
            return true;
        case 1:
            load_trace_info();
            m_il.emit_call(METHOD_CALL_1_TOKEN);
            return true;
        case 2:
            load_trace_info();
            m_il.emit_call(METHOD_CALL_2_TOKEN);
            return true;
        case 3:
            load_trace_info();
            m_il.emit_call(METHOD_CALL_3_TOKEN);
            return true;
        case 4:
            load_trace_info();
            m_il.emit_call(METHOD_CALL_4_TOKEN);
            return true;
        case 5:
            load_trace_info();
            m_il.emit_call(METHOD_CALL_5_TOKEN);
            return true;
        case 6:
            load_trace_info();
            m_il.emit_call(METHOD_CALL_6_TOKEN);
            return true;
        case 7:
            load_trace_info();
            m_il.emit_call(METHOD_CALL_7_TOKEN);
            return true;
        case 8:
            load_trace_info();
            m_il.emit_call(METHOD_CALL_8_TOKEN);
            return true;
        case 9:
            load_trace_info();
            m_il.emit_call(METHOD_CALL_9_TOKEN);
            return true;
        case 10:
            load_trace_info();
            m_il.emit_call(METHOD_CALL_10_TOKEN);
            return true;
        default:
            return false;
    }
    return false;
}

bool PythonCompiler::emit_method_call(py_oparg argCnt) {
    switch (argCnt) {
        case 0:
            load_trace_info();
            m_il.emit_call(METHOD_METHCALL_0_TOKEN);
            return true;
        case 1:
            load_trace_info();
            m_il.emit_call(METHOD_METHCALL_1_TOKEN);
            return true;
        case 2:
            load_trace_info();
            m_il.emit_call(METHOD_METHCALL_2_TOKEN);
            return true;
        case 3:
            load_trace_info();
            m_il.emit_call(METHOD_METHCALL_3_TOKEN);
            return true;
        case 4:
            load_trace_info();
            m_il.emit_call(METHOD_METHCALL_4_TOKEN);
            return true;
        case 5:
            load_trace_info();
            m_il.emit_call(METHOD_METHCALL_5_TOKEN);
            return true;
        case 6:
            load_trace_info();
            m_il.emit_call(METHOD_METHCALL_6_TOKEN);
            return true;
        case 7:
            load_trace_info();
            m_il.emit_call(METHOD_METHCALL_7_TOKEN);
            return true;
        case 8:
            load_trace_info();
            m_il.emit_call(METHOD_METHCALL_8_TOKEN);
            return true;
        case 9:
            load_trace_info();
            m_il.emit_call(METHOD_METHCALL_9_TOKEN);
            return true;
        case 10:
            load_trace_info();
            m_il.emit_call(METHOD_METHCALL_10_TOKEN);
            return true;
        default:
            return false;
    }
    return false;
}

void PythonCompiler::emit_method_call_n() {
    load_trace_info();
    m_il.emit_call(METHOD_METHCALLN_TOKEN);
}

void PythonCompiler::emit_call_with_tuple() {
    load_trace_info();
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

void PythonCompiler::emit_fetch_err() {
    Local PrevExc = emit_define_local(LK_Pointer), PrevExcVal = emit_define_local(LK_Pointer), PrevTraceback = emit_define_local(LK_Pointer);
    // The previous traceback and exception values if we're handling a finally block.
    // We store these in locals and keep only the exception type on the stack so that
    // we don't enter the finally handler with multiple stack depths.
    Local Exc = emit_define_local(LK_Pointer), Traceback = emit_define_local(LK_Pointer), ExcVal = emit_define_local(LK_Pointer);
    emit_load_local_addr(Exc);
    emit_load_local_addr(ExcVal);
    emit_load_local_addr(Traceback);
    emit_load_local_addr(PrevExc);
    emit_load_local_addr(PrevExcVal);
    emit_load_local_addr(PrevTraceback);

    m_il.emit_call(METHOD_HANDLE_EXCEPTION);
    emit_load_and_free_local(PrevTraceback);
    emit_load_and_free_local(PrevExcVal);
    emit_load_and_free_local(PrevExc);
    emit_load_and_free_local(Traceback);
    emit_load_and_free_local(ExcVal);
    emit_load_and_free_local(Exc);
}

void PythonCompiler::emit_compare_exceptions() {
    m_il.emit_call(METHOD_COMPARE_EXCEPTIONS);
}

void PythonCompiler::emit_pyerr_clear() {
    m_il.emit_call(METHOD_PYERR_CLEAR);
}

void PythonCompiler::emit_pyerr_setstring(void* exception, const char* msg) {
    emit_ptr(exception);
    emit_ptr((void*) msg);
    m_il.emit_call(METHOD_PYERR_SETSTRING);
}

void PythonCompiler::emit_unwind_eh(Local prevExc, Local prevExcVal, Local prevTraceback) {
    m_il.ld_loc(prevExc);
    m_il.ld_loc(prevExcVal);
    m_il.ld_loc(prevTraceback);
    m_il.emit_call(METHOD_UNWIND_EH);
}

void PythonCompiler::emit_int(int value) {
    m_il.ld_i4(value);
}

void PythonCompiler::emit_sizet(size_t value) {
    m_il.ld_i((void*) value);
}

void PythonCompiler::emit_long_long(long long value) {
    m_il.ld_i8(value);
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

void PythonCompiler::emit_load_deref(py_oparg index) {
    load_frame();
    m_il.ld_i4(index);
    m_il.emit_call(METHOD_PYCELL_GET);
}

void PythonCompiler::emit_store_deref(py_oparg index) {
    load_frame();
    m_il.ld_i4(index);
    m_il.emit_call(METHOD_PYCELL_SET_TOKEN);
}

void PythonCompiler::emit_delete_deref(py_oparg index) {
    m_il.load_null();
    load_frame();
    m_il.ld_i4(index);
    m_il.emit_call(METHOD_PYCELL_SET_TOKEN);
}

void PythonCompiler::emit_load_closure(py_oparg index) {
    load_frame();
    m_il.ld_i4(index);
    m_il.emit_call(METHOD_LOAD_CLOSURE);
}

void PythonCompiler::emit_load_classderef(py_oparg index) {
    load_frame();
    m_il.ld_i4(index);
    m_il.emit_call(METHOD_LOAD_CLASSDEREF_TOKEN);
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
    m_il.emit_call(METHOD_DO_RAISE);
}

void PythonCompiler::emit_print_expr() {
    m_il.emit_call(METHOD_PRINT_EXPR_TOKEN);
}

void PythonCompiler::emit_dict_update() {
    m_il.emit_call(METHOD_DICTUPDATE_TOKEN);
}

void PythonCompiler::emit_getiter() {
    m_il.emit_call(METHOD_GETITER_TOKEN);
}

void PythonCompiler::emit_getiter_unboxed() {
    m_il.emit_call(METHOD_GET_UNBOXED_ITER);
}

Label PythonCompiler::emit_define_label() {
    return m_il.define_label();
}

void PythonCompiler::emit_inc_local(Local local, size_t value) {
    emit_sizet(value);
    emit_load_local(local);
    m_il.add();
    emit_store_local(local);
}

void PythonCompiler::emit_dec_local(Local local, size_t value) {
    emit_load_local(local);
    emit_sizet(value);
    m_il.sub();
    emit_store_local(local);
}

void PythonCompiler::emit_ret() {
    m_il.ret();
}

void PythonCompiler::emit_mark_label(Label label) {
    m_il.mark_label(label);
}

void PythonCompiler::emit_for_next() {
    m_il.emit_call(METHOD_FORITER);
}

void PythonCompiler::emit_for_next_unboxed() {
    m_il.emit_call(METHOD_FORITER_UNBOXED);
}

void PythonCompiler::emit_debug_msg(const char* msg) {
#ifdef DEBUG_VERBOSE
    m_il.ld_i((void*) msg);
    m_il.emit_call(METHOD_DEBUG_TRACE);
#endif
}

void PythonCompiler::emit_debug_pyobject() {
    m_il.emit_call(METHOD_DEBUG_PYOBJECT);
}

void PythonCompiler::emit_debug_fault(const char* msg, const char* context, py_opindex index) {
#ifdef DEBUG_VERBOSE
    m_il.ld_i((void*) msg);
    m_il.ld_i((void*) context);
    m_il.ld_i4(index);
    load_frame();
    m_il.emit_call(METHOD_DEBUG_FAULT);
#endif
}

LocalKind PythonCompiler::emit_binary_float(uint16_t opcode) {
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
            m_il.sub();
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
        default:
            throw UnexpectedValueException();
    }
    return LK_Float;
}
LocalKind PythonCompiler::emit_binary_int(uint16_t opcode) {
    switch (opcode) {
        case BINARY_ADD:
        case INPLACE_ADD:
            m_il.add();
            return LK_Int;
        case INPLACE_TRUE_DIVIDE:
        case BINARY_TRUE_DIVIDE:
            m_il.emit_call(METHOD_INT_TRUE_DIVIDE);
            return LK_Float;
        case INPLACE_MODULO:
        case BINARY_MODULO:
            m_il.emit_call(METHOD_INT_MOD);
            return LK_Int;
        case INPLACE_MULTIPLY:
        case BINARY_MULTIPLY:
            m_il.mul();
            return LK_Int;
        case INPLACE_SUBTRACT:
        case BINARY_SUBTRACT:
            m_il.sub();
            return LK_Int;
        case BINARY_POWER:
        case INPLACE_POWER:
            m_il.emit_call(METHOD_INT_POWER);
            return LK_Int;
        case BINARY_FLOOR_DIVIDE:
        case INPLACE_FLOOR_DIVIDE:
            m_il.emit_call(METHOD_INT_FLOOR_DIVIDE);
            return LK_Int;
        case BINARY_LSHIFT:
            m_il.lshift();
            return LK_Int;
        case BINARY_RSHIFT:
            m_il.rshift();
            return LK_Int;
        case BINARY_AND:
            m_il.bitwise_and();
            return LK_Int;
        case BINARY_OR:
            m_il.bitwise_or();
            return LK_Int;
        case BINARY_XOR:
            m_il.bitwise_xor();
            return LK_Int;
        default:
            throw UnexpectedValueException();
    }
    return LK_Int;
}

void PythonCompiler::emit_is(bool isNot) {
    m_il.emit_call(isNot ? METHOD_ISNOT : METHOD_IS);
}

void PythonCompiler::emit_is(bool isNot, AbstractValueWithSources lhs, AbstractValueWithSources rhs) {
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
}

void PythonCompiler::emit_in() {
    m_il.emit_call(METHOD_CONTAINS_TOKEN);
}


void PythonCompiler::emit_not_in() {
    m_il.emit_call(METHOD_NOTCONTAINS_TOKEN);
}

void PythonCompiler::emit_compare_object(uint16_t compareType) {
    m_il.ld_i4(compareType);
    m_il.emit_call(METHOD_RICHCMP_TOKEN);
}

void PythonCompiler::emit_compare_known_object(uint16_t compareType, AbstractValueWithSources lhs, AbstractValueWithSources rhs) {
    // OPT-3 Optimize the comparison of an intern'ed const integer with an integer to an IS_OP expression.
    if ((lhs.Value->isIntern() && rhs.Value->kind() == AVK_Integer) ||
        (rhs.Value->isIntern() && lhs.Value->kind() == AVK_Integer)) {
        switch (compareType) {// NOLINT(hicpp-multiway-paths-covered)
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

void PythonCompiler::emit_compare_floats(uint16_t compareType) {
    switch (compareType) {
        case Py_EQ:
            m_il.compare_eq();
            break;
        case Py_NE:
            m_il.compare_ne();
            break;
        case Py_GE:
            m_il.compare_ge_float();
            break;
        case Py_LE:
            m_il.compare_le_float();
            break;
        case Py_LT:
            m_il.compare_lt();
            break;
        case Py_GT:
            m_il.compare_gt();
            break;
        default:
            throw UnexpectedValueException();
    };
}

void PythonCompiler::emit_compare_ints(uint16_t compareType) {
    switch (compareType) {
        case Py_EQ:
            m_il.compare_eq();
            break;
        case Py_NE:
            m_il.compare_ne();
            break;
        case Py_GE:
            m_il.compare_ge();
            break;
        case Py_LE:
            m_il.compare_le();
            break;
        case Py_LT:
            m_il.compare_lt();
            break;
        case Py_GT:
            m_il.compare_gt();
            break;
        default:
            throw UnexpectedValueException();
    };
}

void PythonCompiler::emit_load_method(void* name) {
    Local method = emit_define_local(LK_Pointer), self = emit_define_local(LK_Pointer);
    Local result = emit_define_local(LK_Int);
    m_il.ld_i(name);
    emit_load_local_addr(method);
    emit_load_local_addr(self);
    m_il.emit_call(METHOD_LOAD_METHOD);
    emit_store_local(result);
    emit_load_and_free_local(self);
    emit_load_and_free_local(method);
    emit_load_and_free_local(result);
}

void PythonCompiler::emit_init_instr_counter() {
    m_instrCount = emit_define_local(LK_Int);
    m_il.load_null();
    emit_store_local(m_instrCount);
}

void PythonCompiler::emit_pending_calls() {
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
    m_il.pop();// TODO : Handle error from Py_MakePendingCalls?
    emit_mark_label(skipPending);
}

void PythonCompiler::emit_builtin_method(PyObject* name, AbstractValue* typeValue) {
    auto pyType = typeValue->pythonType();

    if (pyType == nullptr || typeValue->kind() == AVK_Type) {
        emit_load_method(name);// Can't inline this type of method
        return;
    }

    auto meth = _PyType_Lookup(pyType, name);

    if (meth == nullptr || !PyType_HasFeature(Py_TYPE(meth), Py_TPFLAGS_METHOD_DESCRIPTOR)) {
        emit_load_method(name);// Can't inline this type of method
        return;
    }
    Label guard_pass = emit_define_label(), guard_fail = emit_define_label();
    if (typeValue->needsGuard()){
        m_il.dup();
        LD_FIELDI(PyObject, ob_type);
        emit_ptr(pyType);
        emit_branch(BranchNotEqual, guard_fail);
    }
    // Use cached method
    emit_ptr(meth);
    emit_ptr(meth);
    emit_incref();
    emit_int(0);
    if (typeValue->needsGuard()){
        emit_branch(BranchAlways, guard_pass);
        emit_mark_label(guard_fail);
        emit_load_method(name);
        emit_mark_label(guard_pass);
    }
}

void PythonCompiler::emit_call_function_inline(py_oparg n_args, AbstractValueWithSources func) {
    auto functionType = func.Value->pythonType();
    PyObject* functionObject = nullptr;
    Local argumentLocal = emit_define_local(LK_Pointer),
          functionLocal = emit_define_local(LK_Pointer),
          gstate = emit_define_local(LK_Pointer);
    Label fallback = emit_define_label(), pass = emit_define_label();

    m_il.emit_call(METHOD_GIL_ENSURE);
    emit_store_local(gstate);

    if (func.Sources->isBuiltin()) {
        auto builtin = reinterpret_cast<BuiltinSource*>(func.Sources);
        functionType = builtin->getValue()->ob_type;
        functionObject = builtin->getValue();
    }
    if (func.Value->needsGuard()) {
        auto vol = reinterpret_cast<VolatileValue*>(func.Value);
        functionObject = vol->lastValue();
    }
    emit_new_tuple(n_args);
    if (n_args != 0) {
        emit_tuple_store(n_args);
    }
    emit_store_local(argumentLocal);
    emit_store_local(functionLocal);

    if (functionType == &PyFunction_Type) {
        // Send all function calls through vectorcall_call
        emit_load_local(functionLocal);
        emit_load_local(argumentLocal);
        emit_null();// kwargs
        if (func.Value->needsGuard()) {
            emit_load_local(functionLocal);// Check it hasn't been swapped for something else.
            LD_FIELDI(PyObject, ob_type);
            emit_ptr(functionType);
            emit_branch(BranchNotEqual, fallback);
            m_il.emit_call(METHOD_VECTORCALL);
            emit_branch(BranchAlways, pass);
            emit_mark_label(fallback);
            m_il.emit_call(METHOD_OBJECTCALL);
            emit_mark_label(pass);
        } else {
            m_il.emit_call(METHOD_VECTORCALL);
        }
    } else if (func.Sources->isBuiltin() &&
               functionType == &PyType_Type &&
               functionObject != nullptr &&
               (PyTypeObject*) functionObject != &PyType_Type) {
        // builtin Type call
        auto tp_new_token = g_module.AddMethod(CORINFO_TYPE_NATIVEINT,
                                               vector<Parameter>{
                                                       Parameter(CORINFO_TYPE_NATIVEINT),
                                                       Parameter(CORINFO_TYPE_NATIVEINT),
                                                       Parameter(CORINFO_TYPE_NATIVEINT)},
                                               (void*) ((PyTypeObject*) functionObject)->tp_new, "tp_new");

        emit_load_local(functionLocal);
        emit_load_local(argumentLocal);
        emit_null();// kwargs
        m_il.emit_call(tp_new_token);
        if (((PyTypeObject*) functionObject)->tp_init != nullptr) {
            Local resultLocal = emit_define_local(LK_Pointer);
            Label good = emit_define_label(), returnLabel = emit_define_label();

            emit_dup();
            emit_branch(BranchFalse, returnLabel);

            emit_store_local(resultLocal);
            // has __init__
            auto tp_init_token = g_module.AddMethod(CORINFO_TYPE_NATIVEINT,
                                                    vector<Parameter>{
                                                            Parameter(CORINFO_TYPE_NATIVEINT),
                                                            Parameter(CORINFO_TYPE_NATIVEINT),
                                                            Parameter(CORINFO_TYPE_NATIVEINT)},
                                                    (void*) ((PyTypeObject*) functionObject)->tp_init, "tp_init");
            emit_load_local(resultLocal);
            emit_load_local(argumentLocal);
            emit_null();// kwargs
            m_il.emit_call(tp_init_token);

            emit_int(0);
            emit_branch(BranchEqual, good);

            // Bad
            emit_load_local(resultLocal);
            decref();
            emit_null();
            emit_branch(BranchAlways, returnLabel);

            emit_mark_label(good);
            // Good, return new object
            emit_load_and_free_local(resultLocal);
            emit_mark_label(returnLabel);
        }

    } else if (functionType == &PyCFunction_Type && functionObject != nullptr) {
        // Is a CFunction...
        int flags = PyCFunction_GET_FLAGS(functionObject);
        if (!(flags & METH_VARARGS)) {
            emit_load_local(functionLocal);
            emit_load_local(argumentLocal);
            /* If this is not a METH_VARARGS function, delegate to vectorcall */
            emit_null();// kwargs is always null
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
            PyObject* self = PyCFunction_GET_SELF(functionObject);
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
                                                          Parameter(CORINFO_TYPE_NATIVEINT)},// kwargs
                                                  (void*) meth, "method_call");
            } else {
                builtinToken = g_module.AddMethod(CORINFO_TYPE_NATIVEINT,
                                                  vector<Parameter>{
                                                          Parameter(CORINFO_TYPE_NATIVEINT), // Self
                                                          Parameter(CORINFO_TYPE_NATIVEINT)},// Args-tuple
                                                  (void*) meth, "method_call");
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
    } else {
        // General object call
        emit_load_local(functionLocal);
        emit_load_local(argumentLocal);
        emit_null();// kwargs
        m_il.emit_call(METHOD_OBJECTCALL);
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
    auto* jitInfo = new CorJitInfo(PyUnicode_AsUTF8(m_code->co_filename), PyUnicode_AsUTF8(m_code->co_name), m_module, m_compileDebug);
    auto addr = m_il.compile(jitInfo, g_jit, m_code->co_stacksize + 100).m_addr;
    if (addr == nullptr) {
#ifdef REPORT_CLR_FAULTS
        printf("Compiling failed %s from %s line %d\r\n",
               PyUnicode_AsUTF8(m_code->co_name),
               PyUnicode_AsUTF8(m_code->co_filename),
               m_code->co_firstlineno);
#endif
        delete jitInfo;
        return nullptr;
    }
    return jitInfo;
}

void PythonCompiler::mark_sequence_point(size_t idx) {
    m_il.mark_sequence_point(idx);
}

void PythonCompiler::emit_pgc_profile_capture(Local value, size_t ipos, size_t istack) {
    load_profile();
    emit_load_local(value);
    emit_sizet(ipos);
    emit_sizet(istack);
    m_il.emit_call(METHOD_PGC_PROBE);
}

LocalKind PythonCompiler::emit_unboxed_binary_subscr(AbstractValueWithSources left, AbstractValueWithSources right){
    // This is the only supported scenario right now.
    if (left.Value->kind() != AVK_Bytearray && right.Value->kind() != AVK_Integer)
        throw UnexpectedValueException();

    Local index = emit_define_local(LK_Int), array = emit_define_local(LK_Pointer);
    Label overflow = emit_define_label(), done = emit_define_label();
    emit_store_local(index);
    emit_store_local(array);

    emit_load_local(index);
    emit_load_local(array);
    emit_list_length();
    emit_branch(BranchGreaterThanEqual, overflow);
    emit_load_local(index);
    m_il.ld_i(0);
    emit_branch(BranchLessThan, overflow);

        emit_load_local(array);
        LD_FIELDI(PyByteArrayObject, ob_start);
        emit_load_local(index);
        m_il.add();
        m_il.ld_ind_u1();
        m_il.conv_i();
        emit_branch(BranchAlways, done);

    emit_mark_label(overflow);

        emit_nan_long();

    emit_mark_label(done);
    emit_free_local(index);
    emit_load_and_free_local(array);
    decref();

    return LK_Int;
}

void PythonCompiler::emit_box(AbstractValueKind kind) {
    switch (kind) {
        case AVK_Float:
            m_il.emit_call(METHOD_FLOAT_FROM_DOUBLE);
            break;
        case AVK_Bool:
            m_il.emit_call(METHOD_BOOL_FROM_LONG);
            break;
        case AVK_Integer:
            m_il.emit_call(METHOD_PYLONG_FROM_LONGLONG);
            break;
        default:
            throw UnexpectedValueException();
    }
};
void PythonCompiler::emit_compare_unboxed(uint16_t compareType, AbstractValueWithSources left, AbstractValueWithSources right) {
#ifdef DEBUG
    assert(supportsEscaping(left.Value->kind()) && supportsEscaping(right.Value->kind()));
#endif
    AbstractValueKind leftKind = left.Value->kind();
    AbstractValueKind rightKind = right.Value->kind();

    // Treat bools as integers
    if (leftKind == AVK_Bool)
        leftKind = AVK_Integer;
    if (rightKind == AVK_Bool)
        rightKind = AVK_Integer;

    if (leftKind == AVK_Float && rightKind == AVK_Float) {
        return emit_compare_floats(compareType);
    } else if (leftKind == AVK_Integer && rightKind == AVK_Integer) {
        return emit_compare_ints(compareType);
    } else if (leftKind == AVK_Integer && rightKind == AVK_Float) {
        Local right_l = emit_define_local(LK_Float);
        emit_store_local(right_l);
        m_il.conv_r8();
        emit_load_and_free_local(right_l);
        return emit_compare_floats(compareType);
    } else if (leftKind == AVK_Float && rightKind == AVK_Integer) {
        m_il.conv_r8();
        return emit_compare_floats(compareType);
    } else {
        throw UnexpectedValueException();
    }
}

void PythonCompiler::emit_guard_exception(const char* expected) {
    m_il.ld_i((void*) expected);
    m_il.emit_call(METHOD_PGC_GUARD_EXCEPTION);
}

void PythonCompiler::emit_unbox(AbstractValueKind kind, bool guard, Local success) {
#ifdef DEBUG
    assert(supportsEscaping(kind));
#endif
    switch (kind) {
        case AVK_Float: {
            Local lcl = emit_define_local(LK_Pointer);
            Label guard_pass = emit_define_label();
            Label guard_fail = emit_define_label();
            emit_store_local(lcl);
            if (guard) {
                emit_load_local(lcl);
                LD_FIELDI(PyObject, ob_type);
                emit_ptr(&PyFloat_Type);
                emit_branch(BranchNotEqual, guard_fail);
            }

            emit_load_local(lcl);
            m_il.ld_i(offsetof(PyFloatObject, ob_fval));
            m_il.add();
            m_il.ld_ind_r8();
            emit_load_local(lcl);
            decref();

            if (guard) {
                emit_branch(BranchAlways, guard_pass);
                emit_mark_label(guard_fail);
                emit_int(1);
                emit_store_local(success);
                emit_load_local(lcl);
                emit_guard_exception("float");
                emit_nan();// keep the stack effect equivalent, this value is never used.
                emit_mark_label(guard_pass);
            }
            emit_free_local(lcl);
            break;
        }
        case AVK_Integer: {
            Local lcl = emit_define_local(LK_Pointer);
            emit_store_local(lcl);
            emit_load_local(lcl);
            emit_load_local_addr(success);
            m_il.emit_call(METHOD_PYLONG_AS_LONGLONG);
            emit_load_local(lcl);
            decref();
            emit_free_local(lcl);
            break;
        }
        case AVK_Bool: {
            if (guard){
                emit_load_local_addr(success);
                m_il.emit_call(METHOD_UNBOX_BOOL);
            } else {
                Local lcl = emit_define_local(LK_Pointer);
                Label isFalse = emit_define_label();
                Label decref_out = emit_define_label();
                emit_store_local(lcl);
                emit_load_local(lcl);
                emit_ptr(Py_True);
                m_il.compare_eq();
                emit_load_local(lcl);
                decref();
                emit_free_local(lcl);
            }
            break;
        }
        default:
            throw UnexpectedValueException();
    }
};

void PythonCompiler::emit_unboxed_unary_not(AbstractValueWithSources val) {
#ifdef DEBUG
    assert(supportsEscaping(val.Value->kind()));
#endif
    switch (val.Value->kind()){
        case AVK_Integer:
        case AVK_Bool:
            m_il.ld_i4(0);
            m_il.compare_eq();
            break;
        case AVK_Float:
            m_il.ld_r8(0.0);
            m_il.compare_eq();
            break;
        default:
            throw UnexpectedValueException();
    }
}

void PythonCompiler::emit_unboxed_unary_positive(AbstractValueWithSources val) {
#ifdef DEBUG
    assert(supportsEscaping(val.Value->kind()));
#endif
    switch (val.Value->kind()){
        case AVK_Bool:
            break; // Do nothing
        case AVK_Integer: {
            Local loc = emit_define_local(LK_Int), mask = emit_define_local(LK_Int);
            emit_store_local(loc);
            emit_load_local(loc);
            emit_int(63);
            m_il.rshift();
            emit_store_local(mask);

            emit_load_local(loc);
            emit_load_local(mask);
            m_il.add();
            emit_load_local(mask);
            m_il.bitwise_xor();
            emit_free_local(loc);
            emit_free_local(mask);
        }
            break;
        case AVK_Float: {
            // TODO : Write a more efficient branch-less version
            Label is_positive = emit_define_label();
            m_il.dup();
            m_il.ld_r8(0.0);
            emit_branch(BranchGreaterThanEqual, is_positive);
            m_il.neg();
            emit_mark_label(is_positive);
        }
            break;
        default:
            throw UnexpectedValueException();
    }
}

void PythonCompiler::emit_unboxed_unary_negative(AbstractValueWithSources val) {
#ifdef DEBUG
    assert(supportsEscaping(val.Value->kind()));
#endif
    switch (val.Value->kind()){
        case AVK_Integer:
        case AVK_Bool:
        case AVK_Float:
            m_il.neg();
            break;
        default:
            throw UnexpectedValueException();
    }
}

void PythonCompiler::emit_unboxed_unary_invert(AbstractValueWithSources val) {
#ifdef DEBUG
    assert(supportsEscaping(val.Value->kind()));
#endif
    switch (val.Value->kind()){
        case AVK_Integer:
        case AVK_Bool:
            m_il.ld_i4(1);
            m_il.add();
            m_il.neg();
            break;
        default:
            throw UnexpectedValueException();
    }
}

void PythonCompiler::emit_infinity() {
    m_il.ld_r8(INFINITY);
}

void PythonCompiler::emit_nan() {
    m_il.ld_r8(NAN);
}

void PythonCompiler::emit_infinity_long() {
    m_il.ld_i8(MAXLONG);
}

void PythonCompiler::emit_nan_long() {
    m_il.ld_i8(MAXLONG);
}

void PythonCompiler::emit_escape_edges(vector<Edge> edges, Local success) {
    emit_int(0);
    emit_store_local(success);// Will get set to 1 on unbox failures.

    // Push the values onto temporary locals, box/unbox them then push
    // them back onto the stack in the same order
    vector<Local> stack = vector<Local>(edges.size());

    for (size_t i = 0; i < stack.size(); i++) {
        if (edges[i].escaped == Unboxed || edges[i].escaped == Box) {
            stack[i] = emit_define_local(edges[i].value->kind());
        } else {
            stack[i] = emit_define_local(LK_Pointer);
        }
        emit_store_local(stack[i]);
    }
    // Recover the stack in the right order
    for (size_t i = edges.size(); i > 0; --i) {
        emit_load_and_free_local(stack[i - 1]);
        switch (edges[i - 1].escaped) {
            case Unbox:
                emit_unbox(edges[i - 1].value->kind(), edges[i - 1].value->needsGuard(), success);
                break;
            case Box:
                emit_box(edges[i - 1].value->kind());
                break;
            default:
                break;
        }
    }
}

void PythonCompiler::emit_store_subscr_unboxed(AbstractValueWithSources value, AbstractValueWithSources container, AbstractValueWithSources key){
    m_il.emit_call(METHOD_STORE_SUBSCR_BYTEARRAY_UB);
}

void PythonCompiler::emit_return_value(Local retValue, Label retLabel){
    emit_store_local(retValue);
    emit_set_frame_state(PY_FRAME_RETURNED);
    emit_set_frame_stackdepth(0);
    emit_branch(BranchAlways, retLabel);
}

void PythonCompiler::emit_yield_value(Local retValue, Label retLabel, py_opindex index, size_t stackSize, offsetLabels& yieldOffsets) {
    emit_lasti_update(index);

    emit_store_local(retValue);
    emit_set_frame_state(PY_FRAME_SUSPENDED);

    // Stack has submitted result back. Store any other variables
    for (uint32_t i = (stackSize - 1); i > 0; --i) {
        emit_store_in_frame_value_stack(i - 1);
    }
    emit_set_frame_stackdepth(stackSize - 1);
    emit_branch(BranchAlways, retLabel);
    // ^ Exit Frame || 🔽 Enter frame from next()
    emit_mark_label(yieldOffsets[index]);
    for (uint32_t i = 0; i < stackSize; i++) {
        emit_load_from_frame_value_stack(i);
    }
    emit_dec_frame_stackdepth(stackSize);
}

/************************************************************************
* End Compiler interface implementation
*/

class GlobalMethod {
    JITMethod m_method;

public:
    GlobalMethod(int token, JITMethod method, const char* label) : m_method(method) {
        m_method = method;
        g_module.m_methods[token] = &m_method;
        g_module.RegisterSymbol(token, label);
    }
};

#define GLOBAL_METHOD(token, addr, returnType, ...) \
    GlobalMethod g##token(token, JITMethod(&g_module, returnType, std::vector<Parameter>{__VA_ARGS__}, (void*) addr, false), #token);

#define GLOBAL_INTRINSIC(token, addr, returnType, ...) \
    GlobalMethod g##token(token, JITMethod(&g_module, returnType, std::vector<Parameter>{__VA_ARGS__}, (void*) addr, true), #token);

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

GLOBAL_METHOD(METHOD_PYLIST_NEW, &PyJit_NewList, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_INT));
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
GLOBAL_METHOD(METHOD_STORE_SUBSCR_BYTEARRAY_UB, &PyJit_StoreByteArrayUnboxed, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_LONG), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_LONG))

GLOBAL_METHOD(METHOD_DELETESUBSCR_TOKEN, &PyJit_DeleteSubscr, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_BUILD_DICT_FROM_TUPLES, &PyJit_BuildDictFromTuples, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_DICT_MERGE, &PyJit_DictMerge, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_PYDICT_NEWPRESIZED, &_PyDict_NewPresized, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_PYTUPLE_NEW, &PyJit_PyTuple_New, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_INT));
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

GLOBAL_METHOD(METHOD_CALL_0_TOKEN, &Call0, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_CALL_1_TOKEN, &Call1, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_CALL_2_TOKEN, &Call2, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_CALL_3_TOKEN, &Call3, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_CALL_4_TOKEN, &Call4, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_CALL_5_TOKEN, &Call5, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_CALL_6_TOKEN, &Call6, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_CALL_7_TOKEN, &Call7, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_CALL_8_TOKEN, &Call8, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_CALL_9_TOKEN, &Call9, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_CALL_10_TOKEN, &Call10, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_CALLN_TOKEN, &PyJit_CallN, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_KWCALLN_TOKEN, &PyJit_KwCallN, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_VECTORCALL, &PyVectorcall_Call, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_OBJECTCALL, &PyObject_Call, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_STOREGLOBAL_TOKEN, &PyJit_StoreGlobal, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_DELETEGLOBAL_TOKEN, &PyJit_DeleteGlobal, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_LOADGLOBAL_TOKEN, &PyJit_LoadGlobal, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

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
GLOBAL_METHOD(METHOD_GET_UNBOXED_ITER, &PyJit_GetUnboxedIter, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_FORITER, &PyJit_IterNext, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_FORITER_UNBOXED, &PyJit_IterNextUnboxed, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_DECREF_TOKEN, &PyJit_DecRef, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_SET_CLOSURE, &PyJit_SetClosure, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_BUILD_SLICE, &PyJit_BuildSlice, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_UNARY_POSITIVE, &PyJit_UnaryPositive, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_UNARY_NEGATIVE, &PyJit_UnaryNegative, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_UNARY_NOT, &PyJit_UnaryNot, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));

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

GLOBAL_METHOD(METHOD_HANDLE_EXCEPTION, &PyJit_HandleException, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT),
              Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_DO_RAISE, &PyJit_Raise, CORINFO_TYPE_BOOL, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_EH_TRACE, &PyJit_EhTrace, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_POP_EXCEPT, &PyJit_PopExcept, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_COMPARE_EXCEPTIONS, &PyJit_CompareExceptions, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_UNBOUND_LOCAL, &PyJit_UnboundLocal, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_PYERR_RESTORE, &PyJit_PyErrRestore, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_PYERR_CLEAR, &PyErr_Clear, CORINFO_TYPE_VOID);
GLOBAL_METHOD(METHOD_DEBUG_TRACE, &PyJit_DebugTrace, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_DEBUG_PTR, &PyJit_DebugPtr, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_DEBUG_TYPE, &PyJit_DebugType, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_DEBUG_PYOBJECT, &PyJit_DebugPyObject, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_DEBUG_FAULT, &PyJit_DebugFault, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_INT), Parameter(CORINFO_TYPE_NATIVEINT));

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

GLOBAL_METHOD(METHOD_FLOAT_POWER_TOKEN, &PyJit_DoublePow, CORINFO_TYPE_DOUBLE, Parameter(CORINFO_TYPE_DOUBLE), Parameter(CORINFO_TYPE_DOUBLE));
GLOBAL_METHOD(METHOD_FLOAT_FLOOR_TOKEN, static_cast<double (*)(double)>(floor), CORINFO_TYPE_DOUBLE, Parameter(CORINFO_TYPE_DOUBLE));
GLOBAL_METHOD(METHOD_INT_POWER, PyJit_LongPow, CORINFO_TYPE_LONG, Parameter(CORINFO_TYPE_LONG), Parameter(CORINFO_TYPE_LONG));
GLOBAL_METHOD(METHOD_INT_FLOOR_DIVIDE, PyJit_LongFloorDivide, CORINFO_TYPE_LONG, Parameter(CORINFO_TYPE_LONG), Parameter(CORINFO_TYPE_LONG));
GLOBAL_METHOD(METHOD_INT_TRUE_DIVIDE, PyJit_LongTrueDivide, CORINFO_TYPE_DOUBLE, Parameter(CORINFO_TYPE_LONG), Parameter(CORINFO_TYPE_LONG));
GLOBAL_METHOD(METHOD_INT_MOD, PyJit_LongMod, CORINFO_TYPE_LONG, Parameter(CORINFO_TYPE_LONG), Parameter(CORINFO_TYPE_LONG));

GLOBAL_METHOD(METHOD_FLOAT_MODULUS_TOKEN, static_cast<double (*)(double, double)>(fmod), CORINFO_TYPE_DOUBLE, Parameter(CORINFO_TYPE_DOUBLE), Parameter(CORINFO_TYPE_DOUBLE));
GLOBAL_METHOD(METHOD_FLOAT_FROM_DOUBLE, PyFloat_FromDouble, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_DOUBLE));
GLOBAL_METHOD(METHOD_BOOL_FROM_LONG, PyBool_FromLong, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_INT));
GLOBAL_METHOD(METHOD_NUMBER_AS_SSIZET, PyNumber_AsSsize_t, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_PYLONG_AS_LONGLONG, PyJit_LongAsLongLong, CORINFO_TYPE_LONG, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_PYLONG_FROM_LONGLONG, PyLong_FromLongLong, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_LONG));

GLOBAL_METHOD(METHOD_PYERR_SETSTRING, PyErr_SetString, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_PYUNICODE_JOINARRAY, &PyJit_UnicodeJoinArray, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_FORMAT_VALUE, &PyJit_FormatValue, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_FORMAT_OBJECT, &PyJit_FormatObject, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_LOAD_METHOD, &PyJit_LoadMethod, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_METHCALL_0_TOKEN, &MethCall0, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_METHCALL_1_TOKEN, &MethCall1, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_METHCALL_2_TOKEN, &MethCall2, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_METHCALL_3_TOKEN, &MethCall3, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_METHCALL_4_TOKEN, &MethCall4, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_METHCALL_5_TOKEN, &MethCall5, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_METHCALL_6_TOKEN, &MethCall6, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_METHCALL_7_TOKEN, &MethCall7, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_METHCALL_8_TOKEN, &MethCall8, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_METHCALL_9_TOKEN, &MethCall9, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_METHCALL_10_TOKEN, &MethCall10, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_METHCALLN_TOKEN, &MethCallN, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_SETUP_ANNOTATIONS, &PyJit_SetupAnnotations, CORINFO_TYPE_INT, Parameter(CORINFO_TYPE_NATIVEINT), );

GLOBAL_METHOD(METHOD_LOAD_ASSERTION_ERROR, &PyJit_LoadAssertionError, CORINFO_TYPE_NATIVEINT);

GLOBAL_METHOD(METHOD_DEALLOC_OBJECT, &_Py_Dealloc, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_TRACE_LINE, &PyJit_TraceLine, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_TRACE_FRAME_ENTRY, &PyJit_TraceFrameEntry, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_TRACE_FRAME_EXIT, &PyJit_TraceFrameExit, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_TRACE_EXCEPTION, &PyJit_TraceFrameException, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_PROFILE_FRAME_ENTRY, &PyJit_ProfileFrameEntry, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_PROFILE_FRAME_EXIT, &PyJit_ProfileFrameExit, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_LOAD_CLOSURE, &PyJit_LoadClosure, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_INT));

GLOBAL_METHOD(METHOD_PENDING_CALLS, &Py_MakePendingCalls, CORINFO_TYPE_INT, );

GLOBAL_METHOD(METHOD_PGC_PROBE, &capturePgcStackValue, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_PGC_GUARD_EXCEPTION, &PyJit_PgcGuardException, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_SEQUENCE_AS_LIST, &PySequence_List, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_LIST_ITEM_FROM_BACK, &PyJit_GetListItemReversed, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_GIL_ENSURE, &PyGILState_Ensure, CORINFO_TYPE_NATIVEINT);
GLOBAL_METHOD(METHOD_GIL_RELEASE, &PyGILState_Release, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_METHOD(METHOD_BLOCK_POP, &PyJit_BlockPop, CORINFO_TYPE_NATIVEINT, Parameter(CORINFO_TYPE_NATIVEINT));
GLOBAL_METHOD(METHOD_BLOCK_PUSH, &PyFrame_BlockSetup, CORINFO_TYPE_VOID, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_INT), Parameter(CORINFO_TYPE_INT), Parameter(CORINFO_TYPE_INT));
GLOBAL_METHOD(METHOD_UNBOX_BOOL, &PyJit_UnboxBool, CORINFO_TYPE_BYTE, Parameter(CORINFO_TYPE_NATIVEINT), Parameter(CORINFO_TYPE_NATIVEINT));

GLOBAL_INTRINSIC(INTRINSIC_TEST, &PyJit_LongTrueDivide, CORINFO_TYPE_DOUBLE, Parameter(CORINFO_TYPE_LONG), Parameter(CORINFO_TYPE_LONG));

const char* opcodeName(py_opcode opcode) {
#define OP_TO_STR(x) \
    case x:          \
        return #x;
    switch (opcode) {// NOLINT(hicpp-multiway-paths-covered)
        OP_TO_STR(POP_TOP)
        OP_TO_STR(ROT_TWO)
        OP_TO_STR(ROT_THREE)
        OP_TO_STR(DUP_TOP)
        OP_TO_STR(DUP_TOP_TWO)
        OP_TO_STR(ROT_FOUR)
        OP_TO_STR(NOP)
        OP_TO_STR(UNARY_POSITIVE)
        OP_TO_STR(UNARY_NEGATIVE)
        OP_TO_STR(UNARY_NOT)
        OP_TO_STR(UNARY_INVERT)
        OP_TO_STR(BINARY_MATRIX_MULTIPLY)
        OP_TO_STR(INPLACE_MATRIX_MULTIPLY)
        OP_TO_STR(BINARY_POWER)
        OP_TO_STR(BINARY_MULTIPLY)
        OP_TO_STR(BINARY_MODULO)
        OP_TO_STR(BINARY_ADD)
        OP_TO_STR(BINARY_SUBTRACT)
        OP_TO_STR(BINARY_SUBSCR)
        OP_TO_STR(BINARY_FLOOR_DIVIDE)
        OP_TO_STR(BINARY_TRUE_DIVIDE)
        OP_TO_STR(INPLACE_FLOOR_DIVIDE)
        OP_TO_STR(INPLACE_TRUE_DIVIDE)
        OP_TO_STR(RERAISE)
        OP_TO_STR(WITH_EXCEPT_START)
        OP_TO_STR(GET_AITER)
        OP_TO_STR(GET_ANEXT)
        OP_TO_STR(BEFORE_ASYNC_WITH)
        OP_TO_STR(END_ASYNC_FOR)
        OP_TO_STR(INPLACE_ADD)
        OP_TO_STR(INPLACE_SUBTRACT)
        OP_TO_STR(INPLACE_MULTIPLY)
        OP_TO_STR(INPLACE_MODULO)
        OP_TO_STR(STORE_SUBSCR)
        OP_TO_STR(DELETE_SUBSCR)
        OP_TO_STR(BINARY_LSHIFT)
        OP_TO_STR(BINARY_RSHIFT)
        OP_TO_STR(BINARY_AND)
        OP_TO_STR(BINARY_XOR)
        OP_TO_STR(BINARY_OR)
        OP_TO_STR(INPLACE_POWER)
        OP_TO_STR(GET_ITER)
        OP_TO_STR(GET_YIELD_FROM_ITER)
        OP_TO_STR(PRINT_EXPR)
        OP_TO_STR(LOAD_BUILD_CLASS)
        OP_TO_STR(YIELD_FROM)
        OP_TO_STR(GET_AWAITABLE)
        OP_TO_STR(LOAD_ASSERTION_ERROR)
        OP_TO_STR(INPLACE_LSHIFT)
        OP_TO_STR(INPLACE_RSHIFT)
        OP_TO_STR(INPLACE_AND)
        OP_TO_STR(INPLACE_XOR)
        OP_TO_STR(INPLACE_OR)
        OP_TO_STR(LIST_TO_TUPLE)
        OP_TO_STR(RETURN_VALUE)
        OP_TO_STR(IMPORT_STAR)
        OP_TO_STR(SETUP_ANNOTATIONS)
        OP_TO_STR(YIELD_VALUE)
        OP_TO_STR(POP_BLOCK)
        OP_TO_STR(POP_EXCEPT)
        OP_TO_STR(STORE_NAME)
        OP_TO_STR(DELETE_NAME)
        OP_TO_STR(UNPACK_SEQUENCE)
        OP_TO_STR(FOR_ITER)
        OP_TO_STR(UNPACK_EX)
        OP_TO_STR(STORE_ATTR)
        OP_TO_STR(DELETE_ATTR)
        OP_TO_STR(STORE_GLOBAL)
        OP_TO_STR(DELETE_GLOBAL)
        OP_TO_STR(LOAD_CONST)
        OP_TO_STR(LOAD_NAME)
        OP_TO_STR(BUILD_TUPLE)
        OP_TO_STR(BUILD_LIST)
        OP_TO_STR(BUILD_SET)
        OP_TO_STR(BUILD_MAP)
        OP_TO_STR(LOAD_ATTR)
        OP_TO_STR(COMPARE_OP)
        OP_TO_STR(IMPORT_NAME)
        OP_TO_STR(IMPORT_FROM)
        OP_TO_STR(JUMP_FORWARD)
        OP_TO_STR(JUMP_IF_FALSE_OR_POP)
        OP_TO_STR(JUMP_IF_TRUE_OR_POP)
        OP_TO_STR(JUMP_ABSOLUTE)
        OP_TO_STR(POP_JUMP_IF_FALSE)
        OP_TO_STR(POP_JUMP_IF_TRUE)
        OP_TO_STR(LOAD_GLOBAL)
        OP_TO_STR(IS_OP)
        OP_TO_STR(CONTAINS_OP)
        OP_TO_STR(JUMP_IF_NOT_EXC_MATCH)
        OP_TO_STR(SETUP_FINALLY)
        OP_TO_STR(LOAD_FAST)
        OP_TO_STR(STORE_FAST)
        OP_TO_STR(DELETE_FAST)
        OP_TO_STR(RAISE_VARARGS)
        OP_TO_STR(CALL_FUNCTION)
        OP_TO_STR(MAKE_FUNCTION)
        OP_TO_STR(BUILD_SLICE)
        OP_TO_STR(LOAD_CLOSURE)
        OP_TO_STR(LOAD_DEREF)
        OP_TO_STR(STORE_DEREF)
        OP_TO_STR(DELETE_DEREF)
        OP_TO_STR(CALL_FUNCTION_EX)
        OP_TO_STR(CALL_FUNCTION_KW)
        OP_TO_STR(SETUP_WITH)
        OP_TO_STR(EXTENDED_ARG)
        OP_TO_STR(LIST_APPEND)
        OP_TO_STR(SET_ADD)
        OP_TO_STR(MAP_ADD)
        OP_TO_STR(LOAD_CLASSDEREF)
        OP_TO_STR(SETUP_ASYNC_WITH)
        OP_TO_STR(FORMAT_VALUE)
        OP_TO_STR(BUILD_CONST_KEY_MAP)
        OP_TO_STR(BUILD_STRING)
        OP_TO_STR(LOAD_METHOD)
        OP_TO_STR(CALL_METHOD)
        OP_TO_STR(LIST_EXTEND)
        OP_TO_STR(SET_UPDATE)
        OP_TO_STR(DICT_MERGE)
        OP_TO_STR(DICT_UPDATE)
        OP_TO_STR(GEN_START)
        OP_TO_STR(COPY_DICT_WITHOUT_KEYS)
        OP_TO_STR(MATCH_CLASS)
        OP_TO_STR(GET_LEN)
        OP_TO_STR(MATCH_MAPPING)
        OP_TO_STR(MATCH_SEQUENCE)
        OP_TO_STR(MATCH_KEYS)
        OP_TO_STR(ROT_N)
    }
    return "unknown";
}

const char* frameStateAsString(PyFrameState state) {
    switch (state) {
        case PY_FRAME_CREATED:
            return "PY_FRAME_CREATED";
        case PY_FRAME_SUSPENDED:
            return "PY_FRAME_SUSPENDED";
        case PY_FRAME_EXECUTING:
            return "PY_FRAME_EXECUTING";
        case PY_FRAME_RETURNED:
            return "PY_FRAME_RETURNED";
        case PY_FRAME_UNWINDING:
            return "PY_FRAME_UNWINDING";
        case PY_FRAME_RAISED:
            return "PY_FRAME_RAISED";
        case PY_FRAME_CLEARED:
            return "PY_FRAME_CLEARED";
        default:
            return "unknown state";
    }
}