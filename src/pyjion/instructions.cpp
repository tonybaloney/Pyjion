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

InstructionGraph::InstructionGraph(PyCodeObject *code, unordered_map<size_t, const InterpreterStack*> stacks) {
    auto mByteCode = (_Py_CODEUNIT *)PyBytes_AS_STRING(code->co_code);
    auto size = PyBytes_Size(code->co_code);
    for (size_t curByte = 0; curByte < size; curByte += SIZEOF_CODEUNIT) {
        auto index = curByte;
        auto opcode = GET_OPCODE(curByte);
        auto oparg = GET_OPARG(curByte);

        nodes.push_back({index, static_cast<int16_t>(opcode), static_cast<int16_t>(oparg)});

        if (stacks[index] != nullptr){
            for (const auto & si: *stacks[index]){
                if (si.hasSource()){
                    if (si.Sources->isConsumedBy(index)) {
                        edges.push_back({
                            .from = static_cast<ssize_t>(si.Sources->producer()),
                            .to = index,
                            .label = si.Sources->describe(),
                            .value = si.Value,
                            .source = si.Sources
                        });
                    }
                }
            }
        }

        m_instructions[index] = {
                .index = index,
                .opcode = static_cast<int16_t>(opcode),
                .oparg = static_cast<int16_t>(oparg)
        };
    }
}

void InstructionGraph::printGraph(const char* name) {
    printf("digraph %s { \n", name);
    printf("\tnode [shape=box];\n");
    printf("\tFRAME [label=FRAME];\n");
    for (const auto & node: nodes){
        printf("  OP%zu [label=\"%s (%d)\"];\n", node.index, opcodeName(node.opcode), node.oparg);
    }

    for (const auto & edge: edges){
        if (edge.from == -1) {
            printf("\tFRAME -> OP%zu [label=\"%s (%s)\"];\n", edge.to, edge.label, edge.value->describe());
        } else {
            printf("\tOP%zd -> OP%zu [label=\"%s (%s)\"];\n", edge.from, edge.to, edge.label, edge.value->describe());
        }
    }
    printf("}\n");
}