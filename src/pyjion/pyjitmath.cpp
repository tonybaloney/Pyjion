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

#include "pyjitmath.h"
#include <opcode.h>
#include <longobject.h>

#define UNSUPPORTED_MATH_OP(opcode) PyErr_SetString(PyExc_NotImplementedError, "Operation not supported")

using namespace std;

bool isBinaryMathOp(int opcode){
    switch(opcode){
        case BINARY_TRUE_DIVIDE:
        case BINARY_FLOOR_DIVIDE:
        case BINARY_POWER:
        case BINARY_MODULO:
        case BINARY_MATRIX_MULTIPLY:
        case BINARY_MULTIPLY:
        case BINARY_SUBTRACT:
        case BINARY_ADD:
            return true;
    }
    return false;
}

double dmod(double left, double right){
    double mod = fmod(left, right);
    if (mod) {
        /* ensure the remainder has the same sign as the denominator */
        if ((right < 0) != (mod < 0)) {
            mod += right;
        }
    }
    else {
        mod = copysign(0.0, right);
    }
    return mod;
}
