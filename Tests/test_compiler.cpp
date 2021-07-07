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

TEST_CASE("Test ITER", "[float][binary op][inference]") {
    SECTION("test1") {
        // EXTENDED_ARG FOR_ITER:
        auto t = EmissionTest(
                "def f():\n"
                "        x = 1\n"
                "        for w in 1, 2, 3, 4:\n"
                "            x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2;\n"
                "            x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2; x += 2;\n"
                "        return x\n"
        );
        CHECK(t.returns() == "369");
    }
}
TEST_CASE("Annotation tests") {
    SECTION("test annotations") {
        auto t = EmissionTest(
                "def f():\n    def f(self) -> 42 : pass\n    return 42"
        );
        CHECK(t.returns() == "42");
    }
}

TEST_CASE("Native tests") {
    SECTION("test annotations") {
        auto t = EmissionTest(
                "def f():\n    def f(self) -> 42 : pass\n    return 42"
        );
        CHECK(t.returns() == "42");
        CHECK(PyLong_AsUnsignedLong(PyTuple_GetItem(t.native(), 1)) == t.native_len());
        CHECK(PyByteArray_Size(PyTuple_GetItem(t.native(), 0)) == t.native_len());
        CHECK(PyLong_AsUnsignedLong(PyTuple_GetItem(t.native(), 2)) != 0);
    }
}

TEST_CASE("Test IL dump") {
    SECTION("test short") {
        auto t = EmissionTest(
                "def f(): return 3 / 1"
        );
        CHECK(t.returns() == "3.0");
        CHECK(t.il()[0] =='\003');
    }
    SECTION("test long") {
        auto t = EmissionTest(
                "def f():\n    abc = 0\n    i = 0\n    n = 0\n    if i == n and not abc:\n        return 42\n    return 23"
        );
        CHECK(t.returns() == "42");
        CHECK(t.il()[0] =='\003');
    }
}

TEST_CASE("Test f-strings") {
    SECTION("test3") {
        auto t = EmissionTest(
                "def f(): print(f'x {42}')"
        );
        CHECK(t.returns() == "None");
    }SECTION("test4") {
        auto t = EmissionTest(
                "def f(): return f'abc {42}'"
        );
        CHECK(t.returns() == "'abc 42'");
    }

    SECTION("test5") {
        auto t = EmissionTest(
                "def f(): return f'abc {42:3}'"
        );
        CHECK(t.returns() == "'abc  42'");
    }SECTION("test6") {
        auto t = EmissionTest(
                "def f(): return f'abc {\"abc\"!a}'"
        );
        CHECK(t.returns() == "\"abc 'abc'\"");
    }

    SECTION("test f-strings") {
        auto t = EmissionTest(
                "def f(): return f'abc {\"abc\"!a:6}'"
        );
        CHECK(t.returns() == "\"abc 'abc' \"");
    }SECTION("test f-strings 1") {
        auto t = EmissionTest(
                "def f(): return f'abc {\"abc\"!r:6}'"
        );
        CHECK(t.returns() == "\"abc 'abc' \"");
    }SECTION("test f-strings 2") {
        auto t = EmissionTest(
                "def f(): return f'abc {\"abc\"!s}'"
        );
        CHECK(t.returns() == "'abc abc'");
    }
}
TEST_CASE("Test ranges") {
    SECTION("test in range") {
        auto t = EmissionTest(
                "def f():\n    for b in range(1):\n        x = b & 1 and -1.0 or 1.0\n    return x"
        );
        CHECK(t.returns() == "1.0");
    }
}

TEST_CASE("Test method loads and calls") {
    SECTION("Test method call"){
        auto t = EmissionTest(
                "def f():\n  a = [1,2,3]\n  a.append(4)\n  return a"
        );
        CHECK(t.returns() == "[1, 2, 3, 4]");
    }
}


TEST_CASE("Test boxing") {
    SECTION("partial should be boxed because it's consumed by print after being assigned in the break loop") {
        auto t = EmissionTest(
                "def f():\n    partial = 0\n    while 1:\n        partial = 1\n        break\n    if not partial:\n        print(partial)\n        return True\n    return False\n"
        );
        CHECK(t.returns() == "False");
    }

    SECTION("UNARY_NOT/POP_JUMP_IF_FALSE with optimized value on stack should be compiled correctly w/o crashing") {
        auto t = EmissionTest(
                "def f():\n    abc = 1.0\n    i = 0\n    n = 0\n    if i == n and not abc:\n        return 42\n    return 23"
        );
        CHECK(t.returns() == "23");
    }

    SECTION("test3") {
        auto t = EmissionTest(
                "def f():\n    abc = 1\n    i = 0\n    n = 0\n    if i == n and not abc:\n        return 42\n    return 23"
        );
        CHECK(t.returns() == "23");
    }

    SECTION("test4") {
        auto t = EmissionTest(
                "def f():\n    abc = 0.0\n    i = 0\n    n = 0\n    if i == n and not abc:\n        return 42\n    return 23"
        );
        CHECK(t.returns() == "42");
    }

    SECTION("test5") {
        auto t = EmissionTest(
                "def f():\n    abc = 0\n    i = 0\n    n = 0\n    if i == n and not abc:\n        return 42\n    return 23"
        );
        CHECK(t.returns() == "42");
    }
}

TEST_CASE("Conditional returns") {
    // +=, -= checks are to avoid constant folding
    SECTION("test") {
        auto t = EmissionTest(
                "def f():\n    x = 0\n    x += 1\n    x -= 1\n    return x or 1"
        );
        CHECK(t.returns() == "1");
    }
    SECTION("test2") {
        auto t = EmissionTest(
                "def f():\n    x = 0\n    x += 1\n    x -= 1\n    return x and 1"
        );
        CHECK(t.returns() == "0");
    }
    SECTION("test3") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    x += 1\n    x -= 1\n    return x or 2"
        );
        CHECK(t.returns() == "1");
    }
    SECTION("test4") {
        auto t = EmissionTest(
                "def f():\n    x = 1\n    x += 1\n    x -= 1\n    return x and 2"
        );
        CHECK(t.returns() == "2");
    }
    SECTION("test5") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    x += 1\n    x -= 1\n    return x or 1"
        );
        CHECK(t.returns() == "4611686018427387903");
    }
    SECTION("test6") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    x += 1\n    x -= 1\n    return x and 1"
        );
        CHECK(t.returns() == "1");
    }
    SECTION("test7") {
        // TODO: Work out why this creates an invalid instruction graph.
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    x += 1\n    x -= 1\n    x -= 4611686018427387903\n    return x or 1"
        );
        CHECK(t.returns() == "1");
    }
    SECTION("test8") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    x += 1\n    x -= 1\n    x -= 4611686018427387903\n    return x and 1"
        );
        CHECK(t.returns() == "0");
    }
    SECTION("test9") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    x += 1\n    x -= 1\n    return -x"
        );
        CHECK(t.returns() == "-4611686018427387903");
    }
    SECTION("test10") {
        auto t = EmissionTest(
                "def f():\n    x = 4611686018427387903\n    x += 1\n    return -x"
        );
        CHECK(t.returns() == "-4611686018427387904");
    }
    SECTION("test11") {
        auto t = EmissionTest(
                "def f():\n    x = -4611686018427387904\n    x += 1\n    x -= 1\n    return -x"
        );
        CHECK(t.returns() == "4611686018427387904");
    }
}

TEST_CASE("test make function") {
    SECTION("test make function with argument annotations - return introspected annotation") {
        auto t = EmissionTest(
                "def f():\n    def g(b:1, *, a = 2):\n     return a\n    return g.__annotations__['b']"
        );
        CHECK(t.returns() == "1");
    }SECTION("test make function with argument annotations - return result") {
        auto t = EmissionTest(
                "def f():\n    def g(b:1, *, a = 2):\n     return a\n    return g(3)"
        );
        CHECK(t.returns() == "2");
    }SECTION("test51") {
        auto t = EmissionTest(
                "def f():\n    def g(*, a = 2):\n     return a\n    return g()"
        );
        CHECK(t.returns() == "2");
    }SECTION("test52") {
        auto t = EmissionTest(
                "def f():\n    def g(a:1, b:2): pass\n    return g.__annotations__['a']"
        );
        CHECK(t.returns() == "1");
    }SECTION("test55") {
        auto t = EmissionTest(
                "def f():\n    def g(*a): return a\n    return g(1, 2, 3, **{})"
        );
        CHECK(t.returns() == "(1, 2, 3)");
    }SECTION("test56") {
        auto t = EmissionTest(
                "def f():\n    def g(**a): return a\n    return g(y = 3, **{})"
        );
        CHECK(t.returns() == "{'y': 3}");
    }SECTION("test57") {
        auto t = EmissionTest(
                "def f():\n    def g(**a): return a\n    return g(**{'x':2})"
        );
        CHECK(t.returns() == "{'x': 2}");
    }SECTION("test58") {
        auto t = EmissionTest(
                "def f():\n    def g(**a): return a\n    return g(x = 2, *())"
        );
        CHECK(t.returns() == "{'x': 2}");
    }SECTION("test59") {
        auto t = EmissionTest(
                "def f():\n    def g(*a): return a\n    return g(*(1, 2, 3))"
        );
        CHECK(t.returns() == "(1, 2, 3)");
    }SECTION("test60") {
        auto t = EmissionTest(
                "def f():\n    def g(*a): return a\n    return g(1, *(2, 3))"
        );
        CHECK(t.returns() == "(1, 2, 3)");
    }
}
TEST_CASE("test function calls") {
    SECTION("test most simple function declaration") {
        auto t = EmissionTest(
                "def f():\n    def g(): return 1\n    return g()"
        );
        CHECK(t.returns() == "1");
    }
    SECTION("test function declarations") {
        auto t = EmissionTest(
                "def f():\n    def g(): pass\n    g.abc = {fn.lower() for fn in ['A']}\n    return g.abc"
        );
        CHECK(t.returns() == "{'a'}");
    }

    SECTION("test keyword calling") {
        auto t = EmissionTest(
                "def f():\n    x = {}\n    x.update(y=2)\n    return x"
        );
        CHECK(t.returns() == "{'y': 2}");
    }

    SECTION("test default arguments") {
        auto t = EmissionTest(
                "def f():\n    def g(a=2): return a\n    return g()"
        );
        CHECK(t.returns() == "2");
    }
    SECTION("test default arguments twice") {
        auto t = EmissionTest(
                "def f():\n    def g(a=2): return a\n    return g() + g()"
        );
        CHECK(t.returns() == "4");
    }

    SECTION("test lots of default arguments") {
        auto t = EmissionTest(
                "def f():\n    def g(a,b,c,d,e,f,g,h,i): return a + b + c + d + e + f + g + h + i\n    return g(1,2,4,8,16,32,64,128,256)"
        );
        CHECK(t.returns() == "511");
    }
}

TEST_CASE("test range generators") {
    SECTION("test range iterator with continue/break") {
        auto t = EmissionTest(
                "def f():\n    for i in range(3):\n        if i == 0: continue\n        break\n    return i"
        );
        CHECK(t.returns() == "1");
    }
    SECTION("test range iterator with break") {
        auto t = EmissionTest(
                "def f():\n    for i in range(3):\n        if i == 1: break\n    return i"
        );
        CHECK(t.returns() == "1");
    }
}
TEST_CASE("test slicing"){
    SECTION("test79") {
        auto t = EmissionTest(
                "def f():\n    return [1,2,3][1:]"
        );
        CHECK(t.returns() == "[2, 3]");
    }SECTION("test80") {
        auto t = EmissionTest(
                "def f():\n    return [1,2,3][:1]"
        );
        CHECK(t.returns() == "[1]");
    }SECTION("test81") {
        auto t = EmissionTest(
                "def f():\n    return [1,2,3][1:2]"
        );
        CHECK(t.returns() == "[2]");
    }SECTION("test82") {
        auto t = EmissionTest(
                "def f():\n    return [1,2,3][0::2]"
        );
        CHECK(t.returns() == "[1, 3]");
    }
}


TEST_CASE("test language features") {
    SECTION("test basic iter") {
        auto t = EmissionTest(
                "def f():\n    a = 0\n    for x in [1]:\n        a = a + 1\n    return a"
        );
        CHECK(t.returns() == "1");
    }
    SECTION("test nested iter") {
        auto t = EmissionTest(
                "def f():\n  a = 0\n  for y in [1,2,3]:\n    for x in [1, 2, 3]:\n      a += x + y\n  return a"
        );
        CHECK(t.returns() == "36");
    }
    SECTION("test list comprehension") {
        auto t = EmissionTest(
                "def f(): return [x for x in range(2)]"
        );
        CHECK(t.returns() == "[0, 1]");
    }

    SECTION("test if inside list comprehension") {
        auto t = EmissionTest(
                "def f():\n"
                "   path_parts = ('a', 'b', 'c') \n"
                "   return '/'.join([part.rstrip('-') for part in path_parts if part])"
        );
        CHECK(t.returns() == "'a/b/c'");
    }
    SECTION("test attribute access") {
        auto t = EmissionTest(
                "def f():\n"
                "   def g():\n"
                "    pass\n"
                "   return g.__name__"
        );
        CHECK(t.returns() == "'g'");
    };
}
TEST_CASE("Test augassign"){
    SECTION("test basic augassign every operator") {
        auto t = EmissionTest(
                "def f():\n    x = 2;x += 1;x *= 2;x **= 2;x -= 8;x //= 5;x %= 3;x &= 2;x |= 5;x ^= 1;x /= 2\n    return x"
        );
        CHECK(t.returns() == "3.0");
    }
    SECTION("test list augassign every operator") {
        auto t = EmissionTest(
                "def f():\n  x = [2];x[0] += 1;x[0] *= 2;x[0] **= 2;x[0] -= 8;x[0] //= 5;x[0] %= 3;x[0] &= 2;x[0] |= 5;x[0] ^= 1;x[0] /= 2\n  return x[0]"
        );
        CHECK(t.returns() == "3.0");
    }
    SECTION("test dict augassign every operator") {
        auto t = EmissionTest(
                "def f():\n  x = {0: 2};x[0] += 1;x[0] *= 2;x[0] **= 2;x[0] -= 8;x[0] //= 5;x[0] %= 3;x[0] &= 2;x[0] |= 5;x[0] ^= 1;x[0] /= 2;\n  return x[0]"
        );
        CHECK(t.returns() == "3.0");
    }

}

TEST_CASE("Test and return"){
    SECTION("test func ret builtin") {
        auto t = EmissionTest(
                "def f():\n"
                "    l = [1,1,1,1]\n"
                "    return all(x==1 for x in l) and all(x==2 for x in l)"
        );
        CHECK(t.returns() == "False");
    }
    SECTION("test func ret double") {
        auto t = EmissionTest(
                "def f():\n"
                "    l = [1,1,1,1]\n"
                "    return all(l) and all(l)"
        );
        CHECK(t.returns() == "True");
    }
    SECTION("test func ret simple") {
        auto t = EmissionTest(
                "def f():\n"
                "    l = [1,1,1,1]\n"
                "    return all(l)"
        );
        CHECK(t.returns() == "True");
    }
}
TEST_CASE("Test locals propagation with no frame globals") {
    SECTION("test precomputed hash on dict") {
        auto t = EmissionTest(
                "def f():\n"
                "    l = {'a': 1, 'b': 2}\n"
                "    l['a'] = 3\n"
                "    return l['a']"
        );
        CHECK(t.returns() == "3");
    }
}
TEST_CASE("Test locals propagation", "[!mayfail]") {
    SECTION("test precomputed hash on dict within exec") {
        auto t = EmissionTest(
                "def f():\n"
                "    l = {'a': 1, 'b': 2}\n"
                "    exec('l[\"a\"] = 3')\n"
                "    return l['a']\n"
        );
        CHECK(t.returns() == "3");
    }
    SECTION("get locals") {
        auto t = EmissionTest(
                "def f():\n"
                "    a = 1\n"
                "    b = 2\n"
                "    return locals()\n"
        );
        CHECK(t.returns() == "3");
    }
}

TEST_CASE("byte arrays") {
    SECTION("test bytearray buffer overrun") {
        auto t = EmissionTest(
                "def f():\n"
                "    b = bytearray(10)\n"
                "    b.pop() \n"  // Defeat expanding buffer off-by-one quirk
                "    del b[:1]\n"  // Advance start pointer without reallocating
                "    b += bytes(2)\n"  // Append exactly the number of deleted bytes
                "    del b\n"
        );
        CHECK(t.returns() == "None");
    }
}

TEST_CASE("test equivalent with isinstance") {
    SECTION("test is string") {
        auto t = EmissionTest(
                "def f():\n"
                "    b = str('hello')\n"
                "    return isinstance(b, str)\n"
        );
        CHECK(t.returns() == "True");
    }
}

TEST_CASE("test ternary expressions"){
    SECTION("test expression assignment"){
        auto t = EmissionTest(
                "def f():\n"
                "   bits = 'roar'\n"
                "   is_reversed = bits[-1] == 'r'\n"
                "   return is_reversed\n"
        );
        CHECK(t.returns() == "True");
    }
    SECTION("test ternary"){
        auto t = EmissionTest(
                "def f():\n"
                "   count = 3\n"
                "   is_three = 4 if count == 3 else 1\n"
                "   return is_three\n"
        );
        CHECK(t.returns() == "4");
    }
    SECTION("test sliced ternary"){
        auto t = EmissionTest(
                "def f():\n"
                "   bits = ('whats', 'this', 'in', 'reversed')\n"
                "   is_reversed = bits[-1] == 'reversed'\n"
                "   in_index = -3 if is_reversed else -2\n"
                "   if bits[in_index] != 'in':\n"
                "       return True"
                );
        CHECK(t.returns() == "True");
    }
}

TEST_CASE("Test classmethods"){
    SECTION("Test classmethods with a shared name"){
        auto t = EmissionTest(
                "def f():\n"
                "        class F:\n"
                "            @classmethod\n"
                "            def arg15(cls, e, f, g, h, i, j, k, l, m, n, o, p ,q ,r,s):\n"
                "                a = 1\n"
                "                b = 2\n"
                "                c = 3\n"
                "                d = 4\n"
                "                return a + b + c + d + e + f + g + h + i + j + k + l + m + n + o + p + q + r + s\n"
                "        a = 10000\n"
                "        return F.arg15(a, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19)");
        CHECK(t.returns() == "10185");
    }
}