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


class PgcProfilingTest {
private:
    py_ptr <PyCodeObject> m_code;
    py_ptr <PyjionJittedCode> m_jittedcode;
    PyjionCodeProfile* profile;

    PyObject *run() {
        auto sysModule = PyObject_ptr(PyImport_ImportModule("sys"));
        auto globals = PyObject_ptr(PyDict_New());
        auto builtins = PyEval_GetBuiltins();
        PyDict_SetItemString(globals.get(), "__builtins__", builtins);
        PyDict_SetItemString(globals.get(), "sys", sysModule.get());

        // Don't DECREF as frames are recycled.
        auto tstate = PyThreadState_Get();
        auto frame = PyFrame_New(tstate, m_code.get(), globals.get(), PyObject_ptr(PyDict_New()).get());
        auto prev = _PyInterpreterState_GetEvalFrameFunc(PyInterpreterState_Main());
        _PyInterpreterState_SetEvalFrameFunc(PyInterpreterState_Main(), PyJit_EvalFrame);
        m_jittedcode->j_profile = profile;
        auto res = PyJit_EvalFrame(tstate, frame, 0);

        _PyInterpreterState_SetEvalFrameFunc(PyInterpreterState_Main(), prev);
        //Py_DECREF(frame);
        size_t collected = PyGC_Collect();
        printf("Collected %zu values\n", collected);
        REQUIRE(!m_jittedcode->j_failed);
        return res;
    }

public:
    explicit PgcProfilingTest(const char *code) {
        PyErr_Clear();
        profile = new PyjionCodeProfile();
        m_code.reset(CompileCode(code));
        if (m_code.get() == nullptr) {
            FAIL("failed to compile code");
        }
        auto jitted = PyJit_EnsureExtra((PyObject *) *m_code);
        m_jittedcode.reset(jitted);
    }

    ~PgcProfilingTest(){
        //delete profile;
    }

    std::string returns() {
        auto res = PyObject_ptr(run());
        REQUIRE(res.get() != nullptr);
        if (PyErr_Occurred()) {
            PyErr_PrintEx(-1);
            FAIL("Error on Python execution");
            return nullptr;
        }
        PyObject* v = res.get();
        auto repr = PyUnicode_AsUTF8(PyObject_Repr(v));
        auto tstate = PyThreadState_GET();
        REQUIRE(tstate->curexc_value == nullptr);
        REQUIRE(tstate->curexc_traceback == nullptr);
        if (tstate->curexc_type != nullptr) {
            REQUIRE(tstate->curexc_type == Py_None);
        }

        return std::string(repr);
    }

    PyObject* ret(){
        return run();
    }

    PyObject *raises() {
        auto res = run();
        REQUIRE(res == nullptr);
        auto excType = PyErr_Occurred();
        PyErr_Print();
        PyErr_Clear();
        return excType;
    }

    bool profileEquals(int position, int stackPosition, PyTypeObject* pyType) {
        return profile->getType(position, stackPosition) == pyType;
    }

    PgcStatus pgcStatus() {
        return m_jittedcode->j_pgc_status;
    }
};

TEST_CASE("test most simple application"){
    SECTION("test simple") {
        auto t = PgcProfilingTest(
                "def f():\n  a = 1\n  b = 2.0\n  c=3\n  return a + b + c\n"
        );
        CHECK(t.pgcStatus() == PgcStatus::Uncompiled);
        CHECK(t.returns() == "6.0");
        CHECK(t.pgcStatus() == PgcStatus::CompiledWithProbes);
        CHECK(t.profileEquals(16, 0, &PyFloat_Type)); // right
        CHECK(t.profileEquals(16, 1, &PyLong_Type)); // left
        CHECK(t.profileEquals(20, 0, &PyLong_Type)); // right
        CHECK(t.profileEquals(20, 1, &PyFloat_Type)); // left
        CHECK(t.returns() == "6.0");
        CHECK(t.pgcStatus() == PgcStatus::Optimized);
    };
    SECTION("test changing types") {
        auto t = PgcProfilingTest(
                "def f():\n"
                "  a = 1000\n"
                "  b = 2.0\n"
                "  c = 'cheese'\n"
                "  d = ' shop'\n"
                "  def add(left,right):\n"
                "     return left + right\n"
                "  v = str(add(a, b)) + add(c, d)\n"
                "  return a,b,c,d\n"
        );
        CHECK(t.pgcStatus() == PgcStatus::Uncompiled);
        CHECK(t.returns() == "(1000, 2.0, 'cheese', ' shop')");
        CHECK(t.pgcStatus() == PgcStatus::CompiledWithProbes);
        CHECK(t.returns() == "(1000, 2.0, 'cheese', ' shop')");
        CHECK(t.pgcStatus() == PgcStatus::Optimized);
    };

    SECTION("test changing types for COMPARE_OP") {
        auto t = PgcProfilingTest(
                "def f():\n"
                "  a = 1000\n"
                "  b = 2.0\n"
                "  c = 'cheese'\n"
                "  d = ' shop'\n"
                "  def equal(left,right):\n"
                "     return left == right\n"
                "  return equal(a,b), equal (c,d), equal(a, d)\n"
        );
        CHECK(t.pgcStatus() == PgcStatus::Uncompiled);
        CHECK(t.returns() == "(False, False, False)");
        CHECK(t.pgcStatus() == PgcStatus::CompiledWithProbes);
        CHECK(t.returns() == "(False, False, False)");
        CHECK(t.pgcStatus() == PgcStatus::Optimized);
    };
}

TEST_CASE("test UNPACK_SEQUENCE PGC") {
    SECTION("test simple") {
        auto t = PgcProfilingTest(
                "def f():\n  a, b, c = ['a', 'b', 'c']\n  return a, b, c"
        );
        CHECK(t.pgcStatus() == PgcStatus::Uncompiled);
        CHECK(t.returns() == "('a', 'b', 'c')");
        CHECK(t.pgcStatus() == PgcStatus::CompiledWithProbes);
        CHECK(t.profileEquals(6, 0, &PyList_Type));
        CHECK(t.returns() == "('a', 'b', 'c')");
        CHECK(t.pgcStatus() == PgcStatus::Optimized);
    };
    SECTION("test for_iter stacked"){
        auto t = PgcProfilingTest(
                "def f():\n"
                "  x = [(1,2), (3,4)]\n"
                "  results = []\n"
                "  for i, j in x:\n"
                "    results.append(i); results.append(j)\n"
                "  return results\n"
        );
        CHECK(t.pgcStatus() == PgcStatus::Uncompiled);
        CHECK(t.returns() == "[1, 2, 3, 4]");
        CHECK(t.pgcStatus() == PgcStatus::CompiledWithProbes);
        CHECK(t.profileEquals(18, 0, &PyTuple_Type));
        CHECK(t.returns() == "[1, 2, 3, 4]");
        CHECK(t.pgcStatus() == PgcStatus::Optimized);
    }

    SECTION("test changed types"){
        auto t = PgcProfilingTest(
                "def f():\n"
                "  results = []\n"
                "  def x(it):\n"
                "    a, b = it\n"
                "    return int(a) + int(b)\n"
                "  return x((1,2)) + x([3, 4]) + x('56')\n"
        );
        CHECK(t.pgcStatus() == PgcStatus::Uncompiled);
        CHECK(t.returns() == "21");
        CHECK(t.pgcStatus() == PgcStatus::CompiledWithProbes);
        CHECK(t.returns() == "21");
        CHECK(t.pgcStatus() == PgcStatus::Optimized);
    }
}