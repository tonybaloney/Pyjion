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

#include "pyjit.h"
#include "unboxing.h"

bool supportsUnboxing(py_opcode opcode) {
    switch (opcode) {
        case POP_JUMP_IF_FALSE:
        case POP_JUMP_IF_TRUE:
        case COMPARE_OP:
        case BINARY_POWER:
        case INPLACE_POWER:
        case INPLACE_MULTIPLY:
        case BINARY_MULTIPLY:
        case INPLACE_MODULO:
        case BINARY_MODULO:
        case INPLACE_ADD:
        case BINARY_ADD:
        case BINARY_FLOOR_DIVIDE:
        case INPLACE_FLOOR_DIVIDE:
        case INPLACE_TRUE_DIVIDE:
        case BINARY_TRUE_DIVIDE:
        case INPLACE_SUBTRACT:
        case BINARY_SUBTRACT:
        case LOAD_CONST:
        case STORE_FAST:
        case LOAD_FAST:
        case DELETE_FAST:
        case GET_ITER:
        case FOR_ITER:
        case BINARY_SUBSCR:
        case BINARY_LSHIFT:
        case BINARY_RSHIFT:
        case BINARY_AND:
        case BINARY_OR:
        case BINARY_XOR:
        case UNARY_NOT:
        case UNARY_POSITIVE:
        case UNARY_NEGATIVE:
        case UNARY_INVERT:
            return true;
        default:
            return false;
    }
}

bool supportsUnboxing(py_opcode opcode, vector<AbstractValueKind> edgesIn) {
    switch (opcode) {
        case BINARY_POWER:
        case INPLACE_POWER:
            for (auto& t : edgesIn) {
                if (t == AVK_Integer)// Dont try to unboxed a power function on a 64-bit int
                    return false;
            }
            return true;

        case INPLACE_MULTIPLY:
        case BINARY_MULTIPLY:
            if (OPT_ENABLED(IntegerUnboxingMultiply)) {
                return true;
            } else {
                for (auto& t : edgesIn) {
                    if (t == AVK_Integer)
                        return false;
                }
                return true;
            }
        case GET_ITER:
            if (edgesIn.size() == 1 && edgesIn[0] == AVK_Range)
                return true;
            return false;
        case FOR_ITER:
            for (auto& t : edgesIn) {
                if (t == AVK_UnboxedRangeIterator)
                    return true;
            }
            return false;
        case BINARY_SUBSCR:
            if (edgesIn.size() == 2 && edgesIn[0] == AVK_Integer && edgesIn[1] == AVK_Bytearray)
                return true;
            return false;
        default:
            return true;
    }
}

bool supportsEscaping(AbstractValueKind kind) {
    switch (kind) {
        case AVK_Float:
        case AVK_Integer:
        case AVK_Bool:
            return true;
        default:
            return false;
    }
}

bool unboxedArgument(AbstractValueKind kind) {
    switch (kind){
        case AVK_UnboxedRangeIterator:
        case AVK_Range:
        case AVK_Bytearray:
            return true;
        default:
            return false;
    }
}