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
#include <Python.h>

TEST_CASE("General list unpacking") {
    SECTION("common case") {
        auto t = EmissionTest("def f(): return [1, *[2], 3, 4]");
        CHECK(t.returns() == "[1, 2, 3, 4]");
    }

    SECTION("unpacking an iterable") {
        auto t = EmissionTest("def f(): return [1, *{2}, 3]");
        CHECK(t.returns() == "[1, 2, 3]");
    }
}

TEST_CASE("General list indexing") {
    SECTION("common case") {
        auto t = EmissionTest("def f(): l = [4,3,2,1,0]; return l[0]");
        CHECK(t.returns() == "4");
    }

    SECTION("var case") {
        auto t = EmissionTest("def f(): i =2 ; l = [4,3,2,1,0]; return l[i]");
        CHECK(t.returns() == "2");
    }

    SECTION("negative case") {
        auto t = EmissionTest("def f(): l = [4,3,2,1,0]; return l[-1]");
        CHECK(t.returns() == "0");
    }

    SECTION("local case") {
        auto t = EmissionTest("def f(): l = [0]; a = 1; a -= 1; return l[a]");
        CHECK(t.returns() == "0");
    }

    SECTION("reverse slice case") {
        auto t = EmissionTest("def f(): l = [4,3,2,1,0]; return l[::-1]");
        CHECK(t.returns() == "[0, 1, 2, 3, 4]");
    }
}

TEST_CASE("General tuple indexing") {
    SECTION("common case") {
        auto t = EmissionTest("def f(): l = (4,3,2,1,0); return l[0]");
        CHECK(t.returns() == "4");
    }

    SECTION("var case") {
        auto t = EmissionTest("def f(): i =2 ; l = (4,3,2,1,0); return l[i]");
        CHECK(t.returns() == "2");
    }

    SECTION("negative case") {
        auto t = EmissionTest("def f(): l = (4,3,2,1,0); return l[-1]");
        CHECK(t.returns() == "0");
    }

    SECTION("range case") {
        auto t = EmissionTest("def f(): l = (4,3,2,1,0); return l[::-1]");
        CHECK(t.returns() == "(0, 1, 2, 3, 4)");
    }
}


TEST_CASE("List assignments from const values") {
    SECTION("common case") {
        auto t = EmissionTest("def f():\n"
                              " a = ['v']\n"
                              " a[0] = 'a'\n"
                              " return a");
        CHECK(t.returns() == "['a']");
    }
}

TEST_CASE("General dict comprehensions") {
    SECTION("common case") {
        auto t = EmissionTest("def f():\n  dict1 = {'a': 1, 'b': 2, 'c': 3, 'd': 4, 'e': 5}\n  return {k : v * 2 for k,v in dict1.items()}\n");
        CHECK(t.returns() == "{'a': 2, 'b': 4, 'c': 6, 'd': 8, 'e': 10}");
    }

    SECTION("more complex case") {
        auto t = EmissionTest("def f():\n  return dict({k: v for k, v in enumerate((1,2,3,))})");
        CHECK(t.returns() == "{0: 1, 1: 2, 2: 3}");
    }

    SECTION("test inline") {
        auto t = EmissionTest("def f():\n  return {k: k + 10 for k in range(10)}");
        CHECK(t.returns() == "{0: 10, 1: 11, 2: 12, 3: 13, 4: 14, 5: 15, 6: 16, 7: 17, 8: 18, 9: 19}");
    }
}

TEST_CASE("General tuple unpacking") {
    SECTION("common case") {
        auto t = EmissionTest("def f(): return (1, *(2,), 3)");
        CHECK(t.returns() == "(1, 2, 3)");
    }

    SECTION("unpacking a non-iterable") {
        auto t = EmissionTest("def f(): return (1, *2, 3)");
        CHECK(t.raises() == PyExc_TypeError);
    }
}

TEST_CASE("General list building") {
    SECTION("static list") {
        auto t = EmissionTest("def f(): return [1, 2, 3]");
        CHECK(t.returns() == "[1, 2, 3]");
    }

    SECTION("combine lists") {
        auto t = EmissionTest("def f(): return [1,2,3] + [4,5,6]");
        CHECK(t.returns() == "[1, 2, 3, 4, 5, 6]");
    }
}

TEST_CASE("General list comprehensions") {
    SECTION("static list comprehension") {
        auto t = EmissionTest("def f(): zzzs=(1,2,3) ; return [z for z in zzzs]");
        CHECK(t.returns() == "[1, 2, 3]");
    }

    SECTION("functional list comprehension") {
        auto t = EmissionTest("def f(): return [i for i in range(6)]");
        CHECK(t.returns() == "[0, 1, 2, 3, 4, 5]");
    }
}

TEST_CASE("General set building") {
    SECTION("frozenset") {
        auto t = EmissionTest("def f(): return {1, 2, 3}");
        CHECK(t.returns() == "{1, 2, 3}");
    }

    SECTION("combine sets") {
        auto t = EmissionTest("def f(): return {1, 2, 3} | {4, 5, 6}");
        CHECK(t.returns() == "{1, 2, 3, 4, 5, 6}");
    }

    SECTION("and operator set") {
        auto t = EmissionTest("def f(): return {1, 2, 3, 4} & {4, 5, 6}");
        CHECK(t.returns() == "{4}");
    }
}

TEST_CASE("General set comprehensions") {
    SECTION("simple setcomp") {
        auto t = EmissionTest("def f(): return {i for i in range(5)}");
        CHECK(t.returns() == "{0, 1, 2, 3, 4}");
    }
}

TEST_CASE("General method calls") {
    SECTION("easy case") {
        auto t = EmissionTest("def f(): a=set();a.add(1);return a");
        CHECK(t.returns() == "{1}");
    }
    SECTION("common case") {
        auto t = EmissionTest("def f(): a={False};a.add(True);return a");
        CHECK(t.returns() == "{False, True}");
    }
    SECTION("zero-arg case") {
        auto t = EmissionTest("def f(): a={False};a.add(True);a.pop(); return a");
        CHECK(t.returns() == "{True}");
    }
    SECTION("failure case") {
        auto t = EmissionTest("def f(): a={False};a.add([True]);return a");
        CHECK(t.raises() == PyExc_TypeError);
    }
}

TEST_CASE("General set unpacking") {
    SECTION("string unpack") {
        auto t = EmissionTest("def f(): return {*'oooooo'}");
        CHECK(t.returns() == "{'o'}");
    }

    SECTION("common case") {
        auto t = EmissionTest("def f(): return {1, *[2], 3}");
        CHECK(t.returns() == "{1, 2, 3}");
    }

    SECTION("unpacking a non-iterable") {
        auto t = EmissionTest("def f(): return {1, [], 3}");
        CHECK(t.raises() == PyExc_TypeError);
    }
}

TEST_CASE("General dict building") {
    SECTION("common case") {
        auto t = EmissionTest("def f(): return {1:'a', 2: 'b', 3:'c'}");
        CHECK(t.returns() == "{1: 'a', 2: 'b', 3: 'c'}");
    }
    SECTION("common case in function") {
        auto t = EmissionTest("def f(): \n"
                              "  def g(a, b, c):\n"
                              "     return {'a': a, 'b': b, 'c': c}\n"
                              "  return g(1,2,3) | g(1,2,3)");
        CHECK(t.returns() == "{'a': 1, 'b': 2, 'c': 3}");
    }
    SECTION("key add case") {
        auto t = EmissionTest("def f():\n  a = {1:'a', 2: 'b', 3:'c'}\n  a[4]='d'\n  return a");
        CHECK(t.returns() == "{1: 'a', 2: 'b', 3: 'c', 4: 'd'}");
    }
    SECTION("init") {
        auto t = EmissionTest("def f():\n  a = dict()\n  a[4]='d'\n  return a");
        CHECK(t.returns() == "{4: 'd'}");
    }
    SECTION("subclass") {
        auto t = EmissionTest("def f():\n"
                              "    class MyDict(dict):\n"
                              "       def __setitem__(self, key, value):\n"
                              "           super().__setitem__(key.upper(), value * 2)\n"
                              "    x = MyDict()\n"
                              "    x['a'] = 2\n"
                              "    return x");
        CHECK(t.returns() == "{'A': 4}");
    }
}

TEST_CASE("General dict unpacking") {
    SECTION("common case") {
        auto t = EmissionTest("def f(): return {'c': 'carrot', **{'b': 'banana'}, 'a': 'apple'}");
        CHECK(t.returns() == "{'c': 'carrot', 'b': 'banana', 'a': 'apple'}");
    }

    SECTION("unpacking a non-mapping") {
        auto t = EmissionTest("def f(): return {1:'a', **{2}, 3:'c'}");
        CHECK(t.raises() == PyExc_TypeError);
    }
}

TEST_CASE("Dict Merging") {
    SECTION("merging a dict with | operator") {
        auto t = EmissionTest("def f(): \n  a=dict()\n  b=dict()\n  a['x']=1\n  b['y']=2\n  return a | b");
        CHECK(t.returns() == "{'x': 1, 'y': 2}");
    }

    SECTION("merging a dict with |= operator") {
        auto t = EmissionTest("def f(): \n  a=dict()\n  b=dict()\n  a['x']=1\n  b['y']=2\n  a |= b\n  return a");
        CHECK(t.returns() == "{'x': 1, 'y': 2}");
    }

    SECTION("merging a dict and a list<tuple> with |= operator") {
        auto t = EmissionTest("def f(): \n  a=dict()\n  b=dict()\n  a['x']=1\n  b=[('x', 'y')]\n  a |= b\n  return a");
        CHECK(t.returns() == "{'x': 'y'}");
    }
}

TEST_CASE("General is comparison") {
    SECTION("common case") {
        auto t = EmissionTest("def f(): return 1 is 2");
        CHECK(t.returns() == "False");
    }

    SECTION("common not case") {
        auto t = EmissionTest("def f(): return 1 is not 2");
        CHECK(t.returns() == "True");
    }
}

TEST_CASE("General contains comparison") {
    SECTION("in case") {
        auto t = EmissionTest("def f(): return 'i' in 'team'");
        CHECK(t.returns() == "False");
    }
    SECTION("not in case") {
        auto t = EmissionTest("def f(): return 'i' not in 'team'");
        CHECK(t.returns() == "True");
    }
    SECTION("not in fault case") {
        auto t = EmissionTest("def f():\n"
                              " x = [0, 1, 2]\n"
                              " if x not in 'team':\n"
                              "   return True\n"
                              " else:\n"
                              "   return False\n");
        CHECK(t.raises() == PyExc_TypeError);
    }
}

TEST_CASE("Assertions") {
    SECTION("assert simple case") {
        auto t = EmissionTest("def f(): a = 1 ; assert '1' == '2'");
        CHECK(t.raises() == PyExc_AssertionError);
    }
    SECTION("assert simple case short int") {
        auto t = EmissionTest("def f(): assert 1 == 2");
        CHECK(t.raises() == PyExc_AssertionError);
    }
    SECTION("assert simple case long int") {
        auto t = EmissionTest("def f(): assert 1000000000 == 200000000");
        CHECK(t.raises() == PyExc_AssertionError);
    }
}

TEST_CASE("Binary subscripts") {
    SECTION("assert simple case") {
        auto t = EmissionTest("def f(): x = {'y': 12345.0}; return int(x['y'])");
        CHECK(t.returns() == "12345");
    }
    SECTION("assert scope case") {
        auto t = EmissionTest("def f():\n  x = {'y': 12345.0, 'z': 1234}\n  return int(x['y'])\n");
        CHECK(t.returns() == "12345");
    }
}

TEST_CASE("*args and **kwargs") {
    SECTION("assert *args as sequence") {
        auto t = EmissionTest("def f():\n"
                              "  def g(*args):\n"
                              "     return '-'.join(str(arg) for arg in args)\n"
                              "  return g(1,2,3)\n");
        CHECK(t.returns() == "'1-2-3'");
    }
    SECTION("assert *args as iterator") {
        auto t = EmissionTest("def f():\n"
                              "  sep = '-'\n"
                              "  def g(*args):\n"
                              "     return sep.join([str(arg) for arg in args if arg % 2 ])\n"
                              "  return g(1,2,3)\n");
        CHECK(t.returns() == "'1-3'");
    }
    SECTION("assert **kwargs as dict") {
        auto t = EmissionTest("def f():\n"
                              "  def g(**kwargs):\n"
                              "     return kwargs['x']\n"
                              "  return g(x=1)\n");
        CHECK(t.returns() == "1");
    }
}

TEST_CASE("Iterators") {
    SECTION("list iterator") {
        auto t = EmissionTest("def f():\n"
                              " x = ['1', '2', '3']\n"
                              " total = 0\n"
                              " for y in x:\n"
                              "   total += int(y)\n"
                              " return total");
        CHECK(t.returns() == "6");
    }

    SECTION("extended list iterator") {
        auto t = EmissionTest("def f():\n"
                              " x = ['1', '2', '3']\n"
                              " x.append('4')\n"
                              " total = 0\n"
                              " for y in x:\n"
                              "   total += int(y)\n"
                              " return total");
        CHECK(t.returns() == "10");
    }

    SECTION("nested list iterator") {
        auto t = EmissionTest("def f():\n"
                              " x = ['1', '2', '3']\n"
                              " y = ['4', '5', '6']\n"
                              " x.append('4')\n"
                              " total = 0\n"
                              " for i in x:\n"
                              "  for j in y:\n"
                              "   total += int(i) + int(j)\n"
                              " return total");
        CHECK(t.returns() == "90");
    }
    SECTION("numeric list iterator") {
        auto t = EmissionTest("def f():\n"
                              " x = [1, 2, 3]\n"
                              " y = [4, 5, 6]\n"
                              " x.append(4)\n"
                              " total = 0\n"
                              " for i in x:\n"
                              "  for j in y:\n"
                              "   total += i + j\n"
                              " return total");
        CHECK(t.returns() == "90");
    }

    SECTION("tuple iterator") {
        auto t = EmissionTest("def f():\n"
                              " x = ('1', '2', '3')\n"
                              " total = 0\n"
                              " for y in x:\n"
                              "   total += int(y)\n"
                              " return total");
        CHECK(t.returns() == "6");
    }

    SECTION("test changing types iterator") {
        auto t = EmissionTest(
                "def f():\n"
                "  def _sum(s):\n"
                "     tot = 0\n"
                "     for i in s:\n"
                "       tot += i\n"
                "     return tot\n"
                "  v = _sum((0,1,2)) + _sum([0,1,2])\n"
                "  return v\n");
        CHECK(t.returns() == "6");
    };
}

TEST_CASE("Binary slice subscripts") {
    SECTION("assert list case") {
        auto t = EmissionTest("def f(): l = [0,1,2,3]; return l[0:1]");
        CHECK(t.returns() == "[0]");
    }
    SECTION("assert list case empty start") {
        auto t = EmissionTest("def f(): l = [0,1,2,3]; return l[:1]");
        CHECK(t.returns() == "[0]");
    }
    SECTION("assert list case empty end") {
        auto t = EmissionTest("def f(): l = [0,1,2,3]; return l[1:]");
        CHECK(t.returns() == "[1, 2, 3]");
    }
    SECTION("assert list case empty both") {
        auto t = EmissionTest("def f(): l = [0,1,2,3]; return l[:]");
        CHECK(t.returns() == "[0, 1, 2, 3]");
    }
    SECTION("assert list case negatives") {
        auto t = EmissionTest("def f(): l = [0,1,2,3]; return l[-2:-1]");
        CHECK(t.returns() == "[2]");
    }
    SECTION("assert list cross negatives") {
        auto t = EmissionTest("def f(): l = [0,1,2,3]; return l[-1:-2]");
        CHECK(t.returns() == "[]");
    }
    SECTION("assert list negative start") {
        auto t = EmissionTest("def f(): l = [0,1,2,3]; return l[-1:]");
        CHECK(t.returns() == "[3]");
    }
    SECTION("assert list negative end") {
        auto t = EmissionTest("def f(): l = [0,1,2,3]; return l[:-1]");
        CHECK(t.returns() == "[0, 1, 2]");
    }
    SECTION("assert list case missing step") {
        auto t = EmissionTest("def f(): l = [0,1,2,3]; return l[0:1:]");
        CHECK(t.returns() == "[0]");
    }
    SECTION("assert list case const step") {
        auto t = EmissionTest("def f(): l = [0,1,2,3]; return l[0:1:1]");
        CHECK(t.returns() == "[0]");
    }
    SECTION("assert list case step 1") {
        auto t = EmissionTest("def f(): l = [0,1,2,3]; return l[::1]");
        CHECK(t.returns() == "[0, 1, 2, 3]");
    }
    SECTION("assert list case step back") {
        auto t = EmissionTest("def f(): l = [0,1,2,3]; return l[::-1]");
        CHECK(t.returns() == "[3, 2, 1, 0]");
    }
    SECTION("assert list case step back negative 2") {
        auto t = EmissionTest("def f(): l = [0,1,2,3]; return l[::-2]");
        CHECK(t.returns() == "[3, 1]");
    }
    SECTION("assert list case step two") {
        auto t = EmissionTest("def f(): l = [0,1,2,3]; return l[::2]");
        CHECK(t.returns() == "[0, 2]");
    }
    SECTION("assert list weird indexes") {
        auto t = EmissionTest("def f(): l = [0,1,2,3]; return l[False:True]");
        CHECK(t.returns() == "[0]");
    }
    SECTION("assert complex scenario") {
        auto t = EmissionTest("def f(): return 'The train to Oxford leaves at 3pm'[-1:3:-2]");
        CHECK(t.returns() == "'m3t ealdox tnat'");
    }
    SECTION("assert assign slice from slice") {
        auto t = EmissionTest("def f(): l = [0,1,2,3,4]; l[:2] = l[1::-1]; return l");
        CHECK(t.returns() == "[1, 0, 2, 3, 4]");
    }
    SECTION("assert assign slice from var") {
        auto t = EmissionTest("def f(x): l = [0,1,2,3,4]; x=len(l); l[:2] = l[x::-1]; return l");
        CHECK(t.returns() == "[4, 3, 2, 1, 0, 2, 3, 4]");
    }
}

TEST_CASE("Simple methods") {
    SECTION("assert simple string case") {
        auto t = EmissionTest("def f(): x = 'hello'; return x.upper()");
        CHECK(t.returns() == "'HELLO'");
    }

    SECTION("assert simple dict case") {
        auto t = EmissionTest("def f():\n"
                              "    l = {'a': 1, 'b': 2}\n"
                              "    k = l.keys()\n"
                              "    return tuple(k)");
        CHECK(t.returns() == "('a', 'b')");
    }

    SECTION("assert simple string case twice ") {
        auto t = EmissionTest("def f(): \n"
                              "   x = 'hello'.upper()\n"
                              "   for i in range(0,2):\n"
                              "      x += x.upper()\n"
                              "   return x");
        CHECK(t.returns() == "'HELLOHELLOHELLOHELLO'");
    }

    SECTION("test non existent method attribute raises AttributeError") {
        auto t = EmissionTest("def f():\n"
                              "    l = {'a': 1, 'b': 2}\n"
                              "    k = l.sdfff()\n"
                              "    return tuple(k)");
        CHECK(t.raises() == PyExc_AttributeError);
    }
}

TEST_CASE("Test nested stacks") {
    SECTION("assert nested method optimized case") {
        auto t = EmissionTest("def f():\n"
                              "    l = {'a': 1, 'b': 2}\n"
                              "    return tuple(l.keys())");
        CHECK(t.returns() == "('a', 'b')");
    }
    SECTION("assert double nested method optimized case") {
        auto t = EmissionTest("def f():\n"
                              "    l = {'a': 1, 'b': 2}\n"
                              "    return tuple(tuple(l.keys()))");
        CHECK(t.returns() == "('a', 'b')");
    }
}

TEST_CASE("Type object methods") {
    SECTION("assert type case") {
        auto t = EmissionTest("def f(): return int.__format__(2, '%')");
        CHECK(t.returns() == "'200.000000%'");
    }
    SECTION("assert pow with mixed locals") {
        auto t = EmissionTest(
                "def f():\n"
                "   f = 12\n"
                "   x = 'test'\n"
                "   x = 4\n"
                "   return pow(f, x, 100)");
        CHECK(t.returns() == "36");
    }

    SECTION("assert int from_bytes instance") {
        auto t = EmissionTest(
                "def f():\n"
                "   f = 12\n"
                "   return f.from_bytes(b'1234', 'little')");
        CHECK(t.returns() == "875770417");
    }
    SECTION("assert const instance") {
        auto t = EmissionTest(
                "def f():\n"
                "   f = 1.1234e90\n"
                "   return f.__format__('f')");
        CHECK(t.returns() == "'1123400000000000059889518021533541968680969201463305742225773447091302986902992418794110976.000000'");
    }
}

TEST_CASE("Sequence binary operations") {
    SECTION("add two lists") {
        auto t = EmissionTest("def f(): return ['hello'] + ['world']");
        CHECK(t.returns() == "['hello', 'world']");
    }
    SECTION("assert multi list by number") {
        auto t = EmissionTest("def f(): return ['hello'] * 5");
        CHECK(t.returns() == "['hello', 'hello', 'hello', 'hello', 'hello']");
    }
    SECTION("assert multi list by number reversed") {
        auto t = EmissionTest("def f(): return 5* ['hello']");
        CHECK(t.returns() == "['hello', 'hello', 'hello', 'hello', 'hello']");
    }
    SECTION("assert multi list by complex number") {
        auto t = EmissionTest("def f(): return ['hello'] * int(5)");
        CHECK(t.returns() == "['hello', 'hello', 'hello', 'hello', 'hello']");
    }
    SECTION("assert multi list by complex number reversed") {
        auto t = EmissionTest("def f(): return int(5) * ['hello']");
        CHECK(t.returns() == "['hello', 'hello', 'hello', 'hello', 'hello']");
    }
    SECTION("assert multi letter by complex number") {
        auto t = EmissionTest("def f(): return 'a' * int(5)");
        CHECK(t.returns() == "'aaaaa'");
    }
}

TEST_CASE("Test type annotations") {
    SECTION("test variable definition with annotations") {
        auto t = EmissionTest(
                "def f():\n    x: int = 2\n    return x");
        CHECK(t.returns() == "2");
    }
    SECTION("test class definition with annotations") {
        auto t = EmissionTest(
                "def f():\n    class C:\n      property: int = 0\n    return C");
        CHECK(t.returns() == "<class 'C'>");
    }
    SECTION("test class definition with annotations called") {
        auto t = EmissionTest(
                "def f():\n    class C:\n      property: int = 0\n    return C().property");
        CHECK(t.returns() == "0");
    }
}

TEST_CASE("Test range function") {
    SECTION("test basic range") {
        auto t = EmissionTest(
                "def f():\n"
                "  x = []\n"
                "  for i in range(3):\n"
                "    x.append(i)\n"
                "  return x\n");
        CHECK(t.returns() == "[0, 1, 2]");
    }
    SECTION("test stop range") {
        auto t = EmissionTest(
                "def f():\n"
                "  x = []\n"
                "  for i in range(0, 3):\n"
                "    x.append(i)\n"
                "  return x\n");
        CHECK(t.returns() == "[0, 1, 2]");
    }
    SECTION("test start range") {
        auto t = EmissionTest(
                "def f():\n"
                "  x = []\n"
                "  for i in range(1,3):\n"
                "    x.append(i)\n"
                "  return x\n");
        CHECK(t.returns() == "[1, 2]");
    }
    SECTION("test step range") {
        auto t = EmissionTest(
                "def f():\n"
                "  x = []\n"
                "  for i in range(0,4,2):\n"
                "    x.append(i)\n"
                "  return x\n");
        CHECK(t.returns() == "[0, 2]");
    }
    SECTION("test start stop step range") {
        auto t = EmissionTest(
                "def f():\n"
                "  x = []\n"
                "  for i in range(2,6,2):\n"
                "    x.append(i)\n"
                "  return x\n");
        CHECK(t.returns() == "[2, 4]");
    }
    SECTION("test big range", "[slow]"){
        auto t = EmissionTest(
                "def f():\n"
                "  x = 0\n"
                "  for i in range(100000):\n"
                "    x = i\n"
                "  return x\n");
        CHECK(t.returns() == "99999");
    }
}

TEST_CASE("Test bytearray") {
    SECTION("test slice const index") {
        auto t = EmissionTest(
                "def f():\n"
                "  x = bytearray(b'12')\n"
                "  return x[0]\n");
        CHECK(t.returns() == "49");
    }
    SECTION("test slice const index 2") {
        auto t = EmissionTest(
                "def f():\n"
                "  x = bytearray(b'12')\n"
                "  return x[1]\n");
        CHECK(t.returns() == "50");
    }
    SECTION("test slice var index") {
        auto t = EmissionTest(
                "def f():\n"
                "  x = bytearray(b'12')\n"
                "  return x[int('0')]\n");
        CHECK(t.returns() == "49");
    }
    SECTION("test slice index error") {
        auto t = EmissionTest(
                "def f():\n"
                "  x = bytearray(b'12')\n"
                "  return x[2]\n");
        CHECK(t.raises() == PyExc_IndexError);
    }
    SECTION("test slice -ve index error") {
        auto t = EmissionTest(
                "def f():\n"
                "  x = bytearray(b'12')\n"
                "  return x[-1]\n");
        CHECK(t.raises() == PyExc_IndexError);
    }
    SECTION("test bytearray indexes") {
        auto t = EmissionTest(
                "def f():\n"
                "  x = bytearray(2)\n"
                "  x[0] = 255\n"
                "  x[1] = 155\n"
                "  return x[0], x[1]\n");
        CHECK(t.returns() == "(255, 155)");
    }
}