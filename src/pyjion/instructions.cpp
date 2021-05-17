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

#include "instructions.h"
#include "pycomp.h"
#include "unboxing.h"


InstructionGraph::InstructionGraph(PyCodeObject *code, unordered_map<size_t, const InterpreterStack*> stacks) {
    auto mByteCode = (_Py_CODEUNIT *)PyBytes_AS_STRING(code->co_code);
    auto size = PyBytes_Size(code->co_code);
    for (size_t curByte = 0; curByte < size; curByte += SIZEOF_CODEUNIT) {
        auto index = curByte;
        auto opcode = GET_OPCODE(curByte);
        auto oparg = GET_OPARG(curByte);

        if (stacks[index] != nullptr){
            for (const auto & si: *stacks[index]){
                if (si.hasSource()){
                    if (si.Sources->isConsumedBy(index)) {
                        EscapeTransition transition;
                        if (!supportsEscaping(si.Value->kind())){
                            transition = NoEscape;
                        } else {
                            if (supportsUnboxing(GET_OPCODE(si.Sources->producer())) &&
                                supportsUnboxing(opcode))
                                transition = Unboxed;
                            else if (!supportsUnboxing(GET_OPCODE(si.Sources->producer())) &&
                                supportsUnboxing(opcode))
                                transition = Unbox;
                            else if (supportsUnboxing(GET_OPCODE(si.Sources->producer())) &&
                                !supportsUnboxing(opcode))
                                transition = Box;
                            else
                                transition = NoEscape;
                        }

                        edges.push_back({
                            .from = static_cast<ssize_t>(si.Sources->producer()),
                            .to = index,
                            .label = si.Sources->describe(),
                            .value = si.Value,
                            .source = si.Sources,
                            .escaped = transition
                        });
                    }
                }
            }
        }

        instructions[index] = {
            .index = index,
            .opcode = opcode,
            .oparg = oparg,
            .canEscape = supportsUnboxing(opcode)
        };
    }
}

void InstructionGraph::printGraph(const char* name) {
    printf("digraph %s { \n", name);
    printf("\tnode [shape=box];\n");
    printf("\tFRAME [label=FRAME];\n");
    for (const auto & node: instructions){
        if (supportsUnboxing(node.second.opcode))
            printf("  OP%zu [label=\"%s (%d)\" color=blue];\n", node.first, opcodeName(node.second.opcode), node.second.oparg);
        else
            printf("  OP%zu [label=\"%s (%d)\"];\n", node.first, opcodeName(node.second.opcode), node.second.oparg);
    }

    for (const auto & edge: edges){
        if (edge.from == -1) {
            printf("\tFRAME -> OP%zu [label=\"%s (%s)\"];\n", edge.to, edge.label, edge.value->describe());
        } else {
            switch (edge.escaped) {
                case NoEscape:
                    printf("\tOP%zd -> OP%zu [label=\"%s (%s) -\" color=black];\n", edge.from, edge.to, edge.label, edge.value->describe());
                    break;
                case Unbox:
                    printf("\tOP%zd -> OP%zu [label=\"%s (%s) U\" color=red];\n", edge.from, edge.to, edge.label, edge.value->describe());
                    break;
                case Box:
                    printf("\tOP%zd -> OP%zu [label=\"%s (%s) B\" color=green];\n", edge.from, edge.to, edge.label, edge.value->describe());
                    break;
                case Unboxed:
                    printf("\tOP%zd -> OP%zu [label=\"%s (%s) UN\" color=purple];\n", edge.from, edge.to, edge.label, edge.value->describe());
                    break;
            }

        }
    }
    printf("}\n");
}