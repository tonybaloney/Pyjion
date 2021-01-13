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
        auto t = EmissionTest("def f(): return [1, {2}, 3]");
        CHECK(t.returns() == "[1, {2}, 3]");
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

    SECTION("range case") {
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

    SECTION("test inline"){
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
    SECTION("string unpack"){
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

TEST_CASE("Dict Merging"){
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
}

TEST_CASE("Assertions") {
    SECTION("assert simple case") {
        auto t = EmissionTest("def f(): assert '1' == '2'");
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
}