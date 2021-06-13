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
 Test type inferencing.

 Many tests use a style of test ported over from an old test runner. Any new
 set of tests should write their own testing code that would be easier to work
 with.
*/
#include <catch2/catch.hpp>
#include "testing_util.h"
#include <Python.h>
#include <absint.h>
#include <instructions.h>
#include <memory>
#include <util.h>

class InstructionGraphTest {
private:
    std::unique_ptr<AbstractInterpreter> m_absint;
    InstructionGraph* m_graph;

public:
    explicit InstructionGraphTest(const char* code, const char* name) {
        auto pyCode = CompileCode(code);
        m_absint = std::make_unique<AbstractInterpreter>(pyCode, nullptr);
        auto builtins = PyEval_GetBuiltins();
        auto globals_dict = PyObject_ptr(PyDict_New());
        auto profile = new PyjionCodeProfile();
        auto success = m_absint->interpret(builtins, globals_dict.get(), profile, Uncompiled);
        delete profile;
        if (success != Success) {
            Py_DECREF(pyCode);
            FAIL("Failed to interpret code");
        }
        m_graph = m_absint->buildInstructionGraph();
        m_graph->printGraph(name);
    }

    ~InstructionGraphTest(){
        delete m_graph;
    }

    size_t size(){
        return m_graph->size();
    }

    Instruction instruction(size_t n){
        return m_graph->operator[](n);
    }

    void assertInstruction(size_t n, py_opcode opcode, py_oparg oparg, bool escaped){
        auto i = instruction(n);
        CHECK(i.escape == escaped);
        CHECK(i.opcode == opcode);
        CHECK(i.index == n);
        CHECK(i.oparg == oparg);
    }

    size_t edgesIn(py_opindex idx){
        auto edges = m_graph->getEdges(idx);
        return edges.size();
    }

    EscapeTransition edgeInIs(py_opindex idx, size_t position){
        auto edges = m_graph->getEdges(idx);
        return edges[position].escaped;
    }

    size_t edgesOut(py_opindex idx){
        auto edges = m_graph->getEdgesFrom(idx);
        return edges.size();
    }

    EscapeTransition edgeOutIs(py_opindex idx, size_t position){
        auto edges = m_graph->getEdgesFrom(idx);
        return edges[position].escaped;
    }
};

TEST_CASE("Test instruction graphs"){
    SECTION("return parameters"){
        auto t = InstructionGraphTest("def f(x):\n"
                                      "  return x\n",
                                      "return_parameters");
        CHECK(t.size() == 2);
        t.assertInstruction(0, LOAD_FAST, 0, false);
        CHECK(t.edgesIn(0) == 0);
        CHECK(t.edgesOut(0) == 1);

        t.assertInstruction(2, RETURN_VALUE, 0, false);
        CHECK(t.edgesIn(2) == 1);
        CHECK(t.edgesOut(2) == 0);
    }

    SECTION("assert unboxable"){
        auto t = InstructionGraphTest("def f(x):\n"
                                      "  assert '1' == '2'\n",
                                      "assert_unboxable");
        CHECK(t.size() == 8);
        t.assertInstruction(0, LOAD_CONST, 1, false);
        CHECK(t.edgesIn(0) == 0);
        CHECK(t.edgesOut(0) == 1);

        t.assertInstruction(6, POP_JUMP_IF_TRUE, 12, false);
        CHECK(t.edgesIn(6) == 1);
        CHECK(t.edgeInIs(6, 0) == NoEscape);
        CHECK(t.edgesOut(6) == 0);
    }

    SECTION("assert boxable consts"){
        auto t = InstructionGraphTest("def f(x):\n"
                                      "  assert 1000 == 2000\n",
                                      "assert_boxable_consts");
        CHECK(t.size() == 8);
        t.assertInstruction(0, LOAD_CONST, 1, true); // 1000 should be unboxed
        CHECK(t.edgesIn(0) == 0);
        CHECK(t.edgesOut(0) == 1);
        t.assertInstruction(2, LOAD_CONST, 2, true); // 2000 should be unboxed
        CHECK(t.edgesIn(2) == 0);
        CHECK(t.edgesOut(2) == 1);
        t.assertInstruction(4, COMPARE_OP, 2, true); // == should be unboxed
        CHECK(t.edgesIn(4) == 2);
        CHECK(t.edgeInIs(4, 0) == Unboxed);
        CHECK(t.edgeInIs(4, 1) == Unboxed);
        CHECK(t.edgeOutIs(4, 0) == Unboxed);
        CHECK(t.edgesOut(4) == 1);
        t.assertInstruction(6, POP_JUMP_IF_TRUE, 12, true); // should be unboxed
        CHECK(t.edgesIn(6) == 1);
        CHECK(t.edgeInIs(6, 0) == Unboxed);
        CHECK(t.edgesOut(6) == 0);
    }

    SECTION("test simple local graph isn't optimized") {
        auto t = InstructionGraphTest("def f(x):\n"
                                      "  x = len('help')\n"
                                      "  y = len('me')\n"
                                      "  return x == y\n",
                                      "assert_deopt_binary");
        CHECK(t.size() == 12);
        t.assertInstruction(20, COMPARE_OP, 2, false); // == should be boxed
    }

    SECTION("test COMPARE_OP doesn't get optimized with a POP_JUMP") {
        auto t = InstructionGraphTest("def f(x):\n"
                                      "  x = len('help')\n"
                                      "  y = len('me')\n"
                                      "  if x == y:\n"
                                      "     return False\n",
                                      "assert_deopt_binary_pop");
        CHECK(t.size() == 16);
        t.assertInstruction(20, COMPARE_OP, 2, false); // == should be boxed
        t.assertInstruction(22, POP_JUMP_IF_FALSE, 28, false); // JUMP should be boxed
    }

    SECTION("test JUMP_IF_FALSE_OR_POP doesn't get optimized and a confused graph") {
        auto t = InstructionGraphTest("def f(x):\n"
                                      "  return (len(name) > 2 and\n"
                                      "     name[0] == name[-1])\n",
                                      "assert_deopt_binary_pop");
        CHECK(t.size() == 14);
        t.assertInstruction(24, COMPARE_OP, 2, false); // == should be boxed
        CHECK(t.edgesOut(8) == 1);
        t.assertInstruction(10, JUMP_IF_FALSE_OR_POP, 26, false); // JUMP should be boxed
    }
}