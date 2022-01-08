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
#include "testing_util.h"

template<typename T>
void incref(T v) {
    Py_INCREF(v);
}

template<typename T, typename... Args>
void incref(T v, Args... args) {
    Py_INCREF(v);
    incref(args...);
}

TEST_CASE("Test math errors") {
    SECTION("test float divide by zero") {
        auto t = EmissionTest(
                "def f(): 1.0 / 0");
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
    SECTION("complex nested calculation") {
        auto t = EmissionTest("def f():\n"
                              "  dt = 2.0\n"
                              "  dx = 3.0\n"
                              "  dy = 4.0\n"
                              "  dz = 5.0\n"
                              "  mag = dt * ((dx * dx + dy * dy + dz * dz) ** (-1.5))\n"
                              "  return mag");
        CHECK(t.returns() == "0.00565685424949238");
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

    SECTION("compare two calculations") {
        auto t = EmissionTest("def f():\n"
                              "  a = 3\n"
                              "  b = 5\n"
                              "  c = 7\n"
                              "  if a + b == c * a:\n"
                              "     return False\n"
                              "  else:\n"
                              "     return True");
        CHECK(t.returns() == "True");
    }
}


TEST_CASE("Unary tests") {
    SECTION("most basic unary not") {
        auto t = EmissionTest(
                "def f():\n  x=True\n  return not x\n");
        CHECK(t.returns() == "False");
    }
    SECTION("in place add") {
        auto t = EmissionTest(
                "def f():\n  x=1\n  x+=1\n  return x");
        CHECK(t.returns() == "2");
    }
    SECTION("simple add") {
        auto t = EmissionTest(
                "def f():\n  x=1\n  y=2\n  z = x+y\n  return z");
        CHECK(t.returns() == "3");
    }
    SECTION("test1") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    x += 1\n    x -= 1\n    y = not x\n    return y");
        CHECK(t.returns() == "False");
    }
    SECTION("test2") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    x += 1\n    x -= 1\n    if x:\n        return True\n    return False");
        CHECK(t.returns() == "True");
    }
    SECTION("test3") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    x += 1\n    x -= 1\n    x -= 4611686018427387903\n    if x:\n        return True\n    return False");
        CHECK(t.returns() == "False");
    }

    SECTION("test4") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    x += 1\n    x -= 1\n    if not x:\n        return True\n    return False");
        CHECK(t.returns() == "False");
    }
    SECTION("test5") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    x += 1\n    x -= 1\n    x -= 4611686018427387903\n    if not x:\n        return True\n    return False");
        CHECK(t.returns() == "True");
    }

    SECTION("test6") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    x += 1\n    x -= 1\n    x -= 4611686018427387903\n    y = not x\n    return y");
        CHECK(t.returns() == "True");
    }

    SECTION("test7") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    x -= 1\n    if x == y:\n        return True\n    return False");
        CHECK(t.returns() == "True");
    }
    SECTION("test8") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    x -= 1\n    if x <= y:\n        return True\n    return False");
        CHECK(t.returns() == "True");
    }
    SECTION("test9") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    x -= 1\n    if x >= y:\n        return True\n    return False");
        CHECK(t.returns() == "True");
    }
    SECTION("test10") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    x -= 1\n    if x != y:\n        return True\n    return False");
        CHECK(t.returns() == "False");
    }
    SECTION("test11") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    x -= 1\n    if x < y:\n        return True\n    return False");
        CHECK(t.returns() == "False");
    }
    SECTION("test12") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    x -= 1\n    if x > y:\n        return True\n    return False");
        CHECK(t.returns() == "False");
    }
    SECTION("test13") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    if x < y:\n        return True\n    return False");
        CHECK(t.returns() == "False");
    }
    SECTION("test14") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    if x > y:\n        return True\n    return False");
        CHECK(t.returns() == "True");
    }
    SECTION("test15") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    y += 1\n    if x < y:\n        return True\n    return False");
        CHECK(t.returns() == "True");
    }
    SECTION("test16") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    y += 1\n    if x > y:\n        return True\n    return False");
        CHECK(t.returns() == "False");
    }

    SECTION("test17") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x == y");
        CHECK(t.returns() == "True");
    }
    SECTION("test18") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    x -= 1\n    return x == y");
        CHECK(t.returns() == "True");
    }
    SECTION("test19") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    y += 1\n    y -= 1\n    return x == y");
        CHECK(t.returns() == "True");
    }
    SECTION("test20") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    return x == y");
        CHECK(t.returns() == "False");
    }
    SECTION("test21") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    y += 1\n    return x == y");
        CHECK(t.returns() == "False");
    }
    SECTION("test22") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 1\n    return x == y");
        CHECK(t.returns() == "True");
    }

    SECTION("test23") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x != y");
        CHECK(t.returns() == "False");
    }
    SECTION("test24") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    x -= 1\n    return x != y");
        CHECK(t.returns() == "False");
    }
    SECTION("test25") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    y += 1\n    y -= 1\n    return x != y");
        CHECK(t.returns() == "False");
    }
    SECTION("test26") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    return x != y");
        CHECK(t.returns() == "True");
    }
    SECTION("test27") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    y += 1\n    return x != y");
        CHECK(t.returns() == "True");
    }
    SECTION("test28") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 1\n    return x != y");
        CHECK(t.returns() == "False");
    }

    SECTION("test29") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x >= y");
        CHECK(t.returns() == "True");
    }
    SECTION("test30") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    x -= 1\n    return x >= y");
        CHECK(t.returns() == "True");
    }
    SECTION("test31") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    y += 1\n    y -= 1\n    return x >= y");
        CHECK(t.returns() == "True");
    }
    SECTION("test32") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 1\n    return x >= y");
        CHECK(t.returns() == "True");
    }

    SECTION("test33") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x <= y");
        CHECK(t.returns() == "True");
    }
    SECTION("test34") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    x -= 1\n    return x <= y");
        CHECK(t.returns() == "True");
    }
    SECTION("test35") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    y += 1\n    y -= 1\n    return x <= y");
        CHECK(t.returns() == "True");
    }
    SECTION("test36") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 1\n    return x <= y");
        CHECK(t.returns() == "True");
    }

    SECTION("test37") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x > y");
        CHECK(t.returns() == "False");
    }
    SECTION("test38") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775808\n    y = 9223372036854775807\n    return x > y");
        CHECK(t.returns() == "True");
    }
    SECTION("test39") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775808\n    return x > y");
        CHECK(t.returns() == "False");
    }
    SECTION("test40") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    return x > y");
        CHECK(t.returns() == "True");
    }
    SECTION("test41") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    y += 1\n    return x > y");
        CHECK(t.returns() == "False");
    }

    SECTION("test42") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x < y");
        CHECK(t.returns() == "False");
    }
    SECTION("test43") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775808\n    y = 9223372036854775807\n    return x < y");
        CHECK(t.returns() == "False");
    }
    SECTION("test44") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775808\n    return x < y");
        CHECK(t.returns() == "True");
    }
    SECTION("test45") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    x += 1\n    return x < y");
        CHECK(t.returns() == "False");
    }
    SECTION("test46") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 4611686018427387903\n    y += 1\n    return x < y");
        CHECK(t.returns() == "True");
    }

    SECTION("little int equal") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 1\n    return x == y");
        CHECK(t.returns() == "True");
    }
    SECTION("big int modulus") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 9223372036854775807\n    return x % y");
        CHECK(t.returns() == "1");
    }

    SECTION("simple int divide") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 2\n    return x / y");
        CHECK(t.returns() == "0.5");
    }
    SECTION("large int divide") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 4611686018427387903\n    return x / y");
        CHECK(t.returns() == "2.168404344971009e-19");
    }
    SECTION("big int divide") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 9223372036854775807\n    return x / y");
        CHECK(t.returns() == "1.0842021724855044e-19");
    }
    SECTION("large int divide by 1") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 1\n    return x / y");
        CHECK(t.returns() == "4.611686018427388e+18");
    }
    SECTION("big int divide by 1") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 1\n    return x / y");
        CHECK(t.returns() == "9.223372036854776e+18");
    }
    SECTION("big ints divide") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x / y");
        CHECK(t.returns() == "1.0");
    }
}
TEST_CASE("Tests BINARY_RSHIFT"){
    SECTION("int right shift") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 2\n    return x >> y");
        CHECK(t.returns() == "0");
    }
    SECTION("large int right shift") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 4611686018427387903\n    return x >> y");
        CHECK(t.returns() == "0");
    }
    SECTION("big int right shift") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 9223372036854775807\n    return x >> y");
        CHECK(t.returns() == "0");
    }
    SECTION("test58") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 1\n    return x >> y");
        CHECK(t.returns() == "2305843009213693951");
    }
    SECTION("test59") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 1\n    return x >> y");
        CHECK(t.returns() == "4611686018427387903");
    }
    SECTION("test60") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x >> y");
        CHECK(t.returns() == "0");
    }
    SECTION("test small right shift large") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 9223372036854775807\n    return x >> y");
        CHECK(t.returns() == "0");
    }
}
TEST_CASE ("Test BINARY_LSHIFT") {
    SECTION("test left shift 1 << 2") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 2\n    return x << y");
        CHECK(t.returns() == "4");
    }
    SECTION("test left shift 1 << 32") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 32\n    return x << y");
        CHECK(t.returns() == "4294967296");
    }
    SECTION("test small left shift 1 << 62") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 62\n    return x << y");
        CHECK(t.returns() == "4611686018427387904");
    }

//    SECTION("test small left shift 1 << 63 overflow", "[!mayfail]") {
//        auto t = EmissionTest(
//                "def f():\n    x = 1\n    y = 63\n    return x << y");
//        CHECK(t.returns() == "9223372036854775808");
//    }
//    SECTION("test small left shift 1 << 64 overflow", "[!mayfail]") {
//        auto t = EmissionTest(
//                "def f():\n    x = 1\n    y = 64\n    return x << y");
//        CHECK(t.returns() == "18446744073709551616");
//    }
//    SECTION("test small left shift 1 << 128 overflow", "[!mayfail]") {
//        auto t = EmissionTest(
//                "def f():\n    x = 1\n    y = 128\n    return x << y");
//        CHECK(t.returns() == "340282366920938463463374607431768211456");
//    }
    SECTION("test medium int left shift small") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 1\n    return x << y");
        CHECK(t.returns() == "9223372036854775806");
    }
    SECTION("test large left shift small") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 1\n    return x << y");
        CHECK(t.returns() == "18446744073709551614");
    }
}
TEST_CASE("Test BINARY_POWER") {
    SECTION("small int power") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 2\n    return x ** y");
        CHECK(t.returns() == "1");
    }
    SECTION("medium int power") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 32\n    return x ** y");
        CHECK(t.returns() == "1");
    }
    SECTION("large int power") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 4611686018427387903\n    return x ** y");
        CHECK(t.returns() == "1");
    }
    SECTION("big int power") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 9223372036854775807\n    return x ** y");
        CHECK(t.returns() == "1");
    }
    SECTION("large int power 1") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 1\n    return x ** y");
        CHECK(t.returns() == "4611686018427387903");
    }
    SECTION("big int power 1") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 1\n    return x ** y");
        CHECK(t.returns() == "9223372036854775807");
    }
}
TEST_CASE("Test BINARY_FLOOR_DIVIDE"){
    SECTION("small int floor divide") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 2\n    return x // y");
        CHECK(t.returns() == "0");
    }
    SECTION("large int floor divide") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 4611686018427387903\n    return x // y");
        CHECK(t.returns() == "0");
    }
    SECTION("big int floor divide") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 9223372036854775807\n    return x // y");
        CHECK(t.returns() == "0");
    }
    SECTION("large int floor divide by 1") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 1\n    return x // y");
        CHECK(t.returns() == "4611686018427387903");
    }
    SECTION("big int floor divide by large int") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 4611686018427387903\n    return x // y");
        CHECK(t.returns() == "2");
    }
    SECTION("big int floor divide by -ve large int") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = -4611686018427387903\n    return x // y");
        CHECK(t.returns() == "-3");
    }
    SECTION("big int floor divide by 1") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 1\n    return x // y");
        CHECK(t.returns() == "9223372036854775807");
    }
    SECTION("big int floor divide by -1") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = -1\n    return x // y");
        CHECK(t.returns() == "-9223372036854775807");
    }
    SECTION("big int floor divide by big int") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x // y");
        CHECK(t.returns() == "1");
    }

    SECTION("small int mod") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 2\n    return x % y");
        CHECK(t.returns() == "1");
    }
    SECTION("small int mod by large int") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 4611686018427387903\n    return x % y");
        CHECK(t.returns() == "1");
    }
    SECTION("large int mod by large int") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 1\n    return x % y");
        CHECK(t.returns() == "0");
    }
    SECTION("test87") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 4611686018427387903\n    return x % y");
        CHECK(t.returns() == "1");
    }
    SECTION("big int modulus by large -ve int") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = -4611686018427387903\n    return x % y");
        CHECK(t.returns() == "-4611686018427387902");
    }
    SECTION("test89") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 1\n    return x % y");
        CHECK(t.returns() == "0");
    }
    SECTION("test90") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = -1\n    return x % y");
        CHECK(t.returns() == "0");
    }
    SECTION("test91") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x % y");
        CHECK(t.returns() == "0");
    }

    SECTION("test92") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 2\n    return x | y");
        CHECK(t.returns() == "3");
    }
    SECTION("test93") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 4611686018427387903\n    return x | y");
        CHECK(t.returns() == "4611686018427387903");
    }
    SECTION("test94") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 9223372036854775807\n    return x | y");
        CHECK(t.returns() == "9223372036854775807");
    }
    SECTION("test95") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 1\n    return x | y");
        CHECK(t.returns() == "4611686018427387903");
    }
    SECTION("test96") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 1\n    return x | y");
        CHECK(t.returns() == "9223372036854775807");
    }
    SECTION("test97") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x | y");
        CHECK(t.returns() == "9223372036854775807");
    }
    SECTION("test98") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 2\n    return x & y");
        CHECK(t.returns() == "0");
    }
    SECTION("test99") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 3\n    return x & y");
        CHECK(t.returns() == "1");
    }
    SECTION("test100") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 4611686018427387903\n    return x & y");
        CHECK(t.returns() == "1");
    }
    SECTION("test101") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 9223372036854775807\n    return x & y");
        CHECK(t.returns() == "1");
    }
    SECTION("test102") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 1\n    return x & y");
        CHECK(t.returns() == "1");
    }
    SECTION("test103") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 1\n    return x & y");
        CHECK(t.returns() == "1");
    }
    SECTION("test104") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x & y");
        CHECK(t.returns() == "9223372036854775807");
    }

    SECTION("test105") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 2\n    return x ^ y");
        CHECK(t.returns() == "3");
    }
    SECTION("test106") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 3\n    return x ^ y");
        CHECK(t.returns() == "2");
    }
    SECTION("test107") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 4611686018427387903\n    return x ^ y");
        CHECK(t.returns() == "4611686018427387902");
    }
    SECTION("test108") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 9223372036854775807\n    return x ^ y");
        CHECK(t.returns() == "9223372036854775806");
    }
    SECTION("test109") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 1\n    return x ^ y");
        CHECK(t.returns() == "4611686018427387902");
    }
    SECTION("test110") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 1\n    return x ^ y");
        CHECK(t.returns() == "9223372036854775806");
    }
    SECTION("test111") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x ^ y");
        CHECK(t.returns() == "0");
    }

    SECTION("large -ve spill int") {
        auto t = EmissionTest(
                "def f():\n    x = -9223372036854775808\n    y = 1\n    return x - y");
        CHECK(t.returns() == "-9223372036854775809");
    }
    SECTION("large -ve spill int no overflow") {
        auto t = EmissionTest(
                "def f():\n    x = -1\n    y = 4611686018427387904\n    return x - y");
        CHECK(t.returns() == "-4611686018427387905");
    }
    SECTION("small -1 int minus large int") {
        auto t = EmissionTest(
                "def f():\n    x = -1\n    y = 9223372036854775808\n    return x - y");
        CHECK(t.returns() == "-9223372036854775809");
    }
    SECTION("test115") {
        auto t = EmissionTest(
                "def f():\n    x =  -4611686018427387904\n    y = 1\n    return x - y");
        CHECK(t.returns() == "-4611686018427387905");
    }

    SECTION("test116") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 4611686018427387903\n    return x + y");
        CHECK(t.returns() == "4611686018427387904");
    }
    SECTION("large + spill int") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 9223372036854775807\n    return x + y");
        CHECK(t.returns() == "9223372036854775808");
    }
    SECTION("test118") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 1\n    return x + y");
        CHECK(t.returns() == "4611686018427387904");
    }
    SECTION("large int spill addition") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 1\n    return x + y");
        CHECK(t.returns() == "9223372036854775808");
    }
    SECTION("large int spill addition 2") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x + y");
        CHECK(t.returns() == "18446744073709551614");
    }

    SECTION("large int spill mul") {
        auto t = EmissionTest(
                "def f():\n    x = 2\n    y = 4611686018427387903\n    return x * y");
        CHECK(t.returns() == "9223372036854775806");
    }
    SECTION("small by large int spill mul") {
        auto t = EmissionTest(
                "def f():\n    x = 2\n    y = 9223372036854775807\n    return x * y");
        CHECK(t.returns() == "18446744073709551614");
    }
    SECTION("med int by large int spill mul") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    y = 2\n    return x * y");
        CHECK(t.returns() == "9223372036854775806");
    }
    SECTION("large int by small int spill mul") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 2\n    return x * y");
        CHECK(t.returns() == "18446744073709551614");
    }
    SECTION("large int by large int spill mul") {
        auto t = EmissionTest(
                "def f():\n    x = 9223372036854775807\n    y = 9223372036854775807\n    return x * y");
        CHECK(t.returns() == "85070591730234615847396907784232501249");
    }
}

TEST_CASE("test binary/arithmetic operations") {
    // Simple optimized code test cases...
    SECTION("inplace left shift") {
        auto t = EmissionTest(
                "def f():\n    x = 2\n    x <<= 2\n    return x");
        CHECK(t.returns() == "8");
    }
    SECTION("inplace right shift") {
        auto t = EmissionTest(
                "def f():\n    x = 8\n    x >>= 2\n    return x");
        CHECK(t.returns() == "2");
    }
    SECTION("float postive unary") {
        auto t = EmissionTest(
                "def f():\n    x = 1.0\n    y = +x\n    return y");
        CHECK(t.returns() == "1.0");
    }
    SECTION("int postive unary") {
        auto t = EmissionTest(
                "def f():\n    x = -2\n    y = +x\n    return y");
        CHECK(t.returns() == "2");
    }
    SECTION("int postive unary positive const") {
        auto t = EmissionTest(
                "def f():\n    x = 2\n    y = +x\n    return y");
        CHECK(t.returns() == "2");
    }
    SECTION("float not unary") {
        auto t = EmissionTest(
                "def f():\n    x = 1.0\n    if not x:\n        return 1\n    return 2");
        CHECK(t.returns() == "2");
    }
    SECTION("float is falsey") {
        auto t = EmissionTest(
                "def f():\n    x = 0.0\n    if not x:\n        return 1\n    return 2");
        CHECK(t.returns() == "1");
    }
    SECTION("float negative unary") {
        auto t = EmissionTest(
                "def f():\n    x = 1.0\n    y = -x\n    return y");
        CHECK(t.returns() == "-1.0");
    }
    SECTION("float not operator") {
        auto t = EmissionTest(
                "def f():\n    x = 1.0\n    y = not x\n    return y");
        CHECK(t.returns() == "False");
    }
    SECTION("int ~ operator") {
        CHECK(EmissionTest("def f():\n    x = -1\n    return ~x").returns() == "0");
        CHECK(EmissionTest("def f():\n    x = -4\n    return ~x").returns() == "3");
        CHECK(EmissionTest("def f():\n    x = 4\n    return ~x").returns() == "-5");
        CHECK(EmissionTest("def f():\n    x = 0\n    return ~x").returns() == "-1");
    }
    SECTION("test unary constants") {
        auto t = EmissionTest(
                "def f(): \n"
                "  if not -24.0 < -12.0: \n"
                "    return False");
        CHECK(t.returns() == "None");
    }
    SECTION("test unary constants reversed") {
        auto t = EmissionTest(
                "def f(): \n"
                "  if not -24.0 > -12.0: \n"
                "    return True");
        CHECK(t.returns() == "True");
    }
    SECTION("float falsey not") {
        auto t = EmissionTest(
                "def f():\n    x = 0.0\n    y = not x\n    return y");
        CHECK(t.returns() == "True");
    }
    SECTION("test7") {
        auto t = EmissionTest(
                "def f():\n    x = 1.2\n    return x");
        CHECK(t.returns() == "1.2");
    }
    SECTION("test8") {
        auto t = EmissionTest(
                "def f():\n    x = 1.001\n    y = 2.022\n    z = x + y\n    return z");
        CHECK(t.returns() == "3.0229999999999997");
    }
    SECTION("test9") {
        auto t = EmissionTest(
                "def f():\n    x = 1.001\n    y = 2.01\n    z = x - y\n    return z");
        CHECK(t.returns() == "-1.009");
    }
    SECTION("test10") {
        auto t = EmissionTest(
                "def f():\n    x = 1.022\n    y = 2.033\n    z = x / y\n    return z");
        CHECK(t.returns() == "0.5027053615346778");
    }
    SECTION("test11") {
        auto t = EmissionTest(
                "def f():\n    x = 1.022\n    y = 2.033\n    z = x // y\n    return z");
        CHECK(t.returns() == "0.0");
    }
    SECTION("test12") {
        auto t = EmissionTest(
                "def f():\n    x = 1.011\n    y = 2.011\n    z = x % y\n    return z");
        CHECK(t.returns() == "1.011");
    }
    SECTION("test13") {
        auto t = EmissionTest(
                "def f():\n    x = 2.022\n    y = 3.033\n    z = x * y\n    return z");
        CHECK(t.returns() == "6.132725999999999");
    }
    SECTION("test14") {
        auto t = EmissionTest(
                "def f():\n    x = 2.022\n    y = 3.033\n    z = x ** y\n    return z");
        CHECK(t.returns() == "8.461244245792681");
    }
    SECTION("test15") {
        auto t = EmissionTest(
                "def f():\n    x = 2.022\n    y = 3.033\n    if x == y:\n        return True\n    return False");
        CHECK(t.returns() == "False");
    }
    SECTION("test16") {
        auto t = EmissionTest(
                "def f():\n    x = 3.022\n    y = 3.022\n    if x == y:\n        return True\n    return False");
        CHECK(t.returns() == "True");
    }
    SECTION("test17") {
        auto t = EmissionTest(
                "def f():\n    x = 'a'\n    y = 'b'\n    if x == y:\n        return True\n    return False");
        CHECK(t.returns() == "False");
    }
    SECTION("test18") {
        auto t = EmissionTest(
                "def f():\n    x = 'a'\n    y = 'a'\n    if x == y:\n        return True\n    return False");
        CHECK(t.returns() == "True");
    }
    SECTION("test19") {
        auto t = EmissionTest(
                "def f():\n    class Foo(str): pass\n    x = Foo(1)\n    y = Foo(2)\n    if x == y:        return True\n    return False");
        CHECK(t.returns() == "False");
    }
    SECTION("test20") {
        auto t = EmissionTest(
                "def f():\n    class Foo(str): pass\n    x = Foo(1)\n    y = Foo(1)\n    if x == y:        return True\n    return False");
        CHECK(t.returns() == "True");
    }
    SECTION("test21") {
        auto t = EmissionTest(
                "def f():\n    x = 2.022\n    y = 3.023\n    if x != y:\n        return True\n    return False");
        CHECK(t.returns() == "True");
    }
    SECTION("test22") {
        auto t = EmissionTest(
                "def f():\n    x = 3.023\n    y = 3.023\n    if x != y:\n        return True\n    return False");
        CHECK(t.returns() == "False");
    }
    SECTION("test23") {
        auto t = EmissionTest(
                "def f():\n    x = 2.023\n    y = 3.023\n    if x >= y:\n        return True\n    return False");
        CHECK(t.returns() == "False");
    }
    SECTION("test24") {
        auto t = EmissionTest(
                "def f():\n    x = 3.023\n    y = 3.023\n    if x >= y:\n        return True\n    return False");
        CHECK(t.returns() == "True");
    }
    SECTION("test25") {
        auto t = EmissionTest(
                "def f():\n    x = 2.023\n    y = 3.023\n    if x > y:\n        return True\n    return False");
        CHECK(t.returns() == "False");
    }
    SECTION("test26") {
        auto t = EmissionTest(
                "def f():\n    x = 4.023\n    y = 3.023\n    if x > y:\n        return True\n    return False");
        CHECK(t.returns() == "True");
    }
    SECTION("test27") {
        auto t = EmissionTest(
                "def f():\n    x = 3.023\n    y = 2.023\n    if x <= y:\n        return True\n    return False");
        CHECK(t.returns() == "False");
    }
    SECTION("test28") {
        auto t = EmissionTest(
                "def f():\n    x = 3.023\n    y = 3.023\n    if x <= y:\n        return True\n    return False");
        CHECK(t.returns() == "True");
    }
    SECTION("test29") {
        auto t = EmissionTest(
                "def f():\n    x = 3.023\n    y = 2.023\n    if x < y:\n        return True\n    return False");
        CHECK(t.returns() == "False");
    }
    SECTION("test30") {
        auto t = EmissionTest(
                "def f():\n    x = 3.023\n    y = 4.023\n    if x < y:\n        return True\n    return False");
        CHECK(t.returns() == "True");
    }
    SECTION("test31") {
        auto t = EmissionTest(
                "def f():\n    x = 1.023\n    y = 2.023\n    x += y\n    return x");
        CHECK(t.returns() == "3.0460000000000003");
    }
    SECTION("test32") {
        auto t = EmissionTest(
                "def f():\n    x = 1.023\n    y = 2.023\n    x -= y\n    return x");
        CHECK(t.returns() == "-1.0000000000000002");
    }
    SECTION("test true division of floats") {
        auto t = EmissionTest(
                "def f():\n    x = 1.023\n    y = 2.023\n    x /= y\n    return x");
        CHECK(t.returns() == "0.5056846267918932");
    }
    SECTION("test inplace floor division of floats") {
        auto t = EmissionTest(
                "def f():\n    x = 2.023\n    y = 1.023\n    x //= y\n    return x");
        CHECK(t.returns() == "1.0");
    }
    SECTION("test inplace modulo of floats") {
        auto t = EmissionTest(
                "def f():\n    x = 1.023\n    y = 2.023\n    x %= y\n    return x");
        CHECK(t.returns() == "1.023");
    }
    SECTION("test inplace multiply of floats") {
        auto t = EmissionTest(
                "def f():\n    x = 2.023\n    y = 3.023\n    x *= y\n    return x");
        CHECK(t.returns() == "6.115529");
    }
    SECTION("test inplace power of floats") {
        auto t = EmissionTest(
                "def f():\n    x = 2.023\n    y = 3.023\n    x **= y\n    return x");
        CHECK(t.returns() == "8.414446502664783");
    }
    // fully optimized complex code
    SECTION("test calculation of pi") {
        auto t = EmissionTest(
                "def f():\n    pi = 0.\n    k = 0.\n    while k < 256.:\n        pi += (4. / (8.*k + 1.) - 2. / (8.*k + 4.) - 1. / (8.*k + 5.) - 1. / (8.*k + 6.)) / 16.**k\n        k += 1.\n    return pi");
        CHECK(t.returns() == "3.141592653589793");
    }
    // division error handling code gen with value on the stack
    SECTION("test operator precedence") {
        auto t = EmissionTest(
                "def f():\n    x = 1.0\n    y = 2.0\n    z = 3.0\n    return x + y / z");
        CHECK(t.returns() == "1.6666666666666665");
    }
    SECTION("test name error raised on delete RefCountCheck") {
        auto t = EmissionTest(
                "def f():\n    a = RefCountCheck()\n    del a\n    return finalized");
        CHECK(t.raises() == PyExc_NameError);
    }
    SECTION("test scope leak of loop") {
        auto t = EmissionTest(
                "def f():\n    for i in {2:3}:\n        pass\n    return i");
        CHECK(t.returns() == "2");
    }
}

TEST_CASE("Test math operations") {
    SECTION("test binary multiply") {
        auto t = EmissionTest(
                "def f():\n    x = b'abc'*3\n    return x");
        CHECK(t.returns() == "b'abcabcabc'");
    }
    SECTION("test increment unbound ") {
        auto t = EmissionTest(
                "def f():\n    unbound += 1");
        CHECK(t.raises() == PyExc_UnboundLocalError);
    }
    SECTION("test modulus by zero") {
        auto t = EmissionTest(
                "def f():\n    return 5 % 0");
        CHECK(t.raises() == PyExc_ZeroDivisionError);
    }
    SECTION("test modulus floats by zero") {
        auto t = EmissionTest(
                "def f():\n    return 5.0 % 0.0");
        CHECK(t.raises() == PyExc_ZeroDivisionError);
    }
    SECTION("test floor divide by zero") {
        auto t = EmissionTest(
                "def f():\n    return 5.0 // 0.0");
        CHECK(t.raises() == PyExc_ZeroDivisionError);
    }
    SECTION("test divide by zero") {
        auto t = EmissionTest(
                "def f():\n    return 5.0 / 0.0");
        CHECK(t.raises() == PyExc_ZeroDivisionError);
    }
    SECTION("test string multiply") {
        auto t = EmissionTest(
                "def f():\n    x = 'abc'*3\n    return x");
        CHECK(t.returns() == "'abcabcabc'");
    }
    SECTION("test boundary ranging") {
        auto t = EmissionTest(
                "def f():\n    if 0.0 < 1.0 <= 1.0 == 1.0 >= 1.0 > 0.0 != 1.0:  return 42");
        CHECK(t.returns() == "42");
    }
}

TEST_CASE("Test rich comparisons of floats") {
    SECTION("test greater than") {
        auto t = EmissionTest(
                "def f():\n    x = 1.5\n    y = 2.5\n    return x > y");
        CHECK(t.returns() == "False");
    };
}

TEST_CASE("Test unboxing of floats") {
    SECTION("complex nested calculation") {
        auto t = EmissionTest("def f():\n"
                              "  dx = 3.0\n"
                              "  dy = 4.0\n"
                              "  dz = 5.0\n"
                              "  mag = dz * (dx * dy)\n"
                              "  return mag");
        CHECK(t.returns() == "60.0");
    }
    SECTION("complex nested calculation 2") {
        auto t = EmissionTest("def f():\n"
                              "  dx = 9.5e-322\n"
                              "  dy = -1.2174e-320\n"
                              "  dz = -1.249e-320\n"
                              "  m1 = 39.47841760435743\n"
                              "  m2 = 0.03769367487038949\n"
                              "  return (m1 * m2) / ((dx * dx + dy * dy + dz * dz) ** 0.5)");
        CHECK(t.raises() == PyExc_ZeroDivisionError);
    }
    SECTION("test inplace subtraction") {
        auto t = EmissionTest("def f():\n"
                              "  dx = 0.452345\n"
                              "  dy = -91.35555\n"
                              "  dz = -1.249e-320\n"
                              "  dz -= dx * dy\n"
                              "  return dz");
        CHECK(t.returns() == "41.324226264749996");
    }
    SECTION("test inplace addition") {
        auto t = EmissionTest("def f():\n"
                              "  dx = 0.452345\n"
                              "  dy = -91.35555\n"
                              "  dz = 2346.3333\n"
                              "  dz += dx * dy\n"
                              "  return dz");
        CHECK(t.returns() == "2305.00907373525");
    }
    SECTION("test inplace slice addition") {
        auto t = EmissionTest("def f():\n"
                              "  dx = 0.452345\n"
                              "  dy = -91.35555\n"
                              "  dz = [2346.3333]\n"
                              "  dz[0] += dx * dy\n"
                              "  return dz[0]");
        CHECK(t.returns() == "2305.00907373525");
    }

    SECTION("test mixed modulo") {
        auto t = EmissionTest("def f():\n"
                              "  a = 1\n"
                              "  b = 2\n"
                              "  c = \"boo %s\"\n"
                              "  x = c % (a + b)\n"
                              "  return x");
        CHECK(t.returns() == "'boo 3'");
    }

    SECTION("test root negative mixed") {
        auto t = EmissionTest("def f():\n"
                              "  i = -10\n"
                              "  x = 1234567890.0 * (10.0 ** i)\n"
                              "  return x");
        CHECK(t.returns() == "0.12345678900000001");
    }
}

TEST_CASE("Test bool arithmetic") {
    SECTION("test greater than") {
        auto t = EmissionTest(
                "def f():\n    x = True\n    y = False\n    return x > y");
        CHECK(t.returns() == "True");
    };

    SECTION("test less than") {
        auto t = EmissionTest(
                "def f():\n    x = True\n    y = False\n    return x < y");
        CHECK(t.returns() == "False");
    };

    SECTION("test equal") {
        auto t = EmissionTest(
                "def f():\n    x = True\n    y = False\n    return x == y");
        CHECK(t.returns() == "False");
    };

    SECTION("test greater than equal") {
        auto t = EmissionTest(
                "def f():\n    x = True\n    y = False\n    return x >= y");
        CHECK(t.returns() == "True");
    };

    SECTION("test less than equal") {
        auto t = EmissionTest(
                "def f():\n    x = True\n    y = False\n    return x <= y");
        CHECK(t.returns() == "False");
    };

    SECTION("test not equal") {
        auto t = EmissionTest(
                "def f():\n    x = True\n    y = False\n    return x != y");
        CHECK(t.returns() == "True");
    };

    SECTION("test is") {
        auto t = EmissionTest(
                "def f():\n    x = True\n    y = False\n    return x is y");
        CHECK(t.returns() == "False");
    };
}

TEST_CASE("Test negatives") {
    SECTION("test zero subtraction") {
        auto t = EmissionTest(
                "def f():\n    x = 0.\n    y = 0.\n    return x - y");
        CHECK(t.returns() == "0.0");
    };
    SECTION("test zero power negative float") {
        auto t = EmissionTest(
                "def f():\n    x = 0.\n    return x ** -2.");
        CHECK(t.raises() == PyExc_ZeroDivisionError);
    };
    SECTION("test zero power negative") {
        auto t = EmissionTest(
                "def f():\n    x = 0\n    return x ** -2");
        CHECK(t.raises() == PyExc_ZeroDivisionError);
    };
    SECTION("test number power negative") {
        auto t = EmissionTest(
                "def f():\n    x = 2\n    y = -2\n    return x ** y");
        CHECK(t.returns() == "0.25");
    };
    SECTION("test negative number power") {
        auto t = EmissionTest(
                "def f():\n    x = -2\n    y = 2\n    return x ** y");
        CHECK(t.returns() == "4");
    };
    SECTION("test negative number power float") {
        auto t = EmissionTest(
                "def f():\n    x = -2.\n    y = 2.\n    return x ** y");
        CHECK(t.returns() == "4.0");
    };
    SECTION("test negative number power odd") {
        auto t = EmissionTest(
                "def f():\n    x = -3\n    y = 3\n    return x ** y");
        CHECK(t.returns() == "-27");
    };
    SECTION("test negative number power float odd") {
        auto t = EmissionTest(
                "def f():\n    x = -3.\n    y = 3.\n    return x ** y");
        CHECK(t.returns() == "-27.0");
    };
}

TEST_CASE("Test float modulo") {
    SECTION("test 1") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    y = 2.\n    return x % y");
        CHECK(t.returns() == "1.0");
    };
    SECTION("test 2") {
        auto t = EmissionTest(
                "def f():\n    x = 1.\n    y = 2.\n    return x % y");
        CHECK(t.returns() == "1.0");
    };
    SECTION("test 3") {
        auto t = EmissionTest(
                "def f():\n    x = 1.4\n    y = 2.\n    return x % y");
        CHECK(t.returns() == "1.4");
    };
    SECTION("test 4") {
        auto t = EmissionTest(
                "def f():\n    x = 1.4\n    y = 2\n    return x % y");
        CHECK(t.returns() == "1.4");
    };
    SECTION("test 5") {
        auto t = EmissionTest(
                "def f():\n    x = 3.4\n    y = 2.\n    return x % y");
        CHECK(t.returns() == "1.4");
    };
    SECTION("test 6") {
        auto t = EmissionTest(
                "def f():\n    x = 2\n    y = 4.\n    return x % y");
        CHECK(t.returns() == "2.0");
    };
}