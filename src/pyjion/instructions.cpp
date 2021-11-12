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
#include <set>

InstructionGraph::InstructionGraph(PyCodeObject* code, unordered_map<py_opindex, const InterpreterStack*> stacks, bool escapeLocals) {
    this->code = code;
    auto mByteCode = (_Py_CODEUNIT*) PyBytes_AS_STRING(code->co_code);
    auto size = PyBytes_Size(code->co_code);
    for (py_opindex curByte = 0; curByte < size; curByte += SIZEOF_CODEUNIT) {
        py_opindex index = curByte;
        auto opcode = GET_OPCODE(curByte);
        auto oparg = GET_OPARG(curByte);

        if (opcode == EXTENDED_ARG) {
            instructions[index] = {
                    .index = index,
                    .opcode = opcode,
                    .oparg = oparg,
                    .jumpsTo = jumpsTo(opcode, oparg, index),
                    .escape = false,
            };
            curByte += SIZEOF_CODEUNIT;
            oparg = (oparg << 8) | GET_OPARG(curByte);
            opcode = GET_OPCODE(curByte);
            index = curByte;
        }
        if (stacks[index] != nullptr) {
            for (const auto& si : *stacks[index]) {
                if (si.hasSource()) {
                    ssize_t stackPosition = si.Sources->isConsumedBy(index);
                    if (stackPosition != -1) {
                        edges.push_back({.from = si.Sources->producer(),
                                         .to = index,
                                         .label = si.Sources->describe(),
                                         .value = si.Value,
                                         .source = si.Sources,
                                         .escaped = NoEscape,
                                         .kind = si.hasValue() ? si.Value->kind() : AVK_Any,
                                         .position = static_cast<py_opindex>(stackPosition)});
                    }
                }
            }
        }
        instructions[index] = {
                .index = index,
                .opcode = opcode,
                .oparg = oparg,
                .jumpsTo = jumpsTo(opcode, oparg, index),
                .escape = false};
    }
    fixInstructions();
    if (escapeLocals) {
        fixLocals(code->co_argcount, code->co_nlocals);
    }
    deoptimizeInstructions();
    fixEdges();
}

void InstructionGraph::fixEdges() {
    for (auto& edge : this->edges) {
        if (!this->instructions[edge.from].escape) {
            // From non-escaped operation
            if (this->instructions[edge.to].escape && supportsEscaping(edge.kind)) {
                edge.escaped = Unbox;
            } else {
                edge.escaped = NoEscape;
            }
        } else {
            // From escaped operation
            if (this->instructions[edge.to].escape && supportsEscaping(edge.kind)) {
                edge.escaped = Unboxed;
            } else if (supportsEscaping(edge.kind)) {
                edge.escaped = Box;
            } else {
                edge.escaped = NoEscape;
            }
        }
    }
}

void InstructionGraph::fixInstructions() {
    for (auto& instruction : this->instructions) {
        if (!supportsUnboxing(instruction.second.opcode))
            continue;
        if (instruction.second.opcode == LOAD_FAST || instruction.second.opcode == STORE_FAST || instruction.second.opcode == DELETE_FAST)
            continue;// handled in fixLocals();

        // Check that all inbound edges can be escaped.
        bool allEdgesEscapable = true;
        auto edgesIn = getEdges(instruction.first);
        vector<AbstractValueKind> typesIn;
        for (auto& edgeIn : edgesIn) {
            typesIn.emplace_back(edgeIn.kind);
            if (!supportsEscaping(edgeIn.kind) && !unboxedArgument(edgeIn.kind))
                allEdgesEscapable = false;
        }
        if (!allEdgesEscapable)
            continue;

        // Check that all outbound edges can be escaped.
        bool allOutputsEscapable = true;
        for (auto& edgeOut : getEdgesFrom(instruction.first)) {
            if (!supportsEscaping(edgeOut.kind))
                allOutputsEscapable = false;
        }
        if (!allOutputsEscapable)
            continue;

        // Check the specifics of this opcode.
        if (!supportsUnboxing(instruction.second.opcode, typesIn))
            continue;

        // Otherwise, we can escape this instruction..
        instruction.second.escape = true;
    }
}

void InstructionGraph::deoptimizeInstructions() {
    for (auto& instruction : this->instructions) {
        if (!instruction.second.escape)
            continue;
        if (instruction.second.opcode == LOAD_FAST || instruction.second.opcode == STORE_FAST || instruction.second.opcode == DELETE_FAST)
            continue;// handled in fixLocals();
        if (instruction.second.opcode == FOR_ITER)
            continue;// handled in fixLocals();

        auto edgesIn = getEdges(instruction.first);
        auto edgesOut = getEdgesFrom(instruction.first);
        // If the stack effect is wrong..
        if (PyCompile_OpcodeStackEffect(instruction.second.opcode, instruction.second.oparg) != (edgesOut.size() - edgesIn.size())) {
#ifdef DEBUG_VERBOSE
            printf("Warning, instruction has invalid stack effect %s %d\n", opcodeName(instruction.second.opcode), instruction.second.index);
#endif
            invalid = true;
            instruction.second.escape = false;
            instruction.second.deoptimized = true;
            continue;
        }

        // If op has no inputs and only 1 output edge and the next instruction is not escaped.. dont
        if (edgesIn.empty() && edgesOut.size() == 1) {
            // Get next instruction
            if (!this->instructions[edgesOut[0].to].escape) {
                instruction.second.escape = false;
                instruction.second.deoptimized = true;
                continue;
            }
        }

        // If op has no outputs and only 1 input edge and the previous instruction is not escaped.. dont
        if (edgesIn.size() == 1 && edgesOut.empty()) {
            // Get previous instruction
            if (!this->instructions[edgesIn[0].from].escape) {
                instruction.second.escape = false;
                instruction.second.deoptimized = true;
                continue;
            }
        }

        // If none of the previous instructions are boxed and the next one's aren't either
        if (!edgesIn.empty() && !edgesOut.empty()) {
            auto previousOperationsBoxed = false;
            for (auto& edge : edgesIn) {
                if (this->instructions[edge.from].escape)
                    previousOperationsBoxed = true;
            }

            auto nextOperationsBoxed = false;
            for (auto& edge : edgesOut) {
                if (this->instructions[edge.to].escape)
                    nextOperationsBoxed = true;
            }

            if (!previousOperationsBoxed && !nextOperationsBoxed) {
                instruction.second.escape = false;
                instruction.second.deoptimized = true;
                continue;
            }
        }

        // If none of the previous instructions are boxed and the next is a pop operation
        if (!edgesIn.empty() && !edgesOut.empty() && edgesOut.size() == 1) {
            auto previousOperationsBoxed = false;
            for (auto& edge : edgesIn) {
                if (this->instructions[edge.from].escape)
                    previousOperationsBoxed = true;
            }

            if (!previousOperationsBoxed && getEdgesFrom(edgesOut[0].to).empty()) {
                instruction.second.escape = false;
                if (this->instructions[edgesOut[0].to].opcode != STORE_FAST) {
                    this->instructions[edgesOut[0].to].escape = false;
                    this->instructions[edgesOut[0].to].deoptimized = true;
                }
                continue;
            }
        }
    }
}

void InstructionGraph::fixLocals(py_oparg startIdx, py_oparg endIdx) {
    for (py_oparg localNumber = startIdx; localNumber <= endIdx; localNumber++) {
        // get all LOAD_FAST instructions
        bool loadsCanBeEscaped = true;
        bool storesCanBeEscaped = true;
        bool abstractTypesMatch = true;
        AbstractValueKind localAvk = AVK_Undefined;
        bool hasStores = false, hasLoads = false;
        for (auto& instruction : this->instructions) {
            if (instruction.second.opcode == LOAD_FAST && instruction.second.oparg == localNumber) {
                hasLoads = true;
                // if load doesn't have output edge, dont trust this graph
                auto loadEdges = getEdgesFrom(instruction.first);
                if (loadEdges.size() != 1 || !supportsEscaping(loadEdges[0].kind)) {
                    loadsCanBeEscaped = false;
                } else {
                    if (localAvk != AVK_Undefined && localAvk != loadEdges[0].kind) {
                        abstractTypesMatch = false;
                    }
                    localAvk = loadEdges[0].kind;
                }
            }
            if (instruction.second.opcode == STORE_FAST && instruction.second.oparg == localNumber) {
                hasStores = true;
                // if load doesn't have output edge, dont trust this graph
                auto storeEdges = getEdges(instruction.first);
                if (storeEdges.size() != 1 || !supportsEscaping(storeEdges[0].kind))
                    storesCanBeEscaped = false;
                else {
                    if (localAvk != AVK_Undefined && localAvk != storeEdges[0].kind) {
                        abstractTypesMatch = false;
                    }
                    localAvk = storeEdges[0].kind;
                }
            }
        }
        if (loadsCanBeEscaped && storesCanBeEscaped && hasStores && hasLoads && abstractTypesMatch) {
            unboxedFastLocals.insert({localNumber, localAvk});
            for (auto& instruction : this->instructions) {
                if (instruction.second.opcode == LOAD_FAST && instruction.second.oparg == localNumber) {
                    instruction.second.escape = true;
                }
                if (instruction.second.opcode == STORE_FAST && instruction.second.oparg == localNumber) {
                    instruction.second.escape = true;
                }
                if (instruction.second.opcode == DELETE_FAST && instruction.second.oparg == localNumber) {
                    instruction.second.escape = true;
                }
            }
        }
    }
}

PyObject* InstructionGraph::makeGraph(const char* name) {
    if (PyErr_Occurred()) {
#ifdef DEBUG_VERBOSE
        PyErr_Print();
#endif
        PyErr_Clear();
    }

    PyObject* g = PyUnicode_FromFormat("digraph %s { \n", name);
    PyUnicode_AppendAndDel(&g, PyUnicode_FromString("\tnode [shape=box];\n"));

    if (invalid)
        PyUnicode_AppendAndDel(&g, PyUnicode_FromString("\t// Graph was marked invalid, locals not optimized\n"));

    set<py_opindex> exceptionHandlers;
    for (const auto& node : instructions) {
        const char* blockColor;
        if (node.second.escape) {
            blockColor = "blue";
        } else if (node.second.deoptimized) {
            blockColor = "red";
        } else {
            blockColor = "black";
        }
        if (exceptionHandlers.find(node.second.index) != exceptionHandlers.end()) {
            PyUnicode_AppendAndDel(&g, PyUnicode_FromFormat("subgraph cluster_%u {\nlabel=\"except block\"\n", node.first));
        }

        PyObject* op;
        switch (node.second.opcode) {
            case LOAD_ATTR:
            case STORE_ATTR:
            case DELETE_ATTR:
            case LOAD_GLOBAL:
            case STORE_GLOBAL:
            case DELETE_GLOBAL:
            case STORE_NAME:
            case DELETE_NAME:
            case LOAD_NAME:
            case IMPORT_FROM:
            case IMPORT_NAME:
            case LOAD_METHOD:
                op = PyUnicode_FromFormat("\tOP%u [label=\"%u %s (%s)\" color=\"%s\"];\n", node.first, node.first, opcodeName(node.second.opcode),
                                          PyUnicode_AsUTF8(PyTuple_GetItem(this->code->co_names, node.second.oparg)), blockColor);
                break;
            case LOAD_CONST:
                op = PyUnicode_FromFormat("\tOP%u [label=\"%u %s (%s)\" color=\"%s\"];\n", node.first, node.first, opcodeName(node.second.opcode),
                                          PyUnicode_AsUTF8(PyUnicode_Substring(PyObject_Repr(PyTuple_GetItem(this->code->co_consts, node.second.oparg)), 0, 40)), blockColor);
                break;
            case LOAD_FAST:
            case STORE_FAST:
            case DELETE_FAST:
                op = PyUnicode_FromFormat("\tOP%u [label=\"%u %s (%s)\" color=\"%s\"];\n", node.first, node.first, opcodeName(node.second.opcode),
                                          PyUnicode_AsUTF8(PyObject_Repr(PyTuple_GetItem(this->code->co_varnames, node.second.oparg))), blockColor);
                break;
            case POP_EXCEPT:
            case POP_BLOCK:
                op = PyUnicode_FromFormat("\tOP%u [label=\"%u %s (%d)\" color=\"%s\"];\n}\n", node.first, node.first, opcodeName(node.second.opcode), node.second.oparg, blockColor);
                break;
            case SETUP_FINALLY:
                exceptionHandlers.insert(node.second.jumpsTo);
                op = PyUnicode_FromFormat("\tOP%u [label=\"%u %s (%d)\" color=\"%s\"];\n", node.first, node.first, opcodeName(node.second.opcode), node.second.oparg, blockColor);
                PyUnicode_AppendAndDel(&g, PyUnicode_FromFormat("subgraph cluster_%u {\nlabel = \"try block\";\n", node.first));
                break;
            case SETUP_WITH:
            case SETUP_ASYNC_WITH:
                op = PyUnicode_FromFormat("\tOP%u [label=\"%u %s (%d)\" color=\"%s\"];\n", node.first, node.first, opcodeName(node.second.opcode), node.second.oparg, blockColor);
                PyUnicode_AppendAndDel(&g, PyUnicode_FromFormat("subgraph cluster_%u {\nlabel = \"with block\";\n", node.first));
                break;
            default:
                op = PyUnicode_FromFormat("\tOP%u [label=\"%u %s (%d)\" color=\"%s\"];\n", node.first, node.first, opcodeName(node.second.opcode), node.second.oparg, blockColor);
                break;
        }
        PyUnicode_AppendAndDel(&g, op);

        switch (node.second.opcode) {
            case JUMP_FORWARD:
            case JUMP_ABSOLUTE:
                PyUnicode_AppendAndDel(&g, PyUnicode_FromFormat("\tOP%u -> OP%u [label=\"Jump\" color=yellow];\n", node.second.index, node.second.jumpsTo));
                break;
            case FOR_ITER:
            case JUMP_IF_NOT_EXC_MATCH:
            case JUMP_IF_FALSE_OR_POP:
            case JUMP_IF_TRUE_OR_POP:
            case POP_JUMP_IF_TRUE:
            case POP_JUMP_IF_FALSE:
                PyUnicode_AppendAndDel(&g, PyUnicode_FromFormat("\tOP%u -> OP%u [label=\"Jump (conditional)\" color=orange];\n", node.second.index, node.second.index + 2));
                PyUnicode_AppendAndDel(&g, PyUnicode_FromFormat("\tOP%u -> OP%u [label=\"Jump (conditional)\" color=orange];\n", node.second.index, node.second.jumpsTo));
                break;
        }
    }

    for (const auto& edge : edges) {
        if (edge.from == -1) {
            PyUnicode_AppendAndDel(&g, PyUnicode_FromFormat("\tFRAME -> OP%u [label=\"%s (%s)\"];\n", edge.to, edge.label, edge.value->describe()));
        } else {
            switch (edge.escaped) {
                case NoEscape:
                    PyUnicode_AppendAndDel(&g, PyUnicode_FromFormat("\tOP%u -> OP%u [label=\"%s (%s) -%u\" color=black];\n", edge.from, edge.to, edge.label, edge.value->describe(), edge.position));
                    break;
                case Unbox:
                    PyUnicode_AppendAndDel(&g, PyUnicode_FromFormat("\tOP%u -> OP%u [label=\"%s (%s) U%u\" color=red];\n", edge.from, edge.to, edge.label, edge.value->describe(), edge.position));
                    break;
                case Box:
                    PyUnicode_AppendAndDel(&g, PyUnicode_FromFormat("\tOP%u -> OP%u [label=\"%s (%s) B%u\" color=green];\n", edge.from, edge.to, edge.label, edge.value->describe(), edge.position));
                    break;
                case Unboxed:
                    PyUnicode_AppendAndDel(&g, PyUnicode_FromFormat("\tOP%u -> OP%u [label=\"%s (%s) UN%u\" color=purple];\n", edge.from, edge.to, edge.label, edge.value->describe(), edge.position));
                    break;
            }
        }
    }
    PyUnicode_AppendAndDel(&g, PyUnicode_FromString("}\n"));
    return g;
}

vector<Edge> InstructionGraph::getEdges(py_opindex idx) {
    EdgeMap filteredEdges;
    size_t max_position = 0;
    for (auto& edge : this->edges) {
        if (edge.to == idx) {
            filteredEdges[edge.position] = edge;
            if (edge.position > max_position)
                max_position = edge.position;
        }
    }
    vector<Edge> result;
    for (size_t i = 0; i <= max_position; i++) {
        if (filteredEdges.find(i) != filteredEdges.end())
            result.emplace_back(filteredEdges[i]);
    }
    return result;
}

vector<Edge> InstructionGraph::getEdgesFrom(py_opindex idx) {
    EdgeMap filteredEdges;
    size_t max_position = 0;
    for (auto& edge : this->edges) {
        if (edge.from == idx) {
            filteredEdges[edge.position] = edge;
            if (edge.position > max_position)
                max_position = edge.position;
        }
    }
    vector<Edge> result;
    for (size_t i = 0; i <= max_position; i++) {
        if (filteredEdges.find(i) != filteredEdges.end())
            result.emplace_back(filteredEdges[i]);
    }
    return result;
}

unordered_map<py_oparg, AbstractValueKind> InstructionGraph::getUnboxedFastLocals() {
    return unboxedFastLocals;
}

bool InstructionGraph::isValid() const {
    return !invalid;
}