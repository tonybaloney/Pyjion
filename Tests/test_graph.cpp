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

    bool assertInstruction(size_t n, py_opcode opcode, py_oparg oparg, bool escaped){
        auto i = instruction(n);
        return (i.escape == escaped && i.opcode == opcode && i.index == n && i.oparg == oparg);
    }

    bool assertEdgesIn(py_opindex idx, size_t count){
        auto edges = m_graph->getEdges(idx);
        return (edges.size() == count);
    }

    bool assertEdgeInIs(py_opindex idx, size_t position, EscapeTransition transition){
        auto edges = m_graph->getEdges(idx);
        return edges[position].escaped == transition;
    }

    bool assertEdgesOut(py_opindex idx, size_t count){
        auto edges = m_graph->getEdgesFrom(idx);
        return (edges.size() == count);
    }

    bool assertEdgeOutIs(py_opindex idx, size_t position, EscapeTransition transition){
        auto edges = m_graph->getEdgesFrom(idx);
        return edges[position].escaped == transition;
    }
};

TEST_CASE("Test unsupported instructions"){
    SECTION("return parameters"){
        auto t = InstructionGraphTest("def f(x):\n"
                                      "  return x\n",
                                      "return parameters");
        CHECK(t.size() == 2);
        CHECK(t.assertInstruction(0, LOAD_FAST, 0, false));
        CHECK(t.assertEdgesIn(0, 0));
        CHECK(t.assertEdgesOut(0, 1));

        CHECK(t.assertInstruction(2, RETURN_VALUE, 0, false));
        CHECK(t.assertEdgesIn(2, 1));
        CHECK(t.assertEdgesOut(2, 0));
    }

    SECTION("assert unboxable"){
        auto t = InstructionGraphTest("def f(x):\n"
                                      "  assert '1' == '2'\n",
                                      "assert_unboxable");
        CHECK(t.size() == 8);
        CHECK(t.assertInstruction(0, LOAD_CONST, 1, false));
        CHECK(t.assertEdgesIn(0, 0));
        CHECK(t.assertEdgesOut(0, 1));

        CHECK(t.assertInstruction(6, POP_JUMP_IF_TRUE, 12, false));
        CHECK(t.assertEdgesIn(6, 1));
        CHECK(t.assertEdgeInIs(6, 0, NoEscape));
        CHECK(t.assertEdgesOut(6, 0));
    }

    SECTION("assert boxable consts"){
        auto t = InstructionGraphTest("def f(x):\n"
                                      "  assert 1000 == 2000\n",
                                      "assert_boxable_consts");
        CHECK(t.size() == 8);
        CHECK(t.assertInstruction(0, LOAD_CONST, 1, true)); // 1000 should be unboxed
        CHECK(t.assertEdgesIn(0, 0));
        CHECK(t.assertEdgesOut(0, 1));
        CHECK(t.assertInstruction(2, LOAD_CONST, 2, true)); // 2000 should be unboxed
        CHECK(t.assertEdgesIn(2, 0));
        CHECK(t.assertEdgesOut(2, 1));
        CHECK(t.assertInstruction(4, COMPARE_OP, 2, true)); // == should be unboxed
        CHECK(t.assertEdgesIn(4, 2));
        CHECK(t.assertEdgeInIs(4, 0, Unboxed));
        CHECK(t.assertEdgeInIs(4, 1, Unboxed));
        CHECK(t.assertEdgeOutIs(4, 0, Unboxed));
        CHECK(t.assertEdgesOut(4, 1));
        CHECK(t.assertInstruction(6, POP_JUMP_IF_TRUE, 12, true)); // should be unboxed
        CHECK(t.assertEdgesIn(6, 1));
        CHECK(t.assertEdgeInIs(6, 0, Unboxed));
        CHECK(t.assertEdgesOut(6, 0));
    }
}