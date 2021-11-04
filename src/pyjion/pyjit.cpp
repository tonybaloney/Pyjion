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
#include <filesystem>
#include <Python.h>
#include "pyjit.h"
#include "pycomp.h"

#include "objects/assemblyobject.h"

#ifdef WINDOWS
#define BUFSIZE 65535
#include <libloaderapi.h>
#include <processenv.h>
typedef ICorJitCompiler*(__cdecl* GETJIT)();
typedef void(__cdecl* JITSTARTUP)(ICorJitHost*);
#endif

#define MAX_UINT8_T  255
#define MAX_UINT16_T 65535

PyjionSettings g_pyjionSettings;
extern BaseModule g_module;
#define SET_OPT(opt, actualLevel, minLevel)                                      \
    if ((actualLevel) >= (minLevel)) {                                           \
        g_pyjionSettings.optimizations = g_pyjionSettings.optimizations | (opt); \
    }

void setOptimizationLevel(unsigned short level) {
    g_pyjionSettings.optimizationLevel = level;
    g_pyjionSettings.optimizations = OptimizationFlags();
    SET_OPT(InlineIs, level, 1);
    SET_OPT(InlineDecref, level, 1);
    SET_OPT(InternRichCompare, level, 1);
    SET_OPT(InlineFramePushPop, level, 1);
    SET_OPT(KnownStoreSubscr, level, 1);
    SET_OPT(KnownBinarySubscr, level, 1);
    SET_OPT(InlineIterators, level, 1);
    SET_OPT(HashedNames, level, 1);
    SET_OPT(BuiltinMethods, level, 1);
    SET_OPT(TypeSlotLookups, level, 1);
    SET_OPT(FunctionCalls, level, 1);
    SET_OPT(LoadAttr, level, 1);
    SET_OPT(Unboxing, level, 1);
    SET_OPT(IsNone, level, 1);
    SET_OPT(IntegerUnboxingMultiply, level, 2);
    SET_OPT(OptimisticIntegers, level, 2);
}

PyjionJittedCode::~PyjionJittedCode() {
    delete j_profile;
}

int Pyjit_CheckRecursiveCall(PyThreadState* tstate, const char* where) {
    int recursion_limit = g_pyjionSettings.recursionLimit;

    if (tstate->recursion_headroom) {
        if (tstate->recursion_depth > recursion_limit + 50) {
            /* Overflowing while handling an overflow. Give up. */
            Py_FatalError("Cannot recover from stack overflow.");
        }
    } else {
        if (tstate->recursion_depth > recursion_limit) {
            tstate->recursion_headroom++;
            PyErr_Format(PyExc_RecursionError,
                         "maximum recursion depth exceeded%s",
                         where);
            tstate->recursion_headroom--;
            --tstate->recursion_depth;
            return -1;
        }
    }
    return 0;
}

static inline int Pyjit_EnterRecursiveCall(const char* where) {
    PyThreadState* tstate = PyThreadState_GET();
    return ((++tstate->recursion_depth > g_pyjionSettings.recursionLimit) && Pyjit_CheckRecursiveCall(tstate, where));
}

static inline void Pyjit_LeaveRecursiveCall() {
    PyThreadState* tstate = PyThreadState_GET();
    tstate->recursion_depth--;
}

static inline PyObject*
PyJit_CheckFunctionResult(PyThreadState* tstate, PyObject* result, PyFrameObject* frame) {
    if (result == nullptr) {
        if (!PyErr_Occurred()) {
            PyErr_Format(PyExc_SystemError,
                         "%s returned NULL without setting an exception",
                         PyUnicode_AsUTF8(frame->f_code->co_name));
            return nullptr;
        }
    } else {
        if (PyErr_Occurred()) {
            Py_DECREF(result);

            _PyErr_FormatFromCause(PyExc_SystemError,
                                   "%s returned a result with an exception set", PyUnicode_AsUTF8(frame->f_code->co_name));
            return nullptr;
        }
    }
    return result;
}

static inline PyObject* PyJit_ExecuteJittedFrame(void* state, PyFrameObject* frame, PyThreadState* tstate, PyjionJittedCode* jitted) {
    if (Pyjit_EnterRecursiveCall("")) {
        return nullptr;
    }

    PyTraceInfo trace_info;
    /* Mark trace_info as uninitialized */
    trace_info.code = nullptr;
    CFrame* prev_cframe = tstate->cframe;
    trace_info.cframe.use_tracing = prev_cframe->use_tracing;
    trace_info.cframe.previous = prev_cframe;
    tstate->cframe = &trace_info.cframe;

    if (frame->f_state != PY_FRAME_SUSPENDED)
        frame->f_stackdepth = -1;
    frame->f_state = PY_FRAME_EXECUTING;

    try {
        auto res = ((Py_EvalFunc) state)(nullptr, frame, tstate, jitted->j_profile, &trace_info);
        tstate->cframe = trace_info.cframe.previous;
        tstate->cframe->use_tracing = trace_info.cframe.use_tracing;
        Pyjit_LeaveRecursiveCall();
        return PyJit_CheckFunctionResult(tstate, res, frame);
    } catch (const std::exception& e) {
#ifdef DEBUG_VERBOSE
        printf("Caught exception on execution of frame %s\n", e.what());
#endif
        PyErr_SetString(PyExc_RuntimeError, e.what());
        Pyjit_LeaveRecursiveCall();
        return nullptr;
    }
}

static Py_tss_t* g_extraSlot;

#ifdef WINDOWS
HMODULE GetClrJit() {
    return LoadLibrary(g_pyjionSettings.clrjitpath);
}
#endif

bool JitInit(const char* dotnetroot, const char* version) {
    g_pyjionSettings = PyjionSettings();
    g_pyjionSettings.recursionLimit = Py_GetRecursionLimit();

    filesystem::path dir (dotnetroot);
#ifdef LINUX
    filesystem::path jitname ("clrjit.dll");
    filesystem::path fxrname ("hostfxr.dll");
#else
#ifdef WINDOWS
    filesystem::path jitname ("libclrjit.so");
    filesystem::path fxrname ("libhostfxr.so");
#else
    filesystem::path jitname ("libclrjit.dylib");
    filesystem::path fxrname ("libhostfxr.dylib");
#endif
#endif
    g_pyjionSettings.clrjitpath = (dir / std::filesystem::path("shared/Microsoft.NETCore.App/") / std::filesystem::path(version) / jitname);
    g_pyjionSettings.hostfxrpath = (dir / std::filesystem::path("host/fxr/") / std::filesystem::path(version) / fxrname);
    g_pyjionSettings.runtimeconfigpath = std::filesystem::path("pyjion.runtimeconfig.json");

    g_extraSlot = PyThread_tss_alloc();
    PyThread_tss_create(g_extraSlot);
#ifdef WINDOWS
    auto clrJitHandle = GetClrJit();
    if (clrJitHandle == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to load .NET CLR JIT.");
        return false;
    }
    auto jitStartup = (JITSTARTUP) GetProcAddress(clrJitHandle, "jitStartup");

    if (jitStartup != nullptr)
        jitStartup(&g_jitHost);
    else {
        PyErr_SetString(PyExc_RuntimeError, "Failed to load jitStartup().");
        return false;
    }

    auto getJit = (GETJIT) GetProcAddress(clrJitHandle, "getJit");
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

PyObject* PyJit_ExecuteAndCompileFrame(PyjionJittedCode* state, PyFrameObject* frame, PyThreadState* tstate, PyjionCodeProfile* profile) {
    // Compile and run the now compiled code...
    PythonCompiler jitter((PyCodeObject*) state->j_code);
    AbstractInterpreter interp((PyCodeObject*) state->j_code, &jitter);
    int argCount = frame->f_code->co_argcount + frame->f_code->co_kwonlyargcount;

    // provide the interpreter information about the specialized types
    for (int i = 0; i < argCount; i++) {
        interp.setLocalType(i, frame->f_localsplus[i]);
    }

    if (tstate->cframe->use_tracing && tstate->c_tracefunc) {
        interp.enableTracing();
        state->j_tracingHooks = true;
    } else {
        interp.disableTracing();
        state->j_tracingHooks = false;
    }
    if (tstate->cframe->use_tracing && tstate->c_profilefunc) {
        interp.enableProfiling();
        state->j_profilingHooks = true;
    } else {
        interp.disableProfiling();
        state->j_profilingHooks = false;
    }

    auto res = interp.compile(frame->f_builtins, frame->f_globals, profile, state->j_pgc_status);
    state->j_compile_result = res.result;
    state->j_optimizations = res.optimizations;
    if (g_pyjionSettings.graph) {
        state->j_graph = res.instructionGraph;
    }
    if (res.compiledCode == nullptr || res.result != Success) {
        state->j_failed = true;
        state->j_addr = nullptr;// TODO : Raise specific warning when it used to compile and then it didnt the second time.
        return _PyEval_EvalFrameDefault(tstate, frame, 0);
    }

    // Update the jitted information for this tree node
    state->j_addr = (Py_EvalFunc) res.compiledCode->get_code_addr();
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
    auto code = (_Py_CODEUNIT*) PyBytes_AS_STRING(frame->f_code->co_code);
    for (size_t i = 0; i < state->j_sequencePointsLen; i++) {
        printf(" %016llX (IL_%04X): %d %s %d\n",
               ((uint64_t) state->j_addr + (uint64_t) state->j_sequencePoints[i].nativeOffset),
               state->j_sequencePoints[i].ilOffset,
               state->j_sequencePoints[i].pythonOpcodeIndex,
               opcodeName(_Py_OPCODE(code[(state->j_sequencePoints[i].pythonOpcodeIndex) / sizeof(_Py_CODEUNIT)])),
               _Py_OPARG(code[(state->j_sequencePoints[i].pythonOpcodeIndex) / sizeof(_Py_CODEUNIT)]));
    }
#endif

    // Execute it now.
    return PyJit_ExecuteJittedFrame((void*) state->j_addr, frame, tstate, state);
}

PyjionJittedCode* PyJit_EnsureExtra(PyObject* codeObject) {
    auto index = (ssize_t) PyThread_tss_get(g_extraSlot);
    if (index == 0) {
        index = _PyEval_RequestCodeExtraIndex(PyjionJitFree);
        if (index == -1) {
            return nullptr;
        }

        PyThread_tss_set(g_extraSlot, (void*) ((index << 1) | 0x01));
    } else {
        index = index >> 1;
    }

    PyjionJittedCode* jitted = nullptr;
    if (_PyCode_GetExtra(codeObject, index, (void**) &jitted)) {
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
PyObject* PyJit_EvalFrame(PyThreadState* ts, PyFrameObject* f, int throwflag) {
    auto jitted = PyJit_EnsureExtra((PyObject*) f->f_code);
    if (jitted != nullptr && !throwflag) {
        if (jitted->j_addr != nullptr && !jitted->j_failed && (!g_pyjionSettings.pgc || jitted->j_pgc_status == Optimized)) {
            jitted->j_run_count++;
            return PyJit_ExecuteJittedFrame((void*) jitted->j_addr, f, ts, jitted);
        } else if (!jitted->j_failed && jitted->j_run_count++ >= jitted->j_specialization_threshold) {
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
    auto* code_obj = static_cast<PyjionJittedCode*>(obj);
    Py_XDECREF(code_obj->j_code);
    free(code_obj->j_il);
    code_obj->j_il = nullptr;
    delete code_obj->j_profile;
    Py_XDECREF(code_obj->j_graph);
}

static PyInterpreterState* inter() {
    return PyInterpreterState_Main();
}

static PyObject* pyjion_enable(PyObject* self, PyObject* args) {
    setOptimizationLevel(1);
    auto prev = _PyInterpreterState_GetEvalFrameFunc(inter());
    _PyInterpreterState_SetEvalFrameFunc(inter(), PyJit_EvalFrame);
    if (prev == PyJit_EvalFrame) {
        Py_RETURN_FALSE;
    }
    Py_RETURN_TRUE;
}

static PyObject* pyjion_disable(PyObject* self, PyObject* args) {
    auto prev = _PyInterpreterState_GetEvalFrameFunc(inter());
    _PyInterpreterState_SetEvalFrameFunc(inter(), _PyEval_EvalFrameDefault);
    if (prev == PyJit_EvalFrame) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

static PyObject* pyjion_info(PyObject* self, PyObject* func) {
    PyObject* code;
    if (PyFunction_Check(func)) {
        code = ((PyFunctionObject*) func)->func_code;
    } else if (PyCode_Check(func)) {
        code = func;
    } else {
        PyErr_SetString(PyExc_TypeError, "Expected function or code");
        return nullptr;
    }
    auto res = PyDict_New();
    if (res == nullptr) {
        return nullptr;
    }

    PyjionJittedCode* jitted = PyJit_EnsureExtra(code);

    PyDict_SetItemString(res, "failed", jitted->j_failed ? Py_True : Py_False);
    PyDict_SetItemString(res, "tracing", jitted->j_tracingHooks ? Py_True : Py_False);
    PyDict_SetItemString(res, "profiling", jitted->j_profilingHooks ? Py_True : Py_False);
    PyDict_SetItemString(res, "compile_result", PyLong_FromLong(jitted->j_compile_result));
    PyDict_SetItemString(res, "compiled", jitted->j_addr != nullptr ? Py_True : Py_False);
    PyDict_SetItemString(res, "optimizations", PyLong_FromLong(jitted->j_optimizations));
    PyDict_SetItemString(res, "pgc", PyLong_FromLong(jitted->j_pgc_status));

    auto runCount = PyLong_FromUnsignedLongLong(jitted->j_run_count);
    PyDict_SetItemString(res, "run_count", runCount);
    Py_DECREF(runCount);

    return res;
}

static PyObject* pyjion_dump_il(PyObject* self, PyObject* func) {
    PyObject* code;
    if (PyFunction_Check(func)) {
        code = ((PyFunctionObject*) func)->func_code;
    } else if (PyCode_Check(func)) {
        code = func;
    } else {
        PyErr_SetString(PyExc_TypeError, "Expected function or code");
        return nullptr;
    }

    PyjionJittedCode* jitted = PyJit_EnsureExtra(code);
    if (jitted->j_failed || jitted->j_addr == nullptr)
        Py_RETURN_NONE;

    auto res = PyByteArray_FromStringAndSize(reinterpret_cast<const char*>(jitted->j_il), jitted->j_ilLen);
    if (res == nullptr) {
        return nullptr;
    }
    return res;
}

static PyObject* pyjion_dump_native(PyObject* self, PyObject* func) {
    PyObject* code;
    if (PyFunction_Check(func)) {
        code = ((PyFunctionObject*) func)->func_code;
    } else if (PyCode_Check(func)) {
        code = func;
    } else {
        PyErr_SetString(PyExc_TypeError, "Expected function or code");
        return nullptr;
    }

    PyjionJittedCode* jitted = PyJit_EnsureExtra(code);
    if (jitted->j_failed || jitted->j_addr == nullptr)
        Py_RETURN_NONE;

    auto result_t = PyTuple_New(3);
    if (result_t == nullptr)
        return nullptr;

    auto res = PyByteArray_FromStringAndSize(reinterpret_cast<const char*>(jitted->j_addr), jitted->j_nativeSize);
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

static PyObject* pyjion_get_offsets(PyObject* self, PyObject* func) {
    PyObject* code;
    if (PyFunction_Check(func)) {
        code = ((PyFunctionObject*) func)->func_code;
    } else if (PyCode_Check(func)) {
        code = func;
    } else {
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
    for (size_t i = 0; i < jitted->j_sequencePointsLen; i++, idx++) {
        auto offset = PyTuple_New(4);
        PyTuple_SET_ITEM(offset, 0, PyLong_FromSize_t(jitted->j_sequencePoints[i].pythonOpcodeIndex));
        PyTuple_SET_ITEM(offset, 1, PyLong_FromSize_t(jitted->j_sequencePoints[i].ilOffset));
        PyTuple_SET_ITEM(offset, 2, PyLong_FromSize_t(jitted->j_sequencePoints[i].nativeOffset));
        PyTuple_SET_ITEM(offset, 3, PyUnicode_FromString("instruction"));
        PyTuple_SET_ITEM(offsets, idx, offset);
        Py_INCREF(offset);
    }

    for (size_t i = 0; i < jitted->j_callPointsLen; i++, idx++) {
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

static PyObject* pyjion_get_graph(PyObject* self, PyObject* func) {
    PyObject* code;
    if (PyFunction_Check(func)) {
        code = ((PyFunctionObject*) func)->func_code;
    } else if (PyCode_Check(func)) {
        code = func;
    } else {
        PyErr_SetString(PyExc_TypeError, "Expected function or code");
        return nullptr;
    }

    PyjionJittedCode* jitted = PyJit_EnsureExtra(code);
    if (jitted->j_failed) {
        PyErr_SetString(PyExc_ValueError, "Function failed to compile so it has no graph.");
        return nullptr;
    }
    if (jitted->j_graph == nullptr) {
        PyErr_SetString(PyExc_ValueError, "Compiled function has no graph, graphing was not enabled when it was compiled");
        return nullptr;
    }
    Py_INCREF(jitted->j_graph);
    return jitted->j_graph;
}

static PyObject* pyjion_symbols(PyObject* self, PyObject* func) {
    PyObject* code;
    if (PyFunction_Check(func)) {
        code = ((PyFunctionObject*) func)->func_code;
    } else if (PyCode_Check(func)) {
        code = func;
    } else {
        PyErr_SetString(PyExc_TypeError, "Expected function or code");
        return nullptr;
    }

    PyjionJittedCode* jitted = PyJit_EnsureExtra(code);

    auto table = PyDict_New();
    if (table == nullptr)
        return nullptr;
    for (auto& entry : jitted->j_symbols) {
        PyDict_SetItem(table, PyLong_FromUnsignedLong(entry.first), PyUnicode_FromString(entry.second));
    }
    return table;
}

static PyObject* pyjion_init(PyObject* self, PyObject* args) {
    if (!PyTuple_Check(args) && PyTuple_Size(args) != 2) {
        PyErr_SetString(PyExc_TypeError, "Expected str for new clrjit");
        return nullptr;
    }

    auto path = PyUnicode_AsUTF8(PyTuple_GetItem(args, 0));
    auto ver = PyUnicode_AsUTF8(PyTuple_GetItem(args, 1));
    if (JitInit(path, ver)) {
        Py_RETURN_NONE;
    } else {
        return nullptr;
    }
}

static PyObject*
pyjion_config(PyObject* self, PyObject* args, PyObject* kwargs) {
    Py_ssize_t nargs = PyTuple_GET_SIZE(args);
    PyObject *pgc = nullptr, *level = nullptr, *debug = nullptr, *graph = nullptr, *threshold = nullptr;
    if (kwargs == nullptr) {
        goto return_result;
    }
    pgc = PyDict_GetItemString(kwargs, "pgc");
    if (pgc != nullptr) {
        // pgc
        if (!PyBool_Check(pgc)) {
            PyErr_SetString(PyExc_TypeError, "Expected bool for pgc flag");
            return nullptr;
        }
        g_pyjionSettings.pgc = pgc == Py_True ? true : false;
    }
    level = PyDict_GetItemString(kwargs, "level");
    if (level != nullptr) {
        // optimization level
        if (!PyLong_Check(level)) {
            PyErr_SetString(PyExc_TypeError, "Expected int for optimization level");
            return nullptr;
        }

        auto newLevel = PyLong_AsLong(level);
        if (newLevel < 0 || newLevel > 2) {
            PyErr_SetString(PyExc_ValueError, "Level not in range of 0-2");
            return nullptr;
        }
        setOptimizationLevel(newLevel);
    }
    debug = PyDict_GetItemString(kwargs, "debug");
    if (debug != nullptr) {
        // debug
        if (!PyBool_Check(debug)) {
            PyErr_SetString(PyExc_TypeError, "Expected bool for debug flag");
            return nullptr;
        }
        g_pyjionSettings.debug = debug == Py_True ? true : false;
    }
    graph = PyDict_GetItemString(kwargs, "graph");
    if (graph) {
        // graph
        if (!PyBool_Check(graph)) {
            PyErr_SetString(PyExc_TypeError, "Expected bool for graph flag");
            return nullptr;
        }
        g_pyjionSettings.graph = graph == Py_True ? true : false;
    }
    threshold = PyDict_GetItemString(kwargs, "threshold");
    if (threshold) {
        // threshold
        if (!PyLong_Check(threshold)) {
            PyErr_SetString(PyExc_TypeError, "Expected int for threshold level");
            return nullptr;
        }

        auto newThreshold = PyLong_AsLong(threshold);
        if (newThreshold < 0 || newThreshold > MAX_UINT8_T) {
            PyErr_SetString(PyExc_ValueError, "Threshold cannot be negative or exceed 255");
            return nullptr;
        }
        g_pyjionSettings.threshold = newThreshold;
    }

return_result:
    auto res = PyDict_New();
    if (res == nullptr) {
        return nullptr;
    }

    PyDict_SetItemString(res, "clrjitpath", PyUnicode_FromString(g_pyjionSettings.clrjitpath.c_str()));
    PyDict_SetItemString(res, "pgc", g_pyjionSettings.pgc ? Py_True : Py_False);
    PyDict_SetItemString(res, "graph", g_pyjionSettings.graph ? Py_True : Py_False);
    PyDict_SetItemString(res, "debug", g_pyjionSettings.debug ? Py_True : Py_False);
    PyDict_SetItemString(res, "level", PyLong_FromLong(g_pyjionSettings.optimizationLevel));
    PyDict_SetItemString(res, "threshold", PyLong_FromLong(g_pyjionSettings.threshold));

    return res;
}

static PyMethodDef PyjionMethods[] = {
        {"enable",
         pyjion_enable,
         METH_NOARGS,
         "Enable the JIT.  Returns True if the JIT was enabled, False if it was already enabled."},
        {"disable",
         pyjion_disable,
         METH_NOARGS,
         "Disable the JIT.  Returns True if the JIT was disabled, False if it was already disabled."},
        {"info",
         pyjion_info,
         METH_O,
         "Returns a dictionary describing information about a function or code objects current JIT status."},
        {"config",
         reinterpret_cast<PyCFunction>(pyjion_config),
         METH_VARARGS | METH_KEYWORDS,
         "Configure Pyjion runtime settings."},
        {"il",
         pyjion_dump_il,
         METH_O,
         "Outputs the IL for the compiled code object."},
        {"native",
         pyjion_dump_native,
         METH_O,
         "Outputs the machine code for the compiled code object."},
        {"offsets",
         pyjion_get_offsets,
         METH_O,
         "Get the sequence of offsets for IL and machine code for given python bytecodes."},
        {"graph",
         pyjion_get_graph,
         METH_O,
         "Fetch instruction graph for code object."},
        {"init",
         pyjion_init,
         METH_O,
         "Initialize JIT."},
        {"symbols",
         pyjion_symbols,
         METH_O,
         "Return a list of global symbols."

        },
        {nullptr, nullptr, 0, nullptr} /* Sentinel */
};

static struct PyModuleDef pyjionmodule = {
        PyModuleDef_HEAD_INIT,
        "_pyjion",                                      /* name of module */
        "Pyjion - A Just-in-Time Compiler for CPython", /* module documentation, may be NULL */
        -1,                                             /* size of per-interpreter state of the module,
			  or -1 if the module keeps state in global variables. */
        PyjionMethods};


PyObject* PyjionUnboxingError;
PyMODINIT_FUNC PyInit__pyjion(void) {
    // Install our frame evaluation function
    auto mod = PyModule_Create(&pyjionmodule);
    if (mod == nullptr)
        return nullptr;
    PyjionUnboxingError = PyErr_NewException("pyjion.PyjionUnboxingError", PyExc_ValueError, nullptr);
    int ret = PyModule_AddObject(mod, "PyjionUnboxingError", PyjionUnboxingError);
    if (ret != 0)
        return nullptr;

    Py_INCREF(&PyjionAssembly_Type);
    if (PyModule_AddObject(mod, "Assembly", (PyObject *) &PyjionAssembly_Type) < 0) {
        Py_DECREF(&PyjionAssembly_Type);
        Py_DECREF(mod);
        return nullptr;
    }

    Py_INCREF(&PyjionAssemblyType_Type);
    if (PyModule_AddObject(mod, "AssemblyType", (PyObject *) &PyjionAssemblyType_Type) < 0) {
        Py_DECREF(&PyjionAssemblyType_Type);
        Py_DECREF(mod);
        return nullptr;
    }

    return mod;
}
