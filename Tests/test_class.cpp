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


TEST_CASE("test classes") {
    SECTION("test class definition") {
        auto t = EmissionTest(
                "def f():\n    class C:\n        pass\n    return C"
        );
        CHECK(t.returns() == "<class 'C'>");
    }
    SECTION("test class definition with annotations") {
        auto t = EmissionTest(
                "def f():\n    class C:\n      property: int = 0\n    return C"
        );
        CHECK(t.returns() == "<class 'C'>");
    }
}

TEST_CASE("test type"){
    SECTION("test define custom type") {
        auto t = EmissionTest(
                "def f():\n"
                "        A = type('A', (), {})\n"
                "        assert A.__name__ == 'A'\n"
                "        assert A.__qualname__ == 'A'\n"
                "        assert A.__bases__ == (object,)\n"
                "        assert A.__base__ is object\n"
                "        x = A()\n"
                "        assert type(x) is A\n"
                "        assert x.__class__ is A\n"
                "        return A.__name__\n"
        );
        CHECK(t.returns() == "'A'");
    }
    SECTION("test disappearing custom type") {
        auto t = EmissionTest(
                "def f():\n"
                "        A = type('A', (), {})\n"
                "        assert A.__name__ == 'A'\n"
                "        x = A()\n"
                "        del A\n"
                "        return x.__class__\n"
        );
        CHECK(t.returns() == "<class 'A'>");
    }
    SECTION("test PGC custom type") {
        auto t = PgcProfilingTest(
                "def f():\n"
                "        A = type('A', (), {})\n"
                "        assert A.__name__ == 'A'\n"
                "        x = A()\n"
                "        assert type(x) is A\n"
                "        assert x.__class__ is A\n"
                "        return A.__name__\n"
        );
        CHECK(t.returns() == "'A'");
        CHECK(t.returns() == "'A'");
        CHECK(t.returns() == "'A'");
    }
    SECTION("test define custom subtype"){
        auto t = EmissionTest(
                "def f():\n"
                "       class B:\n"
                "            def ham(self):\n"
                "                return 'ham%d' % self\n"
                "       C = type('C', (B, int), {'spam': lambda self: 'spam%s' % self})\n"
                "       assert C.__name__ == 'C'\n"
                "       assert C.__qualname__ == 'C'\n"
                "       assert C.__bases__ == (B, int)\n"
                "       assert C.__base__ is int\n"
                "       assert 'spam' in C.__dict__\n"
                "       assert 'ham' not in C.__dict__\n"
                "       x = C(42)\n"
                "       assert x == 42\n"
                "       assert type(x) is C\n"
                "       assert x.__class__ is C\n"
                "       assert x.ham() == 'ham42'\n"
                "       assert x.spam() == 'spam42'\n"
                "       assert x.to_bytes(2, 'little') == b'\\x2a\\x00'\n"
                "       return x"
                );
        CHECK(t.returns() == "42");
    }
    SECTION("test define custom subtype PGC"){
        auto t = PgcProfilingTest(
                "def f():\n"
                "       class B:\n"
                "            def ham(self):\n"
                "                return 'ham%d' % self\n"
                "       C = type('C', (B, int), {'spam': lambda self: 'spam%s' % self})\n"
                "       assert C.__name__ == 'C'\n"
                "       assert C.__qualname__ == 'C'\n"
                "       assert C.__bases__ == (B, int)\n"
                "       assert C.__base__ is int\n"
                "       assert 'spam' in C.__dict__\n"
                "       assert 'ham' not in C.__dict__\n"
                "       x = C(42)\n"
                "       assert x == 42\n"
                "       assert type(x) is C\n"
                "       assert x.__class__ is C\n"
                "       assert x.ham() == 'ham42'\n"
                "       assert x.spam() == 'spam42'\n"
                "       assert x.to_bytes(2, 'little') == b'\\x2a\\x00'\n"
                "       return x"
                );
        CHECK(t.returns() == "42");
        CHECK(t.returns() == "42");
        CHECK(t.returns() == "42");
    }
}

TEST_CASE("Test methods"){
    SECTION("test simple method + argument") {
        auto t = EmissionTest(
                "def f():\n"
                "       class B:\n"
                "            def ham(self, _with):\n"
                "                return 'ham + %s' % _with\n"
                "       b = B()\n"
                "       return b.ham('eggs')\n"
        );
        CHECK(t.returns() == "'ham + eggs'");
    }
    SECTION("test simple method raising exception") {
        auto t = EmissionTest(
                "def f():\n"
                "       class B:\n"
                "            def ham(self, _with):\n"
                "                raise ValueError\n"
                "       b = B()\n"
                "       return b.ham('eggs')\n"
        );
        CHECK(t.raises() == PyExc_ValueError);
    }
    SECTION("test simple classmethod + argument") {
        auto t = EmissionTest(
                "def f():\n"
                "       class B:\n"
                "            @classmethod\n"
                "            def ham(cls, _with):\n"
                "                return 'ham + %s' % _with\n"
                "       b = B()\n"
                "       return b.ham('eggs')\n"
        );
        CHECK(t.returns() == "'ham + eggs'");
    }
    SECTION("test simple staticmethod + argument") {
        auto t = EmissionTest(
                "def f():\n"
                "       class B:\n"
                "            @staticmethod\n"
                "            def ham(_with):\n"
                "                return 'ham + %s' % _with\n"
                "       b = B()\n"
                "       return b.ham('eggs')\n"
        );
        CHECK(t.returns() == "'ham + eggs'");
    }
}

TEST_CASE("Test inheritance") {
    SECTION("test simple staticmethod + argument") {
        auto t = EmissionTest(
                "def f():\n"
                "  class Node(object):\n"
                "    def __init__(self, a, b, c):\n"
                "        self.a = a\n"
                "        self.b = b\n"
                "        self.c = c\n"
                "    def __repr__(self):\n"
                "        value = self.a\n"
                "        value = repr(value)\n"
                "        return '%s(tag=%r, value=%s)' % (self.__class__.__name__, self.b, value)\n"
                "  class ChildNode(Node):\n"
                "    def __init__(self, a, b, c):\n"
                "        self.a = a\n"
                "        self.b = b\n"
                "        self.c = c\n"
                "  class GrandchildNode(ChildNode):\n"
                "    d = 1\n"
                "  node = GrandchildNode('a', 'b', 'c')\n"
                "  x = repr(node)\n"
                "  del node\n"
                "  return x\n"
        );
        CHECK(t.returns() == "\"GrandchildNode(tag='b', value='a')\"");
    }
}