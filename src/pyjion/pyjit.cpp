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

#include <Python.h>
#include "pyjit.h"
#include "pycomp.h"

#ifdef WINDOWS
#define BUFSIZE 65535
#include <libloaderapi.h>
#include <processenv.h>
typedef ICorJitCompiler* (__cdecl* GETJIT)();
typedef void(__cdecl* JITSTARTUP)(ICorJitHost*);
#endif

PyjionSettings g_pyjionSettings;
extern BaseModule g_module;
#define SET_OPT(opt, actualLevel, minLevel) \
    g_pyjionSettings.opt_ ## opt = (actualLevel) >= (minLevel) ? true : false;

void setOptimizationLevel(unsigned short level){
    g_pyjionSettings.optimizationLevel = level;
    SET_OPT(inlineIs, level, 1);
    SET_OPT(inlineDecref, level, 1);
    SET_OPT(internRichCompare, level, 1);
    SET_OPT(inlineFramePushPop, level, 1);
    SET_OPT(knownStoreSubscr, level, 1);
    SET_OPT(knownBinarySubscr, level, 1);
    SET_OPT(inlineIterators, level, 1);
    SET_OPT(hashedNames, level, 1);
    SET_OPT(builtinMethods, level, 1);
    SET_OPT(typeSlotLookups, level, 1);
    SET_OPT(functionCalls, level, 1);
    SET_OPT(loadAttr, level, 1);
    SET_OPT(unboxing, level, 1);
}

PgcStatus nextPgcStatus(PgcStatus status){
    switch(status){
        case PgcStatus::Uncompiled: return PgcStatus::CompiledWithProbes;
        case PgcStatus::CompiledWithProbes:
        case PgcStatus::Optimized:
        default:
            return PgcStatus::Optimized;
    }
}

PyjionJittedCode::~PyjionJittedCode() {
	delete j_profile;
}

PyjionCodeProfile::~PyjionCodeProfile() {
    for (auto &pos: this->stackTypes) {
        for(auto &observed: pos.second){
            Py_XDECREF(observed.second);
        }
    }
}

void PyjionCodeProfile::record(size_t opcodePosition, size_t stackPosition, PyObject* value){
    if (PyGen_CheckExact(value) || PyCoro_CheckExact(value))
        return;
    if (this->stackTypes[opcodePosition][stackPosition] == nullptr) {
        this->stackTypes[opcodePosition][stackPosition] = Py_TYPE(value);
        Py_INCREF(Py_TYPE(value));
    }
    this->stackKinds[opcodePosition][stackPosition] = GetAbstractType(Py_TYPE(value), value);
}

PyTypeObject* PyjionCodeProfile::getType(size_t opcodePosition, size_t stackPosition) {
    return this->stackTypes[opcodePosition][stackPosition];
}

AbstractValueKind PyjionCodeProfile::getKind(size_t opcodePosition, size_t stackPosition){
    return this->stackKinds[opcodePosition][stackPosition];
}

void capturePgcStackValue(PyjionCodeProfile* profile, PyObject* value, size_t opcodePosition, size_t stackPosition){
    if (value != nullptr && profile != nullptr){
        profile->record(opcodePosition, stackPosition, value);
    }
}

int
Pyjit_CheckRecursiveCall(PyThreadState *tstate, const char *where)
{
    int recursion_limit = g_pyjionSettings.recursionLimit;

    if (tstate->recursion_critical)
        /* Somebody asked that we don't check for recursion. */
        return 0;
    if (tstate->overflowed) {
        if (tstate->recursion_depth > recursion_limit + 50) {
            /* Overflowing while handling an overflow. Give up. */
            Py_FatalError("Cannot recover from stack overflow.");
        }
        return 0;
    }
    if (tstate->recursion_depth > recursion_limit) {
        --tstate->recursion_depth;
        tstate->overflowed = 1;
        PyErr_Format(PyExc_RecursionError,
                      "maximum recursion depth exceeded - %s.",
                      where);
        return -1;
    }
    return 0;
}

static inline int Pyjit_EnterRecursiveCall(const char *where) {
    PyThreadState *tstate = PyThreadState_GET();
    return ((++tstate->recursion_depth > g_pyjionSettings.recursionLimit)
            && Pyjit_CheckRecursiveCall(tstate, where));
}

static inline void Pyjit_LeaveRecursiveCall() {
    PyThreadState *tstate = PyThreadState_GET();
    tstate->recursion_depth--;
}

static inline PyObject* PyJit_ExecuteJittedFrame(void* state, PyFrameObject*frame, PyThreadState* tstate, PyjionCodeProfile* profile) {
    if (Pyjit_EnterRecursiveCall("")) {
        return nullptr;
    }
    PyObject ** stack_pointer = frame->f_stacktop;
    assert(stack_pointer != nullptr);
    frame->f_stacktop = nullptr;       /* remains NULL unless yield suspends frame */
	frame->f_executing = 1;
    try {
        auto res = ((Py_EvalFunc)state)(nullptr, frame, tstate, profile, stack_pointer);
        Pyjit_LeaveRecursiveCall();
        frame->f_executing = 0;
        return res;
    } catch (const std::exception& e){
        PyErr_SetString(PyExc_RuntimeError, e.what());
        Pyjit_LeaveRecursiveCall();
        frame->f_executing = 0;
        return nullptr;
    }
}

static Py_tss_t* g_extraSlot;

#ifdef WINDOWS
HMODULE GetClrJit() {
    return LoadLibrary(g_pyjionSettings.clrjitpath);
}
#endif

bool JitInit(const wchar_t * path) {
    g_pyjionSettings = {false, false};
    g_pyjionSettings.recursionLimit = Py_GetRecursionLimit();
    g_pyjionSettings.clrjitpath = path;
	g_extraSlot = PyThread_tss_alloc();
	PyThread_tss_create(g_extraSlot);
#ifdef WINDOWS
    auto clrJitHandle = GetClrJit();
	if (clrJitHandle == nullptr) {
	    PyErr_SetString(PyExc_RuntimeError, "Failed to load .NET CLR JIT.");
        return false;
	}
    auto jitStartup = (JITSTARTUP)GetProcAddress(clrJitHandle, "jitStartup");

	if (jitStartup != nullptr)
        jitStartup(&g_jitHost);
    else {
        PyErr_SetString(PyExc_RuntimeError, "Failed to load jitStartup().");
        return false;
    }

	auto getJit = (GETJIT)GetProcAddress(clrJitHandle, "getJit");
	if (getJit == nullptr) {
		PyErr_SetString(PyExc_RuntimeError, "Failed to load clrjit.dll::getJit(), check that the correct version of .NET is installed.");
        return false;
	}
#else
    jitStartup(&g_jitHost);
#endif

	g_jit = getJit();

    if (PyType_Ready(&PyJitMethodLocation_Type) < 0)
        return false;
    g_emptyTuple = PyTuple_New(0);
    return true;
}

PyObject* PyJit_ExecuteAndCompileFrame(PyjionJittedCode* state, PyFrameObject *frame, PyThreadState* tstate, PyjionCodeProfile* profile) {
    // Compile and run the now compiled code...
    PythonCompiler jitter((PyCodeObject*)state->j_code);
    AbstractInterpreter interp((PyCodeObject*)state->j_code, &jitter);
    int argCount = frame->f_code->co_argcount + frame->f_code->co_kwonlyargcount;

    // provide the interpreter information about the specialized types
    for (int i = 0; i < argCount; i++) {
        interp.setLocalType(i, frame->f_localsplus[i]);
    }

    if (g_pyjionSettings.tracing){
        interp.enableTracing();
    } else {
        interp.disableTracing();
    }
    if (g_pyjionSettings.profiling){
        interp.enableProfiling();
    } else {
        interp.disableProfiling();
    }

    auto res = interp.compile(frame->f_builtins, frame->f_globals, profile, state->j_pgc_status);
    state->j_compile_result = res.result;
    if (g_pyjionSettings.graph){
        state->j_graph = res.instructionGraph;
    }
    if (res.compiledCode == nullptr || res.result != Success) {
        state->j_failed = true;
        return _PyEval_EvalFrameDefault(tstate, frame, 0);
    }

    // Update the jitted information for this tree node
    state->j_addr = (Py_EvalFunc)res.compiledCode->get_code_addr();
    assert(state->j_addr != nullptr);
    state->j_il = res.compiledCode->get_il();
    state->j_ilLen = res.compiledCode->get_il_len();
    state->j_nativeSize = res.compiledCode->get_native_size();
    state->j_profile = profile;
    state->j_symbols = res.compiledCode->get_symbol_table();
    state->j_sequencePoints = res.compiledCode->get_sequence_points();
    state->j_sequencePointsLen = res.compiledCode->get_sequence_points_length();
    state->j_callPoints = res.compiledCode->get_call_points();
    state->j_callPointsLen = res.compiledCode->get_call_points_length();

#ifdef DUMP_SEQUENCE_POINTS
    printf("Method disassembly for %s\n", PyUnicode_AsUTF8(frame->f_code->co_name));
    auto code = (_Py_CODEUNIT *)PyBytes_AS_STRING(frame->f_code->co_code);
    for (size_t i = 0; i < state->j_sequencePointsLen; i ++){
        printf(" %016llX (IL_%04X): %d %s %d\n",
            ((uint64_t)state->j_addr + (uint64_t)state->j_sequencePoints[i].nativeOffset),
            state->j_sequencePoints[i].ilOffset,
            state->j_sequencePoints[i].pythonOpcodeIndex,
            opcodeName(_Py_OPCODE(code[(state->j_sequencePoints[i].pythonOpcodeIndex)/sizeof(_Py_CODEUNIT)])),
            _Py_OPARG(code[(state->j_sequencePoints[i].pythonOpcodeIndex)/sizeof(_Py_CODEUNIT)])
        );
    }
#endif

    // Execute it now.
    return PyJit_ExecuteJittedFrame((void*)state->j_addr, frame, tstate, state->j_profile);
}

PyjionJittedCode* PyJit_EnsureExtra(PyObject* codeObject) {
	auto index = (ssize_t)PyThread_tss_get(g_extraSlot);
	if (index == 0) {
		index = _PyEval_RequestCodeExtraIndex(PyjionJitFree);
		if (index == -1) {
			return nullptr;
		}

		PyThread_tss_set(g_extraSlot, (void*)((index << 1) | 0x01));
	}
	else {
		index = index >> 1;
	}

	PyjionJittedCode *jitted = nullptr;
	if (_PyCode_GetExtra(codeObject, index, (void**)&jitted)) {
		PyErr_Clear();
		return nullptr;
	}

	if (jitted == nullptr) {
	    jitted = new PyjionJittedCode(codeObject);
		if (jitted != nullptr) {
			if (_PyCode_SetExtra(codeObject, index, jitted)) {
				PyErr_Clear();

				delete jitted;
				return nullptr;
			}
		}
	}
	return jitted;
}

// This is our replacement evaluation function.  We lookup our corresponding jitted code
// and dispatch to it if it's already compiled.  If it hasn't yet been compiled we'll
// eventually compile it and invoke it.  If it's not time to compile it yet then we'll
// invoke the default evaluation function.
PyObject* PyJit_EvalFrame(PyThreadState *ts, PyFrameObject *f, int throwflag) {
	auto jitted = PyJit_EnsureExtra((PyObject*)f->f_code);
	if (jitted != nullptr && !throwflag) {
		if (jitted->j_addr != nullptr && (!g_pyjionSettings.pgc || jitted->j_pgc_status == Optimized)) {
            jitted->j_run_count++;
			return PyJit_ExecuteJittedFrame((void*)jitted->j_addr, f, ts, jitted->j_profile);
		}
		else if (!jitted->j_failed && jitted->j_run_count++ >= jitted->j_specialization_threshold) {
			auto result = PyJit_ExecuteAndCompileFrame(jitted, f, ts, jitted->j_profile);
            jitted->j_pgc_status = nextPgcStatus(jitted->j_pgc_status);
			return result;
		}
	}
	return _PyEval_EvalFrameDefault(ts, f, throwflag);
}

void PyjionJitFree(void* obj) {
    if (obj == nullptr)
        return;
    auto* code_obj = static_cast<PyjionJittedCode *>(obj);
    Py_XDECREF(code_obj->j_code);
    free(code_obj->j_il);
    code_obj->j_il = nullptr;
    delete code_obj->j_profile;
}

static PyInterpreterState* inter(){
    return PyInterpreterState_Main();
}

static PyObject *pyjion_enable(PyObject *self, PyObject* args) {
    setOptimizationLevel(1);
    auto prev = _PyInterpreterState_GetEvalFrameFunc(inter());
    _PyInterpreterState_SetEvalFrameFunc(inter(), PyJit_EvalFrame);
    if (prev == PyJit_EvalFrame) {
        Py_RETURN_FALSE;
    }
    Py_RETURN_TRUE;
}

static PyObject *pyjion_disable(PyObject *self, PyObject* args) {
	auto prev = _PyInterpreterState_GetEvalFrameFunc(inter());
    _PyInterpreterState_SetEvalFrameFunc(inter(), _PyEval_EvalFrameDefault);
    if (prev == PyJit_EvalFrame) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

static PyObject *pyjion_info(PyObject *self, PyObject* func) {
	PyObject* code;
	if (PyFunction_Check(func)) {
		code = ((PyFunctionObject*)func)->func_code;
	}
	else if (PyCode_Check(func)) {
		code = func;
	}
	else {
		PyErr_SetString(PyExc_TypeError, "Expected function or code");
		return nullptr;
	}
	auto res = PyDict_New();
	if (res == nullptr) {
		return nullptr;
	}

	PyjionJittedCode* jitted = PyJit_EnsureExtra(code);

	PyDict_SetItemString(res, "failed", jitted->j_failed ? Py_True : Py_False);
    PyDict_SetItemString(res, "compile_result", PyLong_FromLong(jitted->j_compile_result));
    PyDict_SetItemString(res, "compiled", jitted->j_addr != nullptr ? Py_True : Py_False);
    PyDict_SetItemString(res, "pgc", PyLong_FromLong(jitted->j_pgc_status));

    auto runCount = PyLong_FromUnsignedLongLong(jitted->j_run_count);
	PyDict_SetItemString(res, "run_count", runCount);
	Py_DECREF(runCount);
	
	return res;
}

static PyObject *pyjion_dump_il(PyObject *self, PyObject* func) {
    PyObject* code;
    if (PyFunction_Check(func)) {
        code = ((PyFunctionObject*)func)->func_code;
    }
    else if (PyCode_Check(func)) {
        code = func;
    }
    else {
        PyErr_SetString(PyExc_TypeError, "Expected function or code");
        return nullptr;
    }

    PyjionJittedCode* jitted = PyJit_EnsureExtra(code);
    if (jitted->j_failed || jitted->j_addr == nullptr)
         Py_RETURN_NONE;

    auto res = PyByteArray_FromStringAndSize(reinterpret_cast<const char *>(jitted->j_il), jitted->j_ilLen);
    if (res == nullptr) {
        return nullptr;
    }
    return res;
}

static PyObject* pyjion_dump_native(PyObject *self, PyObject* func) {
    PyObject* code;
    if (PyFunction_Check(func)) {
        code = ((PyFunctionObject*)func)->func_code;
    }
    else if (PyCode_Check(func)) {
        code = func;
    }
    else {
        PyErr_SetString(PyExc_TypeError, "Expected function or code");
        return nullptr;
    }

    PyjionJittedCode* jitted = PyJit_EnsureExtra(code);
    if (jitted->j_failed || jitted->j_addr == nullptr)
        Py_RETURN_NONE;

    auto result_t = PyTuple_New(3);
    if (result_t == nullptr)
        return nullptr;

    auto res = PyByteArray_FromStringAndSize(reinterpret_cast<const char *>(jitted->j_addr), jitted->j_nativeSize);
    if (res == nullptr)
        return nullptr;

    PyTuple_SET_ITEM(result_t, 0, res);
    Py_INCREF(res);

    auto codeLen = PyLong_FromUnsignedLong(jitted->j_nativeSize);
    if (codeLen == nullptr)
        return nullptr;
    PyTuple_SET_ITEM(result_t, 1, codeLen);
    Py_INCREF(codeLen);

    auto codePosition = PyLong_FromUnsignedLongLong(reinterpret_cast<unsigned long long>(&jitted->j_addr));
    if (codePosition == nullptr)
        return nullptr;
    PyTuple_SET_ITEM(result_t, 2, codePosition);
    Py_INCREF(codePosition);

    return result_t;
}

static PyObject* pyjion_get_offsets(PyObject* self, PyObject* func ){
    PyObject* code;
    if (PyFunction_Check(func)) {
        code = ((PyFunctionObject*)func)->func_code;
    }
    else if (PyCode_Check(func)) {
        code = func;
    }
    else {
        PyErr_SetString(PyExc_TypeError, "Expected function or code");
        return nullptr;
    }

    PyjionJittedCode* jitted = PyJit_EnsureExtra(code);
    if (jitted->j_failed || jitted->j_addr == nullptr)
        Py_RETURN_NONE;

    auto offsets = PyTuple_New(jitted->j_sequencePointsLen + jitted->j_callPointsLen);
    if (offsets == nullptr)
        return nullptr;
    size_t idx = 0;
    for (size_t i = 0; i < jitted->j_sequencePointsLen; i++, idx++){
        auto offset = PyTuple_New(4);
        PyTuple_SET_ITEM(offset, 0, PyLong_FromSize_t(jitted->j_sequencePoints[i].pythonOpcodeIndex));
        PyTuple_SET_ITEM(offset, 1, PyLong_FromSize_t(jitted->j_sequencePoints[i].ilOffset));
        PyTuple_SET_ITEM(offset, 2, PyLong_FromSize_t(jitted->j_sequencePoints[i].nativeOffset));
        PyTuple_SET_ITEM(offset, 3, PyUnicode_FromString("instruction"));
        PyTuple_SET_ITEM(offsets, idx, offset);
        Py_INCREF(offset);
    }

    for (size_t i = 0; i < jitted->j_callPointsLen; i++, idx++){
        auto offset = PyTuple_New(4);
        PyTuple_SET_ITEM(offset, 0, PyLong_FromLong(jitted->j_callPoints[i].tokenId));
        PyTuple_SET_ITEM(offset, 1, PyLong_FromSize_t(jitted->j_callPoints[i].ilOffset));
        PyTuple_SET_ITEM(offset, 2, PyLong_FromSize_t(jitted->j_callPoints[i].nativeOffset));
        PyTuple_SET_ITEM(offset, 3, PyUnicode_FromString("call"));
        PyTuple_SET_ITEM(offsets, idx, offset);
        Py_INCREF(offset);
    }

    return offsets;
}

static PyObject* pyjion_set_threshold(PyObject *self, PyObject* args) {
	if (!PyLong_Check(args)) {
		PyErr_SetString(PyExc_TypeError, "Expected int for new threshold");
		return nullptr;
	}

	auto newValue = PyLong_AsLongLong(args);
	if (newValue < 0) {
		PyErr_SetString(PyExc_ValueError, "Expected positive threshold");
		return nullptr;
	}

	auto prev = PyLong_FromUnsignedLongLong(HOT_CODE);
	HOT_CODE = PyLong_AsLongLong(args);
	return prev;
}

static PyObject* pyjion_get_threshold(PyObject *self, PyObject* args) {
	return PyLong_FromUnsignedLongLong(HOT_CODE);
}

static PyObject* pyjion_enable_tracing(PyObject *self, PyObject* args) {
    g_pyjionSettings.tracing = true;
    Py_RETURN_NONE;
}

static PyObject* pyjion_disable_tracing(PyObject *self, PyObject* args) {
    g_pyjionSettings.tracing = false;
    Py_RETURN_NONE;
}

static PyObject* pyjion_enable_debug(PyObject *self, PyObject* args) {
    g_pyjionSettings.debug = true;
    Py_RETURN_NONE;
}

static PyObject* pyjion_disable_debug(PyObject *self, PyObject* args) {
    g_pyjionSettings.debug = false;
    Py_RETURN_NONE;
}

static PyObject* pyjion_enable_profiling(PyObject *self, PyObject* args) {
    g_pyjionSettings.profiling = true;
    Py_RETURN_NONE;
}

static PyObject* pyjion_disable_profiling(PyObject *self, PyObject* args) {
    g_pyjionSettings.profiling = false;
    Py_RETURN_NONE;
}

static PyObject* pyjion_enable_pgc(PyObject *self, PyObject* args) {
    g_pyjionSettings.pgc = true;
    Py_RETURN_NONE;
}

static PyObject* pyjion_disable_pgc(PyObject *self, PyObject* args) {
    g_pyjionSettings.pgc = false;
    Py_RETURN_NONE;
}

static PyObject* pyjion_enable_graphs(PyObject *self, PyObject* args) {
    g_pyjionSettings.graph = true;
    Py_RETURN_NONE;
}

static PyObject* pyjion_disable_graphs(PyObject *self, PyObject* args) {
    g_pyjionSettings.graph = false;
    Py_RETURN_NONE;
}

static PyObject* pyjion_status(PyObject *self, PyObject* args) {
    auto res = PyDict_New();
	if (res == nullptr) {
		return nullptr;
	}

	PyDict_SetItemString(res, "clrjitpath", PyUnicode_FromWideChar(g_pyjionSettings.clrjitpath, -1));
	PyDict_SetItemString(res, "tracing", g_pyjionSettings.tracing ? Py_True : Py_False);
	PyDict_SetItemString(res, "profiling", g_pyjionSettings.profiling ? Py_True : Py_False);
	PyDict_SetItemString(res, "pgc", g_pyjionSettings.pgc ? Py_True : Py_False);
	PyDict_SetItemString(res, "graph", g_pyjionSettings.graph ? Py_True : Py_False);
 	PyDict_SetItemString(res, "debug", g_pyjionSettings.debug ? Py_True : Py_False);

	return res;
}

static PyObject *pyjion_get_graph(PyObject *self, PyObject* func) {
    PyObject* code;
    if (PyFunction_Check(func)) {
        code = ((PyFunctionObject*)func)->func_code;
    }
    else if (PyCode_Check(func)) {
        code = func;
    }
    else {
        PyErr_SetString(PyExc_TypeError, "Expected function or code");
        return nullptr;
    }

    PyjionJittedCode* jitted = PyJit_EnsureExtra(code);
    Py_INCREF(jitted->j_graph);
    return jitted->j_graph;
}

static PyObject *pyjion_symbols(PyObject *self, PyObject* func) {
    PyObject* code;
    if (PyFunction_Check(func)) {
        code = ((PyFunctionObject*)func)->func_code;
    }
    else if (PyCode_Check(func)) {
        code = func;
    }
    else {
        PyErr_SetString(PyExc_TypeError, "Expected function or code");
        return nullptr;
    }

    PyjionJittedCode* jitted = PyJit_EnsureExtra(code);

    auto table = PyDict_New();
    if (table == nullptr)
        return nullptr;
    for(auto & entry: jitted->j_symbols) {
        PyDict_SetItem(table, PyLong_FromUnsignedLong(entry.first), PyUnicode_FromString(entry.second));
    }
    return table;
}

static PyObject* pyjion_set_optimization_level(PyObject *self, PyObject* args) {
    if (!PyLong_Check(args)) {
        PyErr_SetString(PyExc_TypeError, "Expected int for new threshold");
        return nullptr;
    }

    auto newValue = PyLong_AsUnsignedLong(args);
    if (newValue > 2) {
        PyErr_SetString(PyExc_ValueError, "Expected a number smaller than 3");
        return nullptr;
    }

    setOptimizationLevel(newValue);
    Py_RETURN_NONE;
}

static PyObject* pyjion_init(PyObject *self, PyObject* args) {
    if (!PyUnicode_Check(args)) {
        PyErr_SetString(PyExc_TypeError, "Expected str for new clrjit");
        return nullptr;
    }

    auto path = PyUnicode_AsWideCharString(args, nullptr);
    if (JitInit(path)) {
        Py_RETURN_NONE;
    } else{
        return nullptr;
    }
}

static PyMethodDef PyjionMethods[] = {
	{ 
		"enable",  
		pyjion_enable, 
		METH_NOARGS, 
		"Enable the JIT.  Returns True if the JIT was enabled, False if it was already enabled." 
	},
	{ 
		"disable", 
		pyjion_disable, 
		METH_NOARGS, 
		"Disable the JIT.  Returns True if the JIT was disabled, False if it was already disabled." 
	},
	{
		"info",
		pyjion_info,
		METH_O,
		"Returns a dictionary describing information about a function or code objects current JIT status."
	},
    {
        "dump_il",
        pyjion_dump_il,
        METH_O,
        "Outputs the IL for the compiled code object."
    },
    {
        "dump_native",
        pyjion_dump_native,
        METH_O,
        "Outputs the machine code for the compiled code object."
    },
    {
        "get_offsets",
        pyjion_get_offsets,
        METH_O,
        "Get the sequence of offsets for IL and machine code for given python bytecodes."
    },
	{
		"set_threshold",
		pyjion_set_threshold,
		METH_O,
		"Sets the number of times a method needs to be executed before the JIT is triggered."
	},
	{
		"get_threshold",
		pyjion_get_threshold,
		METH_O,
		"Gets the number of times a method needs to be executed before the JIT is triggered."
	},
    {
        "set_optimization_level",
        pyjion_set_optimization_level,
        METH_O,
        "Sets optimization level (0 = None, 1 = Common, 2 = Maximum)."
    },
    {
        "enable_tracing",
        pyjion_enable_tracing,
        METH_NOARGS,
        "Enable tracing for generated code."
    },
    {
        "disable_tracing",
        pyjion_disable_tracing,
        METH_NOARGS,
        "Enable tracing for generated code."
    },
    {
        "enable_debug",
        pyjion_enable_debug,
        METH_NOARGS,
        "Enable debug symbols for generated code."
    },
    {
        "disable_debug",
        pyjion_disable_debug,
        METH_NOARGS,
        "Enable debug symbols for generated code."
    },
    {
        "enable_profiling",
        pyjion_enable_profiling,
        METH_NOARGS,
        "Enable Python profiling for generated code."
    },
    {
        "disable_profiling",
        pyjion_disable_profiling,
        METH_NOARGS,
        "Disable Python profiling for generated code."
    },
    {
        "enable_pgc",
        pyjion_enable_pgc,
        METH_NOARGS,
        "Enable profile-guided-compilation."
    },
    {
        "disable_pgc",
        pyjion_disable_pgc,
        METH_NOARGS,
        "Disable profile-guided-compilation."
    },
    {
        "enable_graphs",
        pyjion_enable_graphs,
        METH_NOARGS,
        "Enable generating instruction graphs."
    },
    {
        "disable_graphs",
        pyjion_disable_graphs,
        METH_NOARGS,
        "Disable generating instruction graphs."
    },
    {
        "get_graph",
        pyjion_get_graph,
        METH_O,
        "Fetch instruction graph for code object."
    },
    {
        "init",
        pyjion_init,
        METH_O,
        "Initialize JIT."
    },
    {
        "status",
        pyjion_status,
        METH_NOARGS,
        "JIT Status."
      },
  {
        "symbols",
        pyjion_symbols,
        METH_O,
        "Return a list of global symbols."

    },
	{nullptr, nullptr, 0, nullptr}        /* Sentinel */
};

static struct PyModuleDef pyjionmodule = {
	PyModuleDef_HEAD_INIT,
	"_pyjion",   /* name of module */
	"Pyjion - A Just-in-Time Compiler for CPython", /* module documentation, may be NULL */
	-1,       /* size of per-interpreter state of the module,
			  or -1 if the module keeps state in global variables. */
	PyjionMethods
}; 

PyMODINIT_FUNC PyInit__pyjion(void)
{
	// Install our frame evaluation function
	return PyModule_Create(&pyjionmodule);
}
