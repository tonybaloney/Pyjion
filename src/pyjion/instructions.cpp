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
                    ssize_t stackPosition = si.Sources->isConsumedBy(index);
                    if (stackPosition != -1) {
                        EscapeTransition transition;
                        if (!si.hasValue() || !supportsEscaping(si.Value->kind())){
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
                            .escaped = transition,
                            .kind = si.hasValue() ? si.Value->kind() : AVK_Any,
                            .position = static_cast<size_t>(stackPosition)
                        });
                    }
                }
            }
        }
        // Go back through the edges to this operation and inspect those that make no
        // sense to escape because the inputs are unknown.
        bool allEscaped = true;
        auto edgesForOperation = getEdges(index);
        for (auto & e: edgesForOperation){
            if (e.second.escaped == NoEscape)
                allEscaped = false;
        }
        instructions[index] = {
            .index = index,
            .opcode = opcode,
            .oparg = oparg,
            .escape = supportsUnboxing(opcode) && allEscaped
        };
    }
    for (auto & edge: this->edges){
        // If the instruction is no longer escaped, dont box the output
        if (!this->instructions[edge.from].escape) {
            // From non-escaped operation
            if (this->instructions[edge.to].escape){
                edge.escaped = Unbox;
            } else {
                edge.escaped = NoEscape;
            }
        } else {
            // From escaped operation
            if (this->instructions[edge.to].escape){
                edge.escaped = Unboxed;
            } else {
                edge.escaped = Box;
            }
        }
    }
}

void InstructionGraph::printGraph(const char* name) {
    printf("digraph %s { \n", name);
    printf("\tnode [shape=box];\n");
    printf("\tFRAME [label=FRAME];\n");
    for (const auto & node: instructions){
        if (node.second.escape)
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
                    printf("\tOP%zd -> OP%zu [label=\"%s (%s) -%zu\" color=black];\n", edge.from, edge.to, edge.label, edge.value->describe(), edge.position);
                    break;
                case Unbox:
                    printf("\tOP%zd -> OP%zu [label=\"%s (%s) U%zu\" color=red];\n", edge.from, edge.to, edge.label, edge.value->describe(), edge.position);
                    break;
                case Box:
                    printf("\tOP%zd -> OP%zu [label=\"%s (%s) B%zu\" color=green];\n", edge.from, edge.to, edge.label, edge.value->describe(), edge.position);
                    break;
                case Unboxed:
                    printf("\tOP%zd -> OP%zu [label=\"%s (%s) UN%zu\" color=purple];\n", edge.from, edge.to, edge.label, edge.value->describe(), edge.position);
                    break;
            }

        }
    }
    printf("}\n");
}

EdgeMap InstructionGraph::getEdges(size_t i){
    unordered_map<size_t, Edge> filteredEdges;
    for (auto & edge: this->edges){
        if (edge.to == i)
            filteredEdges[edge.position] = edge;
    }
    return filteredEdges;
}