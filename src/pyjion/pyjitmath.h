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

#include <Python.h>
#include "absvalue.h"

#ifndef SRC_PYJION_PYJITMATH_H
#define SRC_PYJION_PYJITMATH_H

bool isBinaryMathOp(int opcode);
bool isMathOp(int opcode);
bool isInplaceMathOp(int opcode);

bool canBeOptimized(int firstOp, int secondOp, AbstractValueKind type_a, AbstractValueKind type_b, AbstractValueKind type_c);

PyObject* PyJitMath_TripleBinaryOp(PyObject*, PyObject*, PyObject*, int, int);
PyObject* PyJitMath_TripleBinaryOpFloatFloatFloat(PyFloatObject * a, PyFloatObject* b, PyFloatObject* c, int firstOp, int secondOp);
PyObject* PyJitMath_TripleBinaryOpIntIntInt(PyObject* a, PyObject* b, PyObject* c, int firstOp, int secondOp);

PyObject* PyJitMath_TripleBinaryOpFloatIntInt(PyObject* a, PyObject* b, PyObject* c, int firstOp, int secondOp);
PyObject* PyJitMath_TripleBinaryOpIntFloatInt(PyObject* a, PyObject* b, PyObject* c, int firstOp, int secondOp);
PyObject* PyJitMath_TripleBinaryOpIntIntFloat(PyObject* a, PyObject* b, PyObject* c, int firstOp, int secondOp);

PyObject* PyJitMath_TripleBinaryOpIntFloatFloat(PyObject* a, PyObject* b, PyObject* c, int firstOp, int secondOp);
PyObject* PyJitMath_TripleBinaryOpFloatFloatInt(PyObject* a, PyObject* b, PyObject* c, int firstOp, int secondOp);
PyObject* PyJitMath_TripleBinaryOpFloatIntFloat(PyObject* a, PyObject* b, PyObject* c, int firstOp, int secondOp);

PyObject* PyJitMath_TripleBinaryOpObjObjObj(PyObject*, PyObject*, PyObject*, int, int);
PyObject* PyJitMath_TripleBinaryOpStrStrStr(PyObject*, PyObject*, PyObject*, int, int);

#endif //SRC_PYJION_PYJITMATH_H
