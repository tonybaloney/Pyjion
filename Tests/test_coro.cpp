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

TEST_CASE("Test yield/generators with YIELD_VALUE") {
    SECTION("common case") {
        auto t = EmissionTest("def f():\n"
            "  def cr():\n"
            "     yield 1\n"
            "     yield 2\n"
            "     yield 3\n"
            "  gen = cr()\n"
            "  return next(gen), next(gen), next(gen)\n");
        CHECK(t.returns() == "(1, 2, 3)");
    }

    SECTION("test preservation of boxed variables") {
        auto t = EmissionTest("def f():\n"
                              "  def cr():\n"
                              "     x = '1'\n"
                              "     yield x\n"
                              "     x = '2'\n"
                              "     yield x\n"
                              "     x = '3'\n"
                              "     yield x\n"
                              "  gen = cr()\n"
                              "  return next(gen), next(gen), next(gen)\n");
        CHECK(t.returns() == "('1', '2', '3')");
    }
    SECTION("test preservation of unboxed variables") {
        auto t = EmissionTest("def f():\n"
                              "  def cr():\n"
                              "     x = 1\n"
                              "     yield x\n"
                              "     x = 2\n"
                              "     yield x\n"
                              "     x = 3\n"
                              "     yield x\n"
                              "  gen = cr()\n"
                              "  return next(gen), next(gen), next(gen)\n");
        CHECK(t.returns() == "(1, 2, 3)");
    }

    SECTION("test yield within branches.") {
        auto t = EmissionTest("def f():\n"
                              "  def cr():\n"
                              "     x = '2'\n"
                              "     if x == '2':\n"
                              "         yield 'a'\n"
                              "     else:\n"
                              "         yield 'b'\n"
                              "     yield 'c'\n"
                              "     x = x + '2'\n"
                              "     if x == '22':\n"
                              "         yield 'd'\n"
                              "     else:\n"
                              "         yield x\n"
                              "     yield 'c'\n"
                              "  gen = cr()\n"
                              "  return next(gen), next(gen), next(gen)\n");
        CHECK(t.returns() == "('a', 'c', 'd')");
    }

    SECTION("test yield within branches for boxable vars.") {
        auto t = EmissionTest("def f():\n"
                              "  def cr():\n"
                              "     x = 2\n"
                              "     if x == 2:\n"
                              "         yield 'a'\n"
                              "     else:\n"
                              "         yield 'b'\n"
                              "     yield 'c'\n"
                              "     x = x + 2\n"
                              "     if x == 4:\n"
                              "         yield 'd'\n"
                              "     else:\n"
                              "         yield x\n"
                              "     yield 'c'\n"
                              "  gen = cr()\n"
                              "  return next(gen), next(gen), next(gen)\n");
        CHECK(t.returns() == "('a', 'c', 'd')");
    }
    SECTION("test range generator.") {
        auto t = EmissionTest("def f():\n"
                              "  def cr():\n"
                              "     for n in range(10):\n"
                              "         yield n ** 2\n"
                              "  return [x for x in cr()]\n");
        CHECK(t.returns() == "[0, 1, 4, 9, 16, 25, 36, 49, 64, 81]");
    }
}