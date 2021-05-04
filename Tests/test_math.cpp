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

template<typename T>
void incref(T v) {
    Py_INCREF(v);
}

template<typename T, typename... Args>
void incref(T v, Args... args) {
    Py_INCREF(v) ; incref(args...);
}

TEST_CASE("Test math errors") {
    SECTION("test2") {
        auto t = EmissionTest(
                "def f(): 1.0 / 0"
        );
        CHECK(t.raises() == PyExc_ZeroDivisionError);
    }
}

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
        incref(a,b,c);

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

    SECTION("Binary then inplace int float float") {
        auto firstOpcode = GENERATE(BINARY_TRUE_DIVIDE, BINARY_FLOOR_DIVIDE, BINARY_POWER, BINARY_MULTIPLY, BINARY_SUBTRACT, BINARY_ADD);
        auto secondOpcode = GENERATE(BINARY_TRUE_DIVIDE, BINARY_FLOOR_DIVIDE, BINARY_POWER, BINARY_MULTIPLY, BINARY_SUBTRACT, BINARY_ADD, INPLACE_MULTIPLY, INPLACE_TRUE_DIVIDE, INPLACE_FLOOR_DIVIDE, INPLACE_ADD, INPLACE_SUBTRACT);
        printf("int/float/float %d by %d", firstOpcode, secondOpcode);
        PyObject* a = PyLong_FromLong(6);
        PyObject* b = PyFloat_FromDouble(3.0);
        PyObject* c = PyFloat_FromDouble(40.0);
        incref(a,b,c);

        PyObject* res = PyJitMath_TripleBinaryOp(c, a, b, firstOpcode, secondOpcode);
        REQUIRE(res != nullptr);
        if (firstOpcode == BINARY_TRUE_DIVIDE)
            CHECK(PyFloat_Check(res));
        if (secondOpcode == BINARY_TRUE_DIVIDE || secondOpcode == INPLACE_TRUE_DIVIDE)
            CHECK(PyFloat_Check(res));
    }

    SECTION("Binary then inplace float int int") {
        auto firstOpcode = GENERATE(BINARY_TRUE_DIVIDE, BINARY_FLOOR_DIVIDE, BINARY_POWER, BINARY_MULTIPLY, BINARY_SUBTRACT, BINARY_ADD);
        auto secondOpcode = GENERATE(BINARY_TRUE_DIVIDE, BINARY_FLOOR_DIVIDE, BINARY_POWER, BINARY_MULTIPLY, BINARY_SUBTRACT, BINARY_ADD, INPLACE_POWER, INPLACE_MULTIPLY, INPLACE_TRUE_DIVIDE, INPLACE_FLOOR_DIVIDE, INPLACE_ADD, INPLACE_SUBTRACT);
        printf("int/float/float %d by %d", firstOpcode, secondOpcode);
        PyObject* a = PyFloat_FromDouble(600.0);
        PyObject* b = PyLong_FromLong(30);
        PyObject* c = PyLong_FromLong(40);
        incref(a,b,c);

        PyObject* res = PyJitMath_TripleBinaryOp(c, a, b, firstOpcode, secondOpcode);
        REQUIRE(res != nullptr);
        if (firstOpcode == BINARY_TRUE_DIVIDE)
            CHECK(PyFloat_Check(res));
        if (secondOpcode == BINARY_TRUE_DIVIDE || secondOpcode == INPLACE_TRUE_DIVIDE)
            CHECK(PyFloat_Check(res));
    }

    SECTION("Binary then inplace all ints") {
        // Dont test the power operation here because it will cause havoc with these numbers!!
        auto firstOpcode = GENERATE(BINARY_TRUE_DIVIDE);
        auto secondOpcode = GENERATE(BINARY_TRUE_DIVIDE, BINARY_FLOOR_DIVIDE, BINARY_MULTIPLY, BINARY_SUBTRACT, BINARY_ADD, INPLACE_POWER, INPLACE_MULTIPLY, INPLACE_TRUE_DIVIDE, INPLACE_FLOOR_DIVIDE, INPLACE_ADD, INPLACE_SUBTRACT);
        printf("int %d by %d", firstOpcode, secondOpcode);
        PyObject* a = PyLong_FromLong(6);
        PyObject* b = PyLong_FromLong(3);
        PyObject* c = PyLong_FromLong(12);
        incref(a,b,c);

        PyObject* res = PyJitMath_TripleBinaryOp(c, a, b, firstOpcode, secondOpcode);
        REQUIRE(res != nullptr);
        if (firstOpcode == BINARY_TRUE_DIVIDE)
            CHECK(PyFloat_Check(res));
        if (secondOpcode == BINARY_TRUE_DIVIDE || secondOpcode == INPLACE_TRUE_DIVIDE)
            CHECK(PyFloat_Check(res));
    }

    SECTION("Binary then inplace all strings") {
        auto firstOpcode = GENERATE(BINARY_ADD);
        auto secondOpcode = GENERATE(BINARY_ADD, INPLACE_ADD);

        PyObject* a = PyUnicode_FromString("123");
        PyObject* b = PyUnicode_FromString("1234");
        PyObject* c = PyUnicode_FromString("12345");
        incref(a,b,c);

        PyObject* res = PyJitMath_TripleBinaryOp(c, a, b, firstOpcode, secondOpcode);
        REQUIRE(res != nullptr);
    }
}

TEST_CASE("Unary tests") {
    SECTION("most basic unary not") {
        auto t = EmissionTest(
                "def f():\n  x=True\n  return not x\n"
        );
        CHECK(t.returns() == "False");
    }
    SECTION("in place add") {
        auto t = EmissionTest(
                "def f():\n  x=1\n  x+=1\n  return x"
        );
        CHECK(t.returns() == "2");
    }
    SECTION("simple add") {
        auto t = EmissionTest(
                "def f():\n  x=1\n  y=2\n  z = x+y\n  return z"
        );
        CHECK(t.returns() == "3");
    }
    SECTION("test1") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    x += 1\n    x -= 1\n    y = not x\n    return y"
        );
        CHECK(t.returns() == "False");
    }SECTION("test2") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    x += 1\n    x -= 1\n    if x:\n        return True\n    return False"
        );
        CHECK(t.returns() == "True");
    }SECTION("test3") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    x += 1\n    x -= 1\n    x -= 4611686018427387903\n    if x:\n        return True\n    return False"
        );
        CHECK(t.returns() == "False");
    }

    SECTION("test4") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    x += 1\n    x -= 1\n    if not x:\n        return True\n    return False"
        );
        CHECK(t.returns() == "False");
    }SECTION("test5") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    x += 1\n    x -= 1\n    x -= 4611686018427387903\n    if not x:\n        return True\n    return False"
        );
        CHECK(t.returns() == "True");
    }

    SECTION("test6") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    x += 1\n    x -= 1\n    x -= 4611686018427387903\n    y = not x\n    return y"
        );
        CHECK(t.returns() == "True");
    }

    SECTION("test7") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    x -= 1\n    if x == y:\n        return True\n    return False"
        );
        CHECK(t.returns() == "True");
    }SECTION("test8") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    x -= 1\n    if x <= y:\n        return True\n    return False"
        );
        CHECK(t.returns() == "True");
    }SECTION("test9") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    x -= 1\n    if x >= y:\n        return True\n    return False"
        );
        CHECK(t.returns() == "True");
    }SECTION("test10") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    x -= 1\n    if x != y:\n        return True\n    return False"
        );
        CHECK(t.returns() == "False");
    }SECTION("test11") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    x -= 1\n    if x < y:\n        return True\n    return False"
        );
        CHECK(t.returns() == "False");
    }SECTION("test12") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    x -= 1\n    if x > y:\n        return True\n    return False"
        );
        CHECK(t.returns() == "False");
    }SECTION("test13") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    if x < y:\n        return True\n    return False"
        );
        CHECK(t.returns() == "False");
    }SECTION("test14") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    if x > y:\n        return True\n    return False"
        );
        CHECK(t.returns() == "True");
    }SECTION("test15") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    y += 1\n    if x < y:\n        return True\n    return False"
        );
        CHECK(t.returns() == "True");
    }SECTION("test16") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    y += 1\n    if x > y:\n        return True\n    return False"
        );
        CHECK(t.returns() == "False");
    }

    SECTION("test17") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x == y"
        );
        CHECK(t.returns() == "True");
    }SECTION("test18") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    x -= 1\n    return x == y"
        );
        CHECK(t.returns() == "True");
    }SECTION("test19") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    y += 1\n    y -= 1\n    return x == y"
        );
        CHECK(t.returns() == "True");
    }SECTION("test20") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    return x == y"
        );
        CHECK(t.returns() == "False");
    }SECTION("test21") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    y += 1\n    return x == y"
        );
        CHECK(t.returns() == "False");
    }SECTION("test22") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 1\n    return x == y"
        );
        CHECK(t.returns() == "True");
    }

    SECTION("test23") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x != y"
        );
        CHECK(t.returns() == "False");
    }SECTION("test24") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    x -= 1\n    return x != y"
        );
        CHECK(t.returns() == "False");
    }SECTION("test25") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    y += 1\n    y -= 1\n    return x != y"
        );
        CHECK(t.returns() == "False");
    }SECTION("test26") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    return x != y"
        );
        CHECK(t.returns() == "True");
    }SECTION("test27") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    y += 1\n    return x != y"
        );
        CHECK(t.returns() == "True");
    }SECTION("test28") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 1\n    return x != y"
        );
        CHECK(t.returns() == "False");
    }

    SECTION("test29") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x >= y"
        );
        CHECK(t.returns() == "True");
    }SECTION("test30") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    x -= 1\n    return x >= y"
        );
        CHECK(t.returns() == "True");
    }SECTION("test31") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    y += 1\n    y -= 1\n    return x >= y"
        );
        CHECK(t.returns() == "True");
    }SECTION("test32") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 1\n    return x >= y"
        );
        CHECK(t.returns() == "True");
    }

    SECTION("test33") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x <= y"
        );
        CHECK(t.returns() == "True");
    }SECTION("test34") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    x -= 1\n    return x <= y"
        );
        CHECK(t.returns() == "True");
    }SECTION("test35") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    y += 1\n    y -= 1\n    return x <= y"
        );
        CHECK(t.returns() == "True");
    }SECTION("test36") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 1\n    return x <= y"
        );
        CHECK(t.returns() == "True");
    }

    SECTION("test37") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x > y"
        );
        CHECK(t.returns() == "False");
    }SECTION("test38") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775808\n    y = 9223372036854775807\n    return x > y"
        );
        CHECK(t.returns() == "True");
    }SECTION("test39") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775808\n    return x > y"
        );
        CHECK(t.returns() == "False");
    }SECTION("test40") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    return x > y"
        );
        CHECK(t.returns() == "True");
    }SECTION("test41") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    y += 1\n    return x > y"
        );
        CHECK(t.returns() == "False");
    }

    SECTION("test42") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x < y"
        );
        CHECK(t.returns() == "False");
    }SECTION("test43") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775808\n    y = 9223372036854775807\n    return x < y"
        );
        CHECK(t.returns() == "False");
    }SECTION("test44") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775808\n    return x < y"
        );
        CHECK(t.returns() == "True");
    }SECTION("test45") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    return x < y"
        );
        CHECK(t.returns() == "False");
    }SECTION("test46") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    y += 1\n    return x < y"
        );
        CHECK(t.returns() == "True");
    }

    SECTION("test47") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 1\n    return x == y"
        );
        CHECK(t.returns() == "True");
    }SECTION("test48") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 9223372036854775807\n    return x % y"
        );
        CHECK(t.returns() == "1");
    }

    SECTION("test49") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 2\n    return x / y"
        );
        CHECK(t.returns() == "0.5");
    }SECTION("test50") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 4611686018427387903\n    return x / y"
        );
        CHECK(t.returns() == "2.168404344971009e-19");
    }SECTION("test51") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 9223372036854775807\n    return x / y"
        );
        CHECK(t.returns() == "1.0842021724855044e-19");
    }SECTION("test52") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 1\n    return x / y"
        );
        CHECK(t.returns() == "4.611686018427388e+18");
    }SECTION("test53") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 1\n    return x / y"
        );
        CHECK(t.returns() == "9.223372036854776e+18");
    }SECTION("test54") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x / y"
        );
        CHECK(t.returns() == "1.0");
    }

    SECTION("test55") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 2\n    return x >> y"
        );
        CHECK(t.returns() == "0");
    }SECTION("test56") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 4611686018427387903\n    return x >> y"
        );
        CHECK(t.returns() == "0");
    }SECTION("test57") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 9223372036854775807\n    return x >> y"
        );
        CHECK(t.returns() == "0");
    }SECTION("test58") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 1\n    return x >> y"
        );
        CHECK(t.returns() == "2305843009213693951");
    }SECTION("test59") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 1\n    return x >> y"
        );
        CHECK(t.returns() == "4611686018427387903");
    }SECTION("test60") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x >> y"
        );
        CHECK(t.returns() == "0");
    }

    SECTION("test61") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 2\n    return x << y"
        );
        CHECK(t.returns() == "4");
    }SECTION("test62") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 32\n    return x << y"
        );
        CHECK(t.returns() == "4294967296");
    }SECTION("test63") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 62\n    return x << y"
        );
        CHECK(t.returns() == "4611686018427387904");
    }SECTION("test64") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 63\n    return x << y"
        );
        CHECK(t.returns() == "9223372036854775808");
    }SECTION("test65") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 64\n    return x << y"
        );
        CHECK(t.returns() == "18446744073709551616");
    }SECTION("test66") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 1\n    return x << y"
        );
        CHECK(t.returns() == "9223372036854775806");
    }SECTION("test67") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 1\n    return x << y"
        );
        CHECK(t.returns() == "18446744073709551614");
    }SECTION("test68") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x << y"
        );
        CHECK(t.raises() == PyExc_MemoryError);
    }

    SECTION("test69") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 2\n    return x ** y"
        );
        CHECK(t.returns() == "1");
    }SECTION("test70") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 32\n    return x ** y"
        );
        CHECK(t.returns() == "1");
    }SECTION("test71") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 4611686018427387903\n    return x ** y"
        );
        CHECK(t.returns() == "1");
    }SECTION("test72") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 9223372036854775807\n    return x ** y"
        );
        CHECK(t.returns() == "1");
    }SECTION("test73") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 1\n    return x ** y"
        );
        CHECK(t.returns() == "4611686018427387903");
    }SECTION("test74") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 1\n    return x ** y"
        );
        CHECK(t.returns() == "9223372036854775807");
    }

    SECTION("test75") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 2\n    return x // y"
        );
        CHECK(t.returns() == "0");
    }SECTION("test76") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 4611686018427387903\n    return x // y"
        );
        CHECK(t.returns() == "0");
    }SECTION("test77") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 9223372036854775807\n    return x // y"
        );
        CHECK(t.returns() == "0");
    }SECTION("test78") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 1\n    return x // y"
        );
        CHECK(t.returns() == "4611686018427387903");
    }SECTION("test79") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 4611686018427387903\n    return x // y"
        );
        CHECK(t.returns() == "2");
    }SECTION("test80") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = -4611686018427387903\n    return x // y"
        );
        CHECK(t.returns() == "-3");
    }SECTION("test81") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 1\n    return x // y"
        );
        CHECK(t.returns() == "9223372036854775807");
    }SECTION("test82") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = -1\n    return x // y"
        );
        CHECK(t.returns() == "-9223372036854775807");
    }SECTION("test83") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x // y"
        );
        CHECK(t.returns() == "1");
    }

    SECTION("test84") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 2\n    return x % y"
        );
        CHECK(t.returns() == "1");
    }SECTION("test85") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 4611686018427387903\n    return x % y"
        );
        CHECK(t.returns() == "1");
    }SECTION("test86") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 1\n    return x % y"
        );
        CHECK(t.returns() == "0");
    }SECTION("test87") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 4611686018427387903\n    return x % y"
        );
        CHECK(t.returns() == "1");
    }SECTION("test88") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = -4611686018427387903\n    return x % y"
        );
        CHECK(t.returns() == "-4611686018427387902");
    }SECTION("test89") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 1\n    return x % y"
        );
        CHECK(t.returns() == "0");
    }SECTION("test90") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = -1\n    return x % y"
        );
        CHECK(t.returns() == "0");
    }SECTION("test91") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x % y"
        );
        CHECK(t.returns() == "0");
    }

    SECTION("test92") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 2\n    return x | y"
        );
        CHECK(t.returns() == "3");
    }SECTION("test93") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 4611686018427387903\n    return x | y"
        );
        CHECK(t.returns() == "4611686018427387903");
    }SECTION("test94") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 9223372036854775807\n    return x | y"
        );
        CHECK(t.returns() == "9223372036854775807");
    }SECTION("test95") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 1\n    return x | y"
        );
        CHECK(t.returns() == "4611686018427387903");
    }SECTION("test96") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 1\n    return x | y"
        );
        CHECK(t.returns() == "9223372036854775807");
    }SECTION("test97") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x | y"
        );
        CHECK(t.returns() == "9223372036854775807");
    }SECTION("test98") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 2\n    return x & y"
        );
        CHECK(t.returns() == "0");
    }SECTION("test99") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 3\n    return x & y"
        );
        CHECK(t.returns() == "1");
    }SECTION("test100") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 4611686018427387903\n    return x & y"
        );
        CHECK(t.returns() == "1");
    }SECTION("test101") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 9223372036854775807\n    return x & y"
        );
        CHECK(t.returns() == "1");
    }SECTION("test102") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 1\n    return x & y"
        );
        CHECK(t.returns() == "1");
    }SECTION("test103") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 1\n    return x & y"
        );
        CHECK(t.returns() == "1");
    }SECTION("test104") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x & y"
        );
        CHECK(t.returns() == "9223372036854775807");
    }

    SECTION("test105") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 2\n    return x ^ y"
        );
        CHECK(t.returns() == "3");
    }SECTION("test106") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 3\n    return x ^ y"
        );
        CHECK(t.returns() == "2");
    }SECTION("test107") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 4611686018427387903\n    return x ^ y"
        );
        CHECK(t.returns() == "4611686018427387902");
    }SECTION("test108") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 9223372036854775807\n    return x ^ y"
        );
        CHECK(t.returns() == "9223372036854775806");
    }SECTION("test109") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 1\n    return x ^ y"
        );
        CHECK(t.returns() == "4611686018427387902");
    }SECTION("test110") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 1\n    return x ^ y"
        );
        CHECK(t.returns() == "9223372036854775806");
    }SECTION("test111") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x ^ y"
        );
        CHECK(t.returns() == "0");
    }

    SECTION("test112") {
        auto t = EmissionTest(
                "def f():\n    x = -9223372036854775808\n    y = 1\n    return x - y"
        );
        CHECK(t.returns() == "-9223372036854775809");
    }SECTION("test113") {
        auto t = EmissionTest(
                "def f():\n    x = -1\n    y = 4611686018427387904\n    return x - y"
        );
        CHECK(t.returns() == "-4611686018427387905");
    }SECTION("test114") {
        auto t = EmissionTest(
                "def f():\n    x = -1\n    y = 9223372036854775808\n    return x - y"
        );
        CHECK(t.returns() == "-9223372036854775809");
    }SECTION("test115") {
        auto t = EmissionTest(
                "def f():\n    x =  -4611686018427387904\n    y = 1\n    return x - y"
        );
        CHECK(t.returns() == "-4611686018427387905");
    }

    SECTION("test116") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 4611686018427387903\n    return x + y"
        );
        CHECK(t.returns() == "4611686018427387904");
    }SECTION("test117") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 9223372036854775807\n    return x + y"
        );
        CHECK(t.returns() == "9223372036854775808");
    }SECTION("test118") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 1\n    return x + y"
        );
        CHECK(t.returns() == "4611686018427387904");
    }SECTION("test119") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 1\n    return x + y"
        );
        CHECK(t.returns() == "9223372036854775808");
    }SECTION("test120") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x + y"
        );
        CHECK(t.returns() == "18446744073709551614");
    }

    SECTION("test121") {
        auto t = EmissionTest(
                "def f():\n    x = 2\n    y = 4611686018427387903\n    return x * y"
        );
        CHECK(t.returns() == "9223372036854775806");
    }SECTION("test122") {
        auto t = EmissionTest(
                "def f():\n    x = 2\n    y = 9223372036854775807\n    return x * y"
        );
        CHECK(t.returns() == "18446744073709551614");
    }SECTION("test123") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 2\n    return x * y"
        );
        CHECK(t.returns() == "9223372036854775806");
    }SECTION("test124") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 2\n    return x * y"
        );
        CHECK(t.returns() == "18446744073709551614");
    }SECTION("test125") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x * y"
        );
        CHECK(t.returns() == "85070591730234615847396907784232501249");
    }
}
TEST_CASE("test binary/arithmetic operations") {
    // Simple optimized code test cases...
    SECTION("inplace left shift") {
        auto t = EmissionTest(
                "def f():\n    x = 2\n    x <<= 2\n    return x"
        );
        CHECK(t.returns() == "8");
    }
    SECTION("inplace right shift") {
        auto t = EmissionTest(
                "def f():\n    x = 8\n    x >>= 2\n    return x"
        );
        CHECK(t.returns() == "2");
    }

    SECTION("test") {
        auto t = EmissionTest(
                "def f():\n    x = 1.0\n    y = +x\n    return y"
        );
        CHECK(t.returns() == "1.0");
    }SECTION("test2") {
        auto t = EmissionTest(
                "def f():\n    x = 1.0\n    if not x:\n        return 1\n    return 2"
        );
        CHECK(t.returns() == "2");
    }SECTION("test3") {
        auto t = EmissionTest(
                "def f():\n    x = 0.0\n    if not x:\n        return 1\n    return 2"
        );
        CHECK(t.returns() == "1");
    }SECTION("test4") {
        auto t = EmissionTest(
                "def f():\n    x = 1.0\n    y = -x\n    return y"
        );
        CHECK(t.returns() == "-1.0");
    }SECTION("test5") {
        auto t = EmissionTest(
                "def f():\n    x = 1.0\n    y = not x\n    return y"
        );
        CHECK(t.returns() == "False");
    }SECTION("test6") {
        auto t = EmissionTest(
                "def f():\n    x = 0.0\n    y = not x\n    return y"
        );
        CHECK(t.returns() == "True");
    }SECTION("test7") {
        auto t = EmissionTest(
                "def f():\n    x = 1.0\n    return x"
        );
        CHECK(t.returns() == "1.0");
    }SECTION("test8") {
        auto t = EmissionTest(
                "def f():\n    x = 1.0\n    y = 2.0\n    z = x + y\n    return z"
        );
        CHECK(t.returns() == "3.0");
    }SECTION("test9") {
        auto t = EmissionTest(
                "def f():\n    x = 1.0\n    y = 2.0\n    z = x - y\n    return z"
        );
        CHECK(t.returns() == "-1.0");
    }SECTION("test10") {
        auto t = EmissionTest(
                "def f():\n    x = 1.0\n    y = 2.0\n    z = x / y\n    return z"
        );
        CHECK(t.returns() == "0.5");
    }SECTION("test11") {
        auto t = EmissionTest(
                "def f():\n    x = 1.0\n    y = 2.0\n    z = x // y\n    return z"
        );
        CHECK(t.returns() == "0.0");
    }SECTION("test12") {
        auto t = EmissionTest(
                "def f():\n    x = 1.0\n    y = 2.0\n    z = x % y\n    return z"
        );
        CHECK(t.returns() == "1.0");
    }SECTION("test13") {
        auto t = EmissionTest(
                "def f():\n    x = 2.0\n    y = 3.0\n    z = x * y\n    return z"
        );
        CHECK(t.returns() == "6.0");
    }SECTION("test14") {
        auto t = EmissionTest(
                "def f():\n    x = 2.0\n    y = 3.0\n    z = x ** y\n    return z"
        );
        CHECK(t.returns() == "8.0");
    }SECTION("test15") {
        auto t = EmissionTest(
                "def f():\n    x = 2.0\n    y = 3.0\n    if x == y:\n        return True\n    return False"
        );
        CHECK(t.returns() == "False");
    }SECTION("test16") {
        auto t = EmissionTest(
                "def f():\n    x = 3.0\n    y = 3.0\n    if x == y:\n        return True\n    return False"
        );
        CHECK(t.returns() == "True");
    }SECTION("test17") {
        auto t = EmissionTest(
                "def f():\n    x = 'a'\n    y = 'b'\n    if x == y:\n        return True\n    return False"
        );
        CHECK(t.returns() == "False");
    }SECTION("test18") {
        auto t = EmissionTest(
                "def f():\n    x = 'a'\n    y = 'a'\n    if x == y:\n        return True\n    return False"
        );
        CHECK(t.returns() == "True");
    }SECTION("test19") {
        auto t = EmissionTest(
                "def f():\n    class Foo(str): pass\n    x = Foo(1)\n    y = Foo(2)\n    if x == y:        return True\n    return False"
        );
        CHECK(t.returns() == "False");
    }SECTION("test20") {
        auto t = EmissionTest(
                "def f():\n    class Foo(str): pass\n    x = Foo(1)\n    y = Foo(1)\n    if x == y:        return True\n    return False"
        );
        CHECK(t.returns() == "True");
    }SECTION("test21") {
        auto t = EmissionTest(
                "def f():\n    x = 2.0\n    y = 3.0\n    if x != y:\n        return True\n    return False"
        );
        CHECK(t.returns() == "True");
    }SECTION("test22") {
        auto t = EmissionTest(
                "def f():\n    x = 3.0\n    y = 3.0\n    if x != y:\n        return True\n    return False"
        );
        CHECK(t.returns() == "False");
    }SECTION("test23") {
        auto t = EmissionTest(
                "def f():\n    x = 2.0\n    y = 3.0\n    if x >= y:\n        return True\n    return False"
        );
        CHECK(t.returns() == "False");
    }SECTION("test24") {
        auto t = EmissionTest(
                "def f():\n    x = 3.0\n    y = 3.0\n    if x >= y:\n        return True\n    return False"
        );
        CHECK(t.returns() == "True");
    }SECTION("test25") {
        auto t = EmissionTest(
                "def f():\n    x = 2.0\n    y = 3.0\n    if x > y:\n        return True\n    return False"
        );
        CHECK(t.returns() == "False");
    }SECTION("test26") {
        auto t = EmissionTest(
                "def f():\n    x = 4.0\n    y = 3.0\n    if x > y:\n        return True\n    return False"
        );
        CHECK(t.returns() == "True");
    }SECTION("test27") {
        auto t = EmissionTest(
                "def f():\n    x = 3.0\n    y = 2.0\n    if x <= y:\n        return True\n    return False"
        );
        CHECK(t.returns() == "False");
    }SECTION("test28") {
        auto t = EmissionTest(
                "def f():\n    x = 3.0\n    y = 3.0\n    if x <= y:\n        return True\n    return False"
        );
        CHECK(t.returns() == "True");
    }SECTION("test29") {
        auto t = EmissionTest(
                "def f():\n    x = 3.0\n    y = 2.0\n    if x < y:\n        return True\n    return False"
        );
        CHECK(t.returns() == "False");
    }SECTION("test30") {
        auto t = EmissionTest(
                "def f():\n    x = 3.0\n    y = 4.0\n    if x < y:\n        return True\n    return False"
        );
        CHECK(t.returns() == "True");
    }SECTION("test31") {
        auto t = EmissionTest(
                "def f():\n    x = 1.0\n    y = 2.0\n    x += y\n    return x"
        );
        CHECK(t.returns() == "3.0");
    }SECTION("test32") {
        auto t = EmissionTest(
                "def f():\n    x = 1.0\n    y = 2.0\n    x -= y\n    return x"
        );
        CHECK(t.returns() == "-1.0");
    }SECTION("test33") {
        auto t = EmissionTest(
                "def f():\n    x = 1.0\n    y = 2.0\n    x /= y\n    return x"
        );
        CHECK(t.returns() == "0.5");
    }SECTION("test34") {
        auto t = EmissionTest(
                "def f():\n    x = 1.0\n    y = 2.0\n    x //= y\n    return x"
        );
        CHECK(t.returns() == "0.0");
    }SECTION("test35") {
        auto t = EmissionTest(
                "def f():\n    x = 1.0\n    y = 2.0\n    x %= y\n    return x"
        );
        CHECK(t.returns() == "1.0");
    }SECTION("test36") {
        auto t = EmissionTest(
                "def f():\n    x = 2.0\n    y = 3.0\n    x *= y\n    return x"
        );
        CHECK(t.returns() == "6.0");
    }SECTION("test37") {
        auto t = EmissionTest(
                "def f():\n    x = 2.0\n    y = 3.0\n    x **= y\n    return x"
        );
        CHECK(t.returns() == "8.0");
    }
        // fully optimized complex code
    SECTION("test38") {
        auto t = EmissionTest(
                "def f():\n    pi = 0.\n    k = 0.\n    while k < 256.:\n        pi += (4. / (8.*k + 1.) - 2. / (8.*k + 4.) - 1. / (8.*k + 5.) - 1. / (8.*k + 6.)) / 16.**k\n        k += 1.\n    return pi"
        );
        CHECK(t.returns() == "3.141592653589793");
    }
        // division error handling code gen with value on the stack
    SECTION("test39") {
        auto t = EmissionTest(
                "def f():\n    x = 1.0\n    y = 2.0\n    z = 3.0\n    return x + y / z"
        );
        CHECK(t.returns() == "1.6666666666666665");
    }
        // division by zero error case
    SECTION("test40") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 0\n    try:\n        return x / y\n    except:\n        return 42"
        );
        CHECK(t.returns() == "42");
    }SECTION("test41") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 0\n    try:\n        return x // y\n    except:\n        return 42"
        );
        CHECK(t.returns() == "42");
    }SECTION("test43") {
        auto t = EmissionTest(
                "def f():\n    a = RefCountCheck()\n    del a\n    return finalized"
        );
        CHECK(t.raises() == PyExc_NameError);
    }SECTION("test44") {
        auto t = EmissionTest(
                "def f():\n    for i in {2:3}:\n        pass\n    return i"
        );
        CHECK(t.returns() == "2");
    }
}

TEST_CASE("Test math operations") {
    SECTION("test binary multiply") {
        auto t = EmissionTest(
                "def f():\n    x = b'abc'*3\n    return x"
        );
        CHECK(t.returns() == "b'abcabcabc'");
    }
    SECTION("test increment unbound ") {
        auto t = EmissionTest(
                "def f():\n    unbound += 1"
        );
        CHECK(t.raises() == PyExc_UnboundLocalError);
    }
    SECTION("test modulus by zero") {
        auto t = EmissionTest(
                "def f():\n    5 % 0"
        );
        CHECK(t.raises() == PyExc_ZeroDivisionError);
    }
    SECTION("test modulus floats by zero") {
        auto t = EmissionTest(
                "def f():\n    5.0 % 0.0"
        );
        CHECK(t.raises() == PyExc_ZeroDivisionError);
    }
    SECTION("test floor divide by zero") {
        auto t = EmissionTest(
                "def f():\n    5.0 // 0.0"
        );
        CHECK(t.raises() == PyExc_ZeroDivisionError);
    }
    SECTION("test divide by zero") {
        auto t = EmissionTest(
                "def f():\n    5.0 / 0.0"
        );
        CHECK(t.raises() == PyExc_ZeroDivisionError);
    }
    SECTION("test string multiply") {
        auto t = EmissionTest(
                "def f():\n    x = 'abc'*3\n    return x"
        );
        CHECK(t.returns() == "'abcabcabc'");
    }
    SECTION("test boundary ranging") {
        auto t = EmissionTest(
                "def f():\n    if 0.0 < 1.0 <= 1.0 == 1.0 >= 1.0 > 0.0 != 1.0:  return 42"
        );
        CHECK(t.returns() == "42");
    }
}

TEST_CASE("Test rich comparisons of floats") {
    SECTION("test greater than") {
        auto t = EmissionTest(
                "def f():\n    x = 1.5\n    y = 2.5\n    return x > y"
        );
        CHECK(t.returns() == "False");
    };
}