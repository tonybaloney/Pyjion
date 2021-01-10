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
