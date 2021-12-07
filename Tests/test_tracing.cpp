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

#include <catch2/catch.hpp>
#include "testing_util.h"
#include <Python.h>
#include <frameobject.h>
#include <util.h>
#include <pyjit.h>


int TestTraceFunc(PyObject* state, PyFrameObject* frame, int type, PyObject* arg) {
    switch (type) {
        case PyTrace_CALL:
            CHECK(arg == Py_None);
            break;
        case PyTrace_EXCEPTION:
            CHECK(arg != Py_None);
            break;
        case PyTrace_LINE:
            CHECK(arg == Py_None);
            break;
        case PyTrace_RETURN:
            CHECK(arg != Py_None);
            CHECK(PyLong_AsLong(arg) == 6);
            break;
        case PyTrace_C_CALL:
            CHECK(arg != Py_None);
            break;
        case PyTrace_C_EXCEPTION:
            CHECK(arg != Py_None);
            break;
        case PyTrace_C_RETURN:
            CHECK(arg != Py_None);
            break;
        default:
            FAIL("Unexpected type");
            break;
    }
    PyDict_SetItem(state, PyLong_FromLong(type), Py_True);
    return 0;
}

int TestProfileFunc(PyObject* state, PyFrameObject* frame, int type, PyObject* arg) {
    switch (type) {
        case PyTrace_CALL:
            CHECK(arg == Py_None);
            break;
        case PyTrace_EXCEPTION:
            CHECK(arg != Py_None);
            break;
        case PyTrace_LINE:
            CHECK(arg == Py_None);
            break;
        case PyTrace_RETURN:
            CHECK(arg != Py_None);
            CHECK(PyLong_AsLong(arg) == 6);
            break;
        case PyTrace_C_CALL:
            CHECK(arg != Py_None);
            break;
        case PyTrace_C_EXCEPTION:
            CHECK(arg != Py_None);
            break;
        case PyTrace_C_RETURN:
            CHECK(arg != Py_None);
            break;
        default:
            FAIL("Unexpected type");
            break;
    }
    PyDict_SetItem(state, PyLong_FromLong(type), Py_True);
    return 0;
}


class TracingTest {
private:
    py_ptr<PyCodeObject> m_code;
    PyjionJittedCode* m_jittedcode;
    PyObject_ptr m_recordedTraces = PyObject_ptr(PyDict_New());
    PyObject_ptr m_recordedProfiles = PyObject_ptr(PyDict_New());

    PyObject* run() {
        auto sysModule = PyObject_ptr(PyImport_ImportModule("sys"));
        auto globals = PyObject_ptr(PyDict_New());
        auto builtins = PyEval_GetBuiltins();
        PyDict_SetItemString(globals.get(), "__builtins__", builtins);
        PyDict_SetItemString(globals.get(), "sys", sysModule.get());
        auto tstate = PyThreadState_Get();
        _PyEval_SetTrace(tstate, &TestTraceFunc, m_recordedTraces.get());
        _PyEval_SetProfile(tstate, &TestProfileFunc, m_recordedProfiles.get());
        auto frame = PyFrame_New(tstate, m_code.get(), globals.get(), PyObject_ptr(PyDict_New()).get());
        auto prev = _PyInterpreterState_GetEvalFrameFunc(PyInterpreterState_Main());
        _PyInterpreterState_SetEvalFrameFunc(PyInterpreterState_Main(), PyJit_EvalFrame);

        auto res = PyJit_ExecuteAndCompileFrame(m_jittedcode, frame, tstate, nullptr);
        _PyEval_SetTrace(tstate, nullptr, nullptr);
        _PyEval_SetProfile(tstate, nullptr, nullptr);
        _PyInterpreterState_SetEvalFrameFunc(PyInterpreterState_Main(), prev);
        Py_DECREF(frame);
        size_t collected = PyGC_Collect();
        REQUIRE(!m_jittedcode->j_failed);
        REQUIRE(m_jittedcode->j_genericAddr != nullptr);
        CHECK(m_jittedcode->j_tracingHooks);
        CHECK(m_jittedcode->j_profilingHooks);
        return res;
    }

public:
    explicit TracingTest(const char* code) {
        PyErr_Clear();
        m_code.reset(CompileCode(code));
        if (m_code.get() == nullptr) {
            FAIL("failed to compile code");
        }
        auto jitted = PyJit_EnsureExtra((PyObject*) *m_code);

        m_jittedcode = jitted;
    }

    std::string returns() {
        auto res = PyObject_ptr(run());
        REQUIRE(res.get() != nullptr);
        if (PyErr_Occurred()) {
            PyErr_PrintEx(-1);
            FAIL("Error on Python execution");
            return "";
        }

        auto repr = PyUnicode_AsUTF8(PyObject_Repr(res.get()));
        auto tstate = PyThreadState_GET();
        REQUIRE(tstate->curexc_value == nullptr);
        REQUIRE(tstate->curexc_traceback == nullptr);
        if (tstate->curexc_type != nullptr) {
            REQUIRE(tstate->curexc_type == Py_None);
        }

        return {repr};
    }

    PyObject* raises() {
        auto res = run();
        REQUIRE(res == nullptr);
        auto excType = PyErr_Occurred();
#ifdef DEBUG_VERBOSE
        PyErr_Print();
#endif
        PyErr_Clear();
        return excType;
    }

    bool traced(int ty) {
        auto item = PyDict_GetItem(m_recordedTraces.get(), PyLong_FromLong(ty));
        if (item == nullptr)
            return false;
        return item == Py_True;
    }

    bool profiled(int ty) {
        auto item = PyDict_GetItem(m_recordedProfiles.get(), PyLong_FromLong(ty));
        if (item == nullptr)
            return false;
        return item == Py_True;
    }
};

TEST_CASE("test tracing hooks") {
    SECTION("test simple") {
        auto t = TracingTest(
                "def f():\n"
                "  \n"
                "  a = 1\n"
                "  b = 2\n"
                "  c = 3\n"
                "  return a + b + c\n");
        CHECK(t.returns() == "6");
        CHECK(t.traced(PyTrace_CALL));
        CHECK(t.traced(PyTrace_RETURN));
        CHECK(t.profiled(PyTrace_CALL));
        CHECK(t.profiled(PyTrace_RETURN));
    };

    SECTION("test calling C function") {
        auto t = TracingTest(
                "def f():\n"
                "  \n"
                "  a = 1\n"
                "  b = 2\n"
                "  c = 3\n"
                "  print(a + b + c)\n"
                "  return a + b + c\n");
        CHECK(t.returns() == "6");
        CHECK(t.traced(PyTrace_CALL));
        CHECK(t.traced(PyTrace_RETURN));
        CHECK(t.profiled(PyTrace_CALL));
        CHECK(t.profiled(PyTrace_RETURN));
        CHECK(t.profiled(PyTrace_C_CALL));
        CHECK(t.profiled(PyTrace_C_RETURN));
    };
}