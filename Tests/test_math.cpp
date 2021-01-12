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

/**
  Test JIT code emission.
*/

#include <catch2/catch.hpp>
#include <pyjitmath.h>
#include "testing_util.h"

TEST_CASE("Test inplace") {
    SECTION("inplace addition of multiple floats") {
        auto t = EmissionTest("def f():\n"
                              "  a = 2.0\n"
                              "  b = 3.0\n"
                              "  c = 4.0\n"
                              "  c += a * b\n"
                              "  return c");
        CHECK(t.returns() == "10.0");
    }

    SECTION("inplace addition of multiple ints") {
        auto t = EmissionTest("def f():\n"
                              "  a = 2\n"
                              "  b = 3\n"
                              "  c = 4\n"
                              "  c += a * b\n"
                              "  return c");
        CHECK(t.returns() == "10");
    }

    SECTION("inplace addition of two floats assigned to an int") {
        auto t = EmissionTest("def f():\n"
                              "  a = 2.0\n"
                              "  b = 3.0\n"
                              "  c = 4\n"
                              "  c += a * b\n"
                              "  return c");
        CHECK(t.returns() == "10.0");
    }

    SECTION("inplace addition of two ints assigned to a float") {
        auto t = EmissionTest("def f():\n"
                              "  a = 2\n"
                              "  b = 3\n"
                              "  c = 4.0\n"
                              "  c += a * b\n"
                              "  return c");
        CHECK(t.returns() == "10.0");
    }

    SECTION("inplace multiplication of two ints assigned to an int") {
        auto t = EmissionTest("def f():\n"
                              "  a = 5\n"
                              "  b = 3\n"
                              "  c = 4\n"
                              "  c *= a - b\n"
                              "  return c");
        CHECK(t.returns() == "8");
    }
    SECTION("inplace addition of two strs ") {
        auto t = EmissionTest("def f():\n"
                              "  a = 'a'\n"
                              "  b = 'b'\n"
                              "  c = 'c'\n"
                              "  c += a + b\n"
                              "  return c");
        CHECK(t.returns() == "'cab'");
    }
}

TEST_CASE("Test math functions directly"){
    SECTION("Binary then inplace all floats") {
        auto firstOpcode = GENERATE(BINARY_TRUE_DIVIDE, BINARY_FLOOR_DIVIDE, BINARY_POWER, BINARY_MULTIPLY, BINARY_SUBTRACT, BINARY_ADD);
        auto secondOpcode = GENERATE(BINARY_TRUE_DIVIDE, BINARY_FLOOR_DIVIDE, BINARY_POWER, BINARY_MULTIPLY, BINARY_SUBTRACT, BINARY_ADD, INPLACE_POWER, INPLACE_MULTIPLY, INPLACE_TRUE_DIVIDE, INPLACE_FLOOR_DIVIDE, INPLACE_ADD, INPLACE_SUBTRACT);
        printf("Float %d by %d", firstOpcode, secondOpcode);
        PyObject* a = PyFloat_FromDouble(6.0);
        PyObject* b = PyFloat_FromDouble(2.0);
        PyObject* c = PyFloat_FromDouble(4.0);
        Py_INCREF(a);
        Py_INCREF(b);
        Py_INCREF(c);

        PyObject* res = PyJitMath_TripleBinaryOp(c, a, b, firstOpcode, secondOpcode);
        REQUIRE(res != nullptr);
        if (firstOpcode == BINARY_TRUE_DIVIDE)
            CHECK(PyFloat_Check(res));
        if (secondOpcode == BINARY_TRUE_DIVIDE || secondOpcode == INPLACE_TRUE_DIVIDE)
            CHECK(PyFloat_Check(res));
        CHECK(a->ob_refcnt == 1);
        CHECK(b->ob_refcnt == 1);
        CHECK(c->ob_refcnt == 1);
    }

    SECTION("Binary then inplace all ints") {
        auto firstOpcode = GENERATE(BINARY_TRUE_DIVIDE, BINARY_FLOOR_DIVIDE, BINARY_POWER, BINARY_MULTIPLY, BINARY_SUBTRACT, BINARY_ADD);
        auto secondOpcode = GENERATE(BINARY_TRUE_DIVIDE, BINARY_FLOOR_DIVIDE, BINARY_POWER, BINARY_MULTIPLY, BINARY_SUBTRACT, BINARY_ADD, INPLACE_POWER, INPLACE_MULTIPLY, INPLACE_TRUE_DIVIDE, INPLACE_FLOOR_DIVIDE, INPLACE_ADD, INPLACE_SUBTRACT);
        printf("int %d by %d", firstOpcode, secondOpcode);
        PyObject* a = PyLong_FromLong(300);
        PyObject* b = PyLong_FromLong(301);
        PyObject* c = PyLong_FromLong(302);
        Py_INCREF(a);
        Py_INCREF(b);
        Py_INCREF(c);

        PyObject* res = PyJitMath_TripleBinaryOp(c, a, b, firstOpcode, secondOpcode);
        REQUIRE(res != nullptr);
        if (firstOpcode == BINARY_TRUE_DIVIDE)
            CHECK(PyFloat_Check(res));
        if (secondOpcode == BINARY_TRUE_DIVIDE || secondOpcode == INPLACE_TRUE_DIVIDE)
            CHECK(PyFloat_Check(res));
        CHECK(res != nullptr);
    }

    SECTION("Binary then inplace int float float") {
        auto firstOpcode = GENERATE(BINARY_TRUE_DIVIDE, BINARY_FLOOR_DIVIDE, BINARY_POWER, BINARY_MULTIPLY, BINARY_SUBTRACT, BINARY_ADD);
        auto secondOpcode = GENERATE(BINARY_TRUE_DIVIDE, BINARY_FLOOR_DIVIDE, BINARY_POWER, BINARY_MULTIPLY, BINARY_SUBTRACT, BINARY_ADD, INPLACE_MULTIPLY, INPLACE_TRUE_DIVIDE, INPLACE_FLOOR_DIVIDE, INPLACE_ADD, INPLACE_SUBTRACT);
        printf("int/float/float %d by %d", firstOpcode, secondOpcode);
        PyObject* a = PyLong_FromLong(300);
        PyObject* b = PyFloat_FromDouble(300.0);
        PyObject* c = PyFloat_FromDouble(400.0);
        Py_INCREF(a);
        Py_INCREF(b);
        Py_INCREF(c);

        PyObject* res = PyJitMath_TripleBinaryOp(c, a, b, firstOpcode, secondOpcode);
        REQUIRE(res != nullptr);
        if (firstOpcode == BINARY_TRUE_DIVIDE)
            CHECK(PyFloat_Check(res));
        if (secondOpcode == BINARY_TRUE_DIVIDE || secondOpcode == INPLACE_TRUE_DIVIDE)
            CHECK(PyFloat_Check(res));
        CHECK(a->ob_refcnt == 1);
        CHECK(b->ob_refcnt == 1);
        CHECK(c->ob_refcnt == 1);
    }

    SECTION("Binary then inplace float int int") {
        auto firstOpcode = GENERATE(BINARY_TRUE_DIVIDE, BINARY_FLOOR_DIVIDE, BINARY_POWER, BINARY_MULTIPLY, BINARY_SUBTRACT, BINARY_ADD);
        auto secondOpcode = GENERATE(BINARY_TRUE_DIVIDE, BINARY_FLOOR_DIVIDE, BINARY_POWER, BINARY_MULTIPLY, BINARY_SUBTRACT, BINARY_ADD, INPLACE_POWER, INPLACE_MULTIPLY, INPLACE_TRUE_DIVIDE, INPLACE_FLOOR_DIVIDE, INPLACE_ADD, INPLACE_SUBTRACT);
        printf("int/float/float %d by %d", firstOpcode, secondOpcode);
        PyObject* a = PyFloat_FromDouble(600.0);
        PyObject* b = PyLong_FromLong(300);
        PyObject* c = PyLong_FromLong(400);
        Py_INCREF(a);
        Py_INCREF(b);
        Py_INCREF(c);

        PyObject* res = PyJitMath_TripleBinaryOp(c, a, b, firstOpcode, secondOpcode);
        REQUIRE(res != nullptr);
        if (firstOpcode == BINARY_TRUE_DIVIDE)
            CHECK(PyFloat_Check(res));
        if (secondOpcode == BINARY_TRUE_DIVIDE || secondOpcode == INPLACE_TRUE_DIVIDE)
            CHECK(PyFloat_Check(res));
        CHECK(a->ob_refcnt == 1);
        CHECK(b->ob_refcnt == 1);
        CHECK(c->ob_refcnt == 1);
    }

    SECTION("Binary then inplace all ints") {
        // Dont test the power operation here because it will cause havoc with these numbers!!
        auto firstOpcode = GENERATE(BINARY_TRUE_DIVIDE, BINARY_FLOOR_DIVIDE, BINARY_MULTIPLY, BINARY_SUBTRACT, BINARY_ADD);
        auto secondOpcode = GENERATE(BINARY_TRUE_DIVIDE, BINARY_FLOOR_DIVIDE, BINARY_MULTIPLY, BINARY_SUBTRACT, BINARY_ADD, INPLACE_POWER, INPLACE_MULTIPLY, INPLACE_TRUE_DIVIDE, INPLACE_FLOOR_DIVIDE, INPLACE_ADD, INPLACE_SUBTRACT);
        printf("int %d by %d", firstOpcode, secondOpcode);
        PyObject* a = PyLong_FromLong(600);
        PyObject* b = PyLong_FromLong(300);
        PyObject* c = PyLong_FromLong(402);
        Py_INCREF(a);
        Py_INCREF(b);
        Py_INCREF(c);

        PyObject* res = PyJitMath_TripleBinaryOp(c, a, b, firstOpcode, secondOpcode);
        REQUIRE(res != nullptr);
        if (firstOpcode == BINARY_TRUE_DIVIDE)
            CHECK(PyFloat_Check(res));
        if (secondOpcode == BINARY_TRUE_DIVIDE || secondOpcode == INPLACE_TRUE_DIVIDE)
            CHECK(PyFloat_Check(res));
        CHECK(a->ob_refcnt == 1);
        CHECK(b->ob_refcnt == 1);
        CHECK(c->ob_refcnt == 1);
    }

    SECTION("Binary then inplace all strings") {
        auto firstOpcode = GENERATE(BINARY_ADD);
        auto secondOpcode = GENERATE(BINARY_ADD, INPLACE_ADD);

        PyObject* a = PyUnicode_FromString("123");
        PyObject* b = PyUnicode_FromString("1234");
        PyObject* c = PyUnicode_FromString("12345");
        Py_INCREF(a);
        Py_INCREF(b);
        Py_INCREF(c);

        PyObject* res = PyJitMath_TripleBinaryOp(c, a, b, firstOpcode, secondOpcode);
        REQUIRE(res != nullptr);
        CHECK(a->ob_refcnt == 1);
        CHECK(b->ob_refcnt == 1);
        CHECK(c->ob_refcnt == 1);
    }
}