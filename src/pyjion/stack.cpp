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

#include "stack.h"


void ValueStack::inc(size_t by, StackEntryKind kind) {
    for (size_t i = 0; i < by; i++) {
        push_back(kind);
    }
}

void ValueStack::dec(size_t by) {
    if (size() < by)
        throw StackUnderflowException();
    for (size_t i = 0; i < by; i++) {
        pop_back();
    }
}

StackEntryKind avkAsStackEntryKind(AbstractValueKind k){
    switch(k){
        case AVK_Integer:
            return STACK_KIND_VALUE_INT;
        case AVK_Float:
            return STACK_KIND_VALUE_FLOAT;
        case AVK_Bool:
            return STACK_KIND_VALUE_INT;
        default:
            return STACK_KIND_OBJECT;
    }
}

StackEntryKind lkAsStackEntryKind(LocalKind k){
    switch (k){
        case LK_Pointer:
            return STACK_KIND_OBJECT;
        case LK_Int:
            return STACK_KIND_VALUE_INT;
        case LK_Float:
            return STACK_KIND_VALUE_FLOAT;
        default:
            return STACK_KIND_OBJECT;
    }
}

LocalKind stackEntryKindAsLocalKind(StackEntryKind k){
    switch (k){
        case STACK_KIND_OBJECT:
            return LK_Pointer;
        case STACK_KIND_VALUE_INT:
            return LK_Int;
        case STACK_KIND_VALUE_FLOAT:
            return LK_Float;
        default:
            return LK_Pointer;
    }
}