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
#include "absvalue.h"
#include "types.h"

using namespace std;

#define SIZEOF_CODEUNIT sizeof(_Py_CODEUNIT)
#define GET_OPARG(index)  (py_oparg)_Py_OPARG(mByteCode[(index)/SIZEOF_CODEUNIT])
#define GET_OPCODE(index) (py_opcode)_Py_OPCODE(mByteCode[(index)/SIZEOF_CODEUNIT])

struct InterpreterStack; // forward decl

enum EscapeTransition {
    // Boxed -> Boxed = NoEscape
    // Boxed -> Unboxed = Unbox
    // Unboxed -> Boxed = Box
    // Unboxed -> Unboxed = Unboxed
    NoEscape = 1,
    Box = 2,
    Unbox = 3,
    Unboxed = 4
};

struct Instruction {
    py_opindex index;
    py_opcode opcode;
    py_oparg oparg;
    bool escape = false;
    bool deoptimized = false;
};

struct Edge {
    py_opindex from;
    py_opindex to;
    const char* label;
    AbstractValue* value;
    AbstractSource* source;
    EscapeTransition escaped;
    AbstractValueKind kind;
    py_opindex position;
};

typedef unordered_map<py_opindex, Edge> EdgeMap;

class InstructionGraph {
private:
    PyCodeObject * code;
    bool invalid = false;
    unordered_map<py_opindex, Instruction> instructions;
    unordered_map<py_oparg, AbstractValueKind> unboxedFastLocals ;
    vector<Edge> edges;
    void fixEdges();
    void fixInstructions();
    void deoptimizeInstructions();
    void fixLocals(py_oparg startIdx, py_oparg endIdx);
public:
    InstructionGraph(PyCodeObject* code, unordered_map<py_opindex, const InterpreterStack*> stacks) ;
    Instruction & operator [](py_opindex i) {return instructions[i];}
    size_t size() {return instructions.size();}
    PyObject* makeGraph(const char* name) ;
    vector<Edge> getEdges(py_opindex i);
    vector<Edge> getEdgesFrom(py_opindex i);
    unordered_map<py_oparg, AbstractValueKind> getUnboxedFastLocals();
    bool isValid() const;
};

#endif //PYJION_INSTRUCTIONS_H
