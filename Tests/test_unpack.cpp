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

TEST_CASE("Test unpacking with UNPACK_SEQUENCE") {
    SECTION("test single unpack") {
        auto t = EmissionTest(
                "def f():\n"
                "  a, = (1,)\n"
                "  return a"
        );
        CHECK(t.returns() == "1");
    }

    SECTION("test basic unpack") {
        auto t = EmissionTest(
                "def f():\n    a, b = (1, 2)\n    return a, b"
        );
        CHECK(t.returns() == "(1, 2)");
    }

    SECTION("unpack from list") {
        auto t = EmissionTest(
                "def f():\n  a, b, c = [1,2,3]\n  return a, b, c\n"
        );
        CHECK(t.returns() == "(1, 2, 3)");
    }

    SECTION("Too many items to unpack from list raises valueerror") {
        auto t = EmissionTest(
                "def f():\n    x = [1,2,3]\n    a, b = x"
        );
        CHECK(t.raises() == PyExc_ValueError);
    }

    SECTION("Too many items to unpack from tuple raises valueerror") {
        auto t = EmissionTest(
                "def f():\n    x = (1,2,3)\n    a, b = x"
        );
        CHECK(t.raises() == PyExc_ValueError);
    }

    SECTION("test sum from function call") {
        auto t = EmissionTest(
                "def f():\n    a, b, c = range(3)\n    return a + b + c"
        );
        CHECK(t.returns() == "3");
    }

    SECTION("test unpack from function call") {
        auto t = EmissionTest(
                "def f():\n    a, b = range(2000, 2002)\n    return a, b"
        );
        CHECK(t.returns() == "(2000, 2001)");
    }

    SECTION("test basic unpack again") {
        auto t = EmissionTest(
                "def f():\n    a, b = (1, 2)\n    return a, b"
        );
        CHECK(t.returns() == "(1, 2)");
    }

    SECTION("test unpack from function call too few") {
        auto t = EmissionTest(
                "def f():\n    a, b, c = range(2)\n    return a, b, c"
        );
        CHECK(t.raises() == PyExc_ValueError);
    }

    SECTION("test multiple assignments by unpack") {
        auto t = EmissionTest(
                "def f():\n    a, b = 1, 2\n    return a, b"
        );
        CHECK(t.returns() == "(1, 2)");
    }

    SECTION("unpacking non-iterable shouldn't crash") {
        auto t = EmissionTest(
                "def f():\n    a, b, c = len"
        );
        CHECK(t.raises() == PyExc_TypeError);
    }

    SECTION("test unpack for loop") {
        auto t = EmissionTest(
                "def f():\n    cs = [('CATEGORY', 'CATEGORY_SPACE')]\n    for op, av in cs:\n        while True:\n            break\n        print(op, av)"
        );
        CHECK(t.returns() == "None");
    }

    SECTION("test deleting unpacked vars 1") {
// Lifted from the stdlib test suite test_grammar test_del
        auto t = EmissionTest(
                "def f():\n"
                "        abc = [1,2,3]\n"
                "        x, y, z = abc\n"
                "        xyz = x, y, z\n"
                "        del abc\n"
                "        del x, y, (z, xyz)\n"
        );
        CHECK(t.returns() == "None");
    }SECTION("test deleting unpacked vars 2") {
        auto t = EmissionTest(
                "def f():\n"
                "        a, b, c, d, e, f, g = \"abcdefg\"\n"
                "        del a, (b, c), (d, (e, f))\n"
                "        a, b, c, d, e, f, g = \"abcdefg\"\n"
                "        del a, [b, c], (d, [e, f])\n"
        );
        CHECK(t.returns() == "None");
    }SECTION("test deleting unpacked vars 3") {
        auto t = EmissionTest(
                "def f():\n"
                "        abcd = list(\"abcd\")\n"
                "        del abcd[1:2]"
        );
        CHECK(t.returns() == "None");
    }
}

TEST_CASE("Test unpacking with UNPACK_EX") {
    SECTION("basic unpack from range iterator, return left") {
        auto t = EmissionTest(
                "def f():\n    a, *b, c = range(3)\n    return a"
        );
        CHECK(t.returns() == "0");
    }SECTION("basic unpack from range iterator, return sequence") {
        auto t = EmissionTest(
                "def f():\n    a, *b, c = range(3)\n    return b"
        );
        CHECK(t.returns() == "[1]");
    }SECTION("basic unpack from range iterator, return right") {
        auto t = EmissionTest(
                "def f():\n    a, *b, c = range(5)\n    return c"
        );
        CHECK(t.returns() == "4");
    }SECTION("unpack from const assignment, return left") {
        auto t = EmissionTest(
                "def f():\n    a, *b, c = 1, 2, 3\n    return a"
        );
        CHECK(t.returns() == "1");
    }SECTION("unpack from const assignment, return middle") {
        auto t = EmissionTest(
                "def f():\n    a, *b, c = 1, 2, 3\n    return b"
        );
        CHECK(t.returns() == "[2]");
    }SECTION("unpack from const assignment, return right") {
        auto t = EmissionTest(
                "def f():\n    a, *b, c = 1, 2, 3\n    return c"
        );
        CHECK(t.returns() == "3");
    }SECTION("unpack from const assignment, return right with empty middle") {
        auto t = EmissionTest(
                "def f():\n    a, *b, c = 1, 3\n    return c"
        );
        CHECK(t.returns() == "3");
    }SECTION("unpack from const assignment, return middle empty") {
        auto t = EmissionTest(
                "def f():\n    a, *b, c = 1, 3\n    return b"
        );
        CHECK(t.returns() == "[]");
    }SECTION("unpack from list, return left") {
        auto t = EmissionTest(
                "def f():\n    a, *b, c = [1, 2, 3]\n    return a"
        );
        CHECK(t.returns() == "1");
    }SECTION("unpack from list, return middle") {
        auto t = EmissionTest(
                "def f():\n    a, *b, c = [1, 2, 3]\n    return b"
        );
        CHECK(t.returns() == "[2]");
    }SECTION("unpack from list, return right") {
        auto t = EmissionTest(
                "def f():\n    a, *b, c = [1, 2, 3]\n    return c"
        );
        CHECK(t.returns() == "3");
    }

    SECTION("unpack from list comp") {
        auto t = EmissionTest(
                "def f():\n"
                "   obj = {'a': 1, 'b': 2}\n"
                "   return dict([\n"
                "     (value, key)\n"
                "     for (key, value) in obj.items()\n"
                "   ])"
        );
        CHECK(t.returns() == "{1: 'a', 2: 'b'}");
    }

    SECTION("unpack from list, return all packed") {
        auto t = EmissionTest(
                "def f():\n    a, *b, c = [1, 3]\n    return a, b, c"
        );
        CHECK(t.returns() == "(1, [], 3)");
    }

    SECTION("unpacks in right sequence") {
        auto t = EmissionTest(
                "def f():\n    a, b, c, *m, d, e, f = (0, 1, 2, 3, 4, 5, 6, 7, 8)\n    return a, b, c, d, e, f, m"
        );
        CHECK(t.returns() == "(0, 1, 2, 6, 7, 8, [3, 4, 5])");
    }

    SECTION("unpack imbalanced sequence") {
        auto t = EmissionTest(
                "def f():\n  first, second, third, *_, last = (0, 1, 2, 3, 4, 5, 6, 7, 8)\n  return second"
        );
        CHECK(t.returns() == "1");
    }

    SECTION("unpack reversed imbalanced sequence") {
        auto t = EmissionTest(
                "def f():\n  first, *_, before, before2, last = (0, 1, 2, 3, 4, 5, 6, 7, 8)\n  return before2"
        );
        CHECK(t.returns() == "7");
    }

/* Failure cases */
    SECTION("left too short") {
        auto t = EmissionTest(
                "def f():\n    x = [1]\n    a, b, *c = x"
        );
        CHECK(t.raises() == PyExc_ValueError);
    }

    SECTION("both too short") {
        auto t = EmissionTest(
                "def f():\n    a, *b, c = dict()"
        );
        CHECK(t.raises() == PyExc_ValueError);
    }

    SECTION("right too short") {
        auto t = EmissionTest(
                "def f():\n    a, *b, c, d, e = range(3)"
        );
        CHECK(t.raises() == PyExc_ValueError);
    }

    SECTION("not iterable") {
        auto t = EmissionTest(
                "def f():\n    a, *b, c, d, e = 3"
        );
        CHECK(t.raises() == PyExc_TypeError);
    }
}