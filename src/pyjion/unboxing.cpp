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

#include "unboxing.h"

bool supportsUnboxing(py_opcode opcode){
    switch (opcode){
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
            return true;
        default:
            return false;
    }
}

bool supportsEscaping(AbstractValueKind kind){
    switch (kind){
        case AVK_Float:
            return true;
        case AVK_Integer:
            return true;
        case AVK_Bool:
            return true;
        default:
            return false;
    }
}
