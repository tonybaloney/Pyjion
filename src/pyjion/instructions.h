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
#ifndef PYJION_INSTRUCTIONS_H
#define PYJION_INSTRUCTIONS_H

#include <vector>
#include <Python.h>
#include <unordered_map>
#include "stack.h"
#include "types.h"

using namespace std;

#define SIZEOF_CODEUNIT sizeof(_Py_CODEUNIT)
#define GET_OPARG(index)  (py_oparg)_Py_OPARG(mByteCode[(index)/SIZEOF_CODEUNIT])
#define GET_OPCODE(index) (py_opcode)_Py_OPCODE(mByteCode[(index)/SIZEOF_CODEUNIT])

struct PyjionInstruction {
    size_t index;
    py_opcode opcode;
    py_oparg oparg;
};

struct Node {
    size_t index;
    py_opcode opcode;
    py_oparg oparg;
};

struct Edge {
    ssize_t from;
    size_t to;
    const char* label;
    AbstractValue* value;
    AbstractSource* source;
    bool escaped;
};

class InstructionGraph {
private:
    unordered_map<size_t, PyjionInstruction> m_instructions;
    vector<Node> nodes;
    vector<Edge> edges;
public:
    InstructionGraph(PyCodeObject* code, unordered_map<size_t, const InterpreterStack*> stacks) ;
    PyjionInstruction & operator [](size_t i) {return m_instructions[i];}
    size_t size() {return m_instructions.size();}
    void printGraph(const char* name) ;
};

#endif //PYJION_INSTRUCTIONS_H
