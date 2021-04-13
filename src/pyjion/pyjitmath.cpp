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

bool isMathOp(int opcode) {
    switch(opcode){
        case BINARY_TRUE_DIVIDE:
        case BINARY_FLOOR_DIVIDE:
        case BINARY_POWER:
        case BINARY_MODULO:
        case BINARY_MATRIX_MULTIPLY:
        case BINARY_MULTIPLY:
        case BINARY_SUBTRACT:
        case BINARY_ADD:
        case INPLACE_POWER:
        case INPLACE_MULTIPLY:
        case INPLACE_MATRIX_MULTIPLY:
        case INPLACE_TRUE_DIVIDE:
        case INPLACE_FLOOR_DIVIDE:
        case INPLACE_MODULO:
        case INPLACE_ADD:
        case INPLACE_SUBTRACT:
            return true;
    }
    return false;
}

bool isInplaceMathOp(int opcode) {
    switch(opcode){
        case INPLACE_POWER:
        case INPLACE_MULTIPLY:
        case INPLACE_MATRIX_MULTIPLY:
        case INPLACE_TRUE_DIVIDE:
        case INPLACE_FLOOR_DIVIDE:
        case INPLACE_MODULO:
        case INPLACE_ADD:
        case INPLACE_SUBTRACT:
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

bool canBeOptimized(int firstOp, int secondOp, AbstractValueKind type_a, AbstractValueKind type_b, AbstractValueKind type_c){
    if (!isBinaryMathOp(firstOp) || !isMathOp(secondOp))
        return false;
    if (type_a == AVK_Float && type_b == AVK_Float && type_c == AVK_Float)
        return true;
    if (type_a == AVK_Integer && type_b == AVK_Integer && type_c == AVK_Integer && firstOp == BINARY_TRUE_DIVIDE)
        return true;
    if (type_a == AVK_Float && type_b == AVK_Integer && type_c == AVK_Integer)
        return true;
    if (type_a == AVK_Integer && type_b == AVK_Float && type_c == AVK_Integer)
        return true;
    if (type_a == AVK_Integer && type_b == AVK_Integer && type_c == AVK_Float)
        return true;
    if (type_a == AVK_Integer && type_b == AVK_Float && type_c == AVK_Float)
        return true;
    if (type_a == AVK_Float && type_b == AVK_Float && type_c == AVK_Integer)
        return true;
    if (type_a == AVK_Float && type_b == AVK_Integer && type_c == AVK_Float)
        return true;
    if (type_a == AVK_String && type_b == AVK_String && type_c == AVK_String)
        return true;
    return false;
}

PyObject* PyJitMath_TripleBinaryOp(PyObject* c, PyObject* a, PyObject* b, int firstOp, int secondOp) {
    PyObject* res = nullptr;
    if (PyFloat_CheckExact(a) && PyFloat_CheckExact(b) && PyFloat_CheckExact(c))
        res = PyJitMath_TripleBinaryOpFloatFloatFloat((PyFloatObject*)a, (PyFloatObject*)b, (PyFloatObject*)c, firstOp, secondOp);
    else if (PyLong_CheckExact(a) && PyLong_CheckExact(b) && PyLong_CheckExact(c) && firstOp == BINARY_TRUE_DIVIDE)
        res =  PyJitMath_TripleBinaryOpIntIntInt(a, b, c, firstOp, secondOp);
    else if (PyFloat_CheckExact(a) && PyLong_CheckExact(b) && PyLong_CheckExact(c))
        res =  PyJitMath_TripleBinaryOpFloatIntInt(a, b, c, firstOp, secondOp);
    else if (PyLong_CheckExact(a) && PyFloat_CheckExact(b) && PyLong_CheckExact(c))
        res =  PyJitMath_TripleBinaryOpIntFloatInt(a, b, c, firstOp, secondOp);
    else if (PyLong_CheckExact(a) && PyLong_CheckExact(b) && PyFloat_CheckExact(c))
        res =  PyJitMath_TripleBinaryOpIntIntFloat(a, b, c, firstOp, secondOp);
    else if (PyLong_CheckExact(a) && PyFloat_CheckExact(b) && PyFloat_CheckExact(c))
        res =  PyJitMath_TripleBinaryOpIntFloatFloat(a, b, c, firstOp, secondOp);
    else if (PyFloat_CheckExact(a) && PyFloat_CheckExact(b) && PyLong_CheckExact(c))
        res =  PyJitMath_TripleBinaryOpFloatFloatInt(a, b, c, firstOp, secondOp);
    else if (PyFloat_CheckExact(a) && PyLong_CheckExact(b) && PyFloat_CheckExact(c))
        res =  PyJitMath_TripleBinaryOpFloatIntFloat(a, b, c, firstOp, secondOp);
    else if (PyUnicode_Check(a) && PyUnicode_Check(b) && PyUnicode_Check(c))
        return PyJitMath_TripleBinaryOpStrStrStr(a, b, c, firstOp, secondOp); // special rules on references
    else {
        PyErr_SetString(PyExc_TypeError, "Cannot optimize these types, this error is a failure in the Pyjion type inference compiler.");
        return nullptr;
    }
    // Decref first op
    Py_DECREF(a);
    Py_DECREF(b);
    // Decref second op
    Py_DECREF(c);
    return res;
}

inline PyObject* PyJitMath_TripleBinaryOpIntIntInt(PyObject* a, PyObject* b, PyObject* c, int firstOp, int secondOp) {
    // This optimization only makes sense if the first opcode is a division. The abstract/PyNumber API works
    // out to be more efficient that converting to a long long, doing a native calculation and converting back again
    double val_c = PyLong_AsDouble(c);
    if (val_c == -1.0 && PyErr_Occurred()){
        return nullptr; // probably an overflow error
    }
    PyFloatObject * res_obj = (PyFloatObject*)PyNumber_TrueDivide(a, b);
    double res = res_obj->ob_fval;
    Py_DECREF(res_obj); // should destroy the value.
    switch (secondOp){
        case INPLACE_TRUE_DIVIDE:
        case BINARY_TRUE_DIVIDE:
            if (res == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            return PyFloat_FromDouble(val_c / res);
        case INPLACE_FLOOR_DIVIDE:
        case BINARY_FLOOR_DIVIDE:
            if (res == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            return PyFloat_FromDouble(floor(val_c / res));
        case INPLACE_POWER:
        case BINARY_POWER:
            return PyFloat_FromDouble(pow(val_c, res));
        case INPLACE_SUBTRACT:
        case BINARY_SUBTRACT:
            return PyFloat_FromDouble(val_c - res);
        case INPLACE_ADD:
        case BINARY_ADD:
            return PyFloat_FromDouble(val_c + res);
        case BINARY_MULTIPLY:
        case INPLACE_MULTIPLY:
            return PyFloat_FromDouble(val_c * res);
        case INPLACE_MATRIX_MULTIPLY:
        case BINARY_MATRIX_MULTIPLY:
            PyErr_SetString(PyExc_NotImplementedError, "Operation not supported");
            return nullptr;
        case BINARY_MODULO:
        case INPLACE_MODULO:
            if (res == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            return PyFloat_FromDouble(dmod(val_c, res));
    }
    UNSUPPORTED_MATH_OP(secondOp);
    return nullptr;
}

inline PyObject* PyJitMath_TripleBinaryOpFloatFloatFloat(PyFloatObject* a, PyFloatObject* b, PyFloatObject * c, int firstOp, int secondOp){
    double val_a = PyFloat_AS_DOUBLE(a);
    double val_b = PyFloat_AS_DOUBLE(b);
    double val_c = PyFloat_AS_DOUBLE(c);
    double res = -1;
    switch(firstOp) {
        case BINARY_TRUE_DIVIDE:
            res = val_a / val_b;
            break;
        case BINARY_FLOOR_DIVIDE:
            res = floor(val_a / val_b);
            break;
        case BINARY_POWER:
            res = pow(val_a, val_b);
            break;
        case BINARY_MODULO:
            if (val_b == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            res = dmod(val_a, val_b);
            break;
        case BINARY_MATRIX_MULTIPLY:
            UNSUPPORTED_MATH_OP(firstOp);
            return nullptr;
        case BINARY_MULTIPLY:
            res = val_a * val_b;
            break;
        case BINARY_SUBTRACT:
            res = val_a - val_b;
            break;
        case BINARY_ADD:
            res = val_a + val_b;
            break;
    }
    switch (secondOp){
        case INPLACE_POWER:
        case BINARY_POWER:
            return PyFloat_FromDouble(pow(val_c, res));
        case INPLACE_MULTIPLY:
        case BINARY_MULTIPLY:
            return PyFloat_FromDouble(val_c * res);
        case INPLACE_SUBTRACT:
        case BINARY_SUBTRACT:
            return PyFloat_FromDouble(val_c - res);
        case INPLACE_ADD:
        case BINARY_ADD:
            return PyFloat_FromDouble(val_c + res);
        case BINARY_TRUE_DIVIDE:
        case INPLACE_TRUE_DIVIDE:
            if (res == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            return PyFloat_FromDouble(val_c / res);
        case BINARY_FLOOR_DIVIDE:
        case INPLACE_FLOOR_DIVIDE:
            if (res == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            return PyFloat_FromDouble(floor(val_c / res));
        case BINARY_MODULO:
        case INPLACE_MODULO:
            if (res == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            return PyFloat_FromDouble(dmod(val_c, res));
        case BINARY_MATRIX_MULTIPLY:
        case INPLACE_MATRIX_MULTIPLY:
            UNSUPPORTED_MATH_OP(firstOp);
            return nullptr;
    }
    UNSUPPORTED_MATH_OP(secondOp);
    return nullptr;
}

inline PyObject* PyJitMath_TripleBinaryOpFloatIntInt(PyObject* a, PyObject* b, PyObject* c, int firstOp, int secondOp) {
    int overflow_b;
    int overflow_c;
    double val_a = PyFloat_AS_DOUBLE(a);
    long long val_b = PyLong_AsLongLongAndOverflow(b, &overflow_b);
    if (overflow_b){
        return PyJitMath_TripleBinaryOpObjObjObj(a, b, c, firstOp, secondOp);
    }
    long long val_c = PyLong_AsLongLongAndOverflow(c, &overflow_c);
    if (overflow_c){
        return PyJitMath_TripleBinaryOpObjObjObj(a, b, c, firstOp, secondOp);
    }
    double res = -1;
    switch(firstOp) {
        case BINARY_TRUE_DIVIDE:
            if (val_b == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            res = val_a / val_b;
            break;
        case BINARY_FLOOR_DIVIDE:
            if (val_b == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            res = floor(val_a / val_b);
            break;
        case BINARY_POWER:
            res = pow(val_a, val_b);
            break;
        case BINARY_MODULO:
            if (val_b == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            res = dmod(val_a, val_b);
            break;
        case BINARY_MATRIX_MULTIPLY:
            UNSUPPORTED_MATH_OP(firstOp);
            return nullptr;
        case BINARY_MULTIPLY:
            res = val_a * val_b;
            break;
        case BINARY_SUBTRACT:
            res = val_a - val_b;
            break;
        case BINARY_ADD:
            res = val_a + val_b;
            break;
    }
    switch (secondOp){
        case INPLACE_POWER:
        case BINARY_POWER:
            return PyFloat_FromDouble(pow(val_c, res));
        case INPLACE_MULTIPLY:
        case BINARY_MULTIPLY:
            return PyFloat_FromDouble(val_c * res);
        case INPLACE_SUBTRACT:
        case BINARY_SUBTRACT:
            return PyFloat_FromDouble(val_c - res);
        case INPLACE_ADD:
        case BINARY_ADD:
            return PyFloat_FromDouble(val_c + res);
        case BINARY_TRUE_DIVIDE:
        case INPLACE_TRUE_DIVIDE:
            if (res == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            return PyFloat_FromDouble(val_c / res);
        case BINARY_FLOOR_DIVIDE:
        case INPLACE_FLOOR_DIVIDE:
            if (res == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            return PyFloat_FromDouble(floor(val_c / res));
        case BINARY_MODULO:
        case INPLACE_MODULO:
            if (res == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            return PyFloat_FromDouble(dmod(val_c, res));
        case BINARY_MATRIX_MULTIPLY:
        case INPLACE_MATRIX_MULTIPLY:
            UNSUPPORTED_MATH_OP(firstOp);
            return nullptr;
    }
    UNSUPPORTED_MATH_OP(secondOp);
    return nullptr;
}

inline PyObject* PyJitMath_TripleBinaryOpIntFloatInt(PyObject* a, PyObject* b, PyObject* c, int firstOp, int secondOp) {
    int overflow_a = 0;
    long long val_a = PyLong_AsLongLongAndOverflow(a, &overflow_a);
    if (overflow_a){
        return PyJitMath_TripleBinaryOpObjObjObj(a, b, c, firstOp, secondOp);
    }
    double val_b = PyFloat_AS_DOUBLE(b);
    int overflow_c = 0;
    long long val_c = PyLong_AsLongLongAndOverflow(c, &overflow_c);
    if (overflow_c){
        return PyJitMath_TripleBinaryOpObjObjObj(a, b, c, firstOp, secondOp);
    }
    double res = -1;
    switch(firstOp) {
        case BINARY_TRUE_DIVIDE:
            if (val_b == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            res = val_a / val_b;
            break;
        case BINARY_FLOOR_DIVIDE:
            if (val_b== 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            res = floor(val_a / val_b);
            break;
        case BINARY_POWER:
            res = pow(val_a, val_b);
            break;
        case BINARY_MODULO:
            if (val_b == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            res = dmod(val_a, val_b);
            break;
        case BINARY_MATRIX_MULTIPLY:
            UNSUPPORTED_MATH_OP(firstOp);
            return nullptr;
        case BINARY_MULTIPLY:
            res = val_a * val_b;
            break;
        case BINARY_SUBTRACT:
            res = val_a - val_b;
            break;
        case BINARY_ADD:
            res = val_a + val_b;
            break;
    }
    switch (secondOp){
        case INPLACE_POWER:
        case BINARY_POWER:
            return PyFloat_FromDouble(pow(val_c, res));
        case INPLACE_MULTIPLY:
        case BINARY_MULTIPLY:
            return PyFloat_FromDouble(val_c * res);
        case INPLACE_SUBTRACT:
        case BINARY_SUBTRACT:
            return PyFloat_FromDouble(val_c - res);
        case INPLACE_ADD:
        case BINARY_ADD:
            return PyFloat_FromDouble(val_c + res);
        case BINARY_TRUE_DIVIDE:
        case INPLACE_TRUE_DIVIDE:
            if (res == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            return PyFloat_FromDouble(val_c / res);
        case BINARY_FLOOR_DIVIDE:
        case INPLACE_FLOOR_DIVIDE:
            if (res == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            return PyFloat_FromDouble(floor(val_c / res));
        case BINARY_MODULO:
        case INPLACE_MODULO:
            if (res == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            return PyFloat_FromDouble(dmod(val_c, res));
        case BINARY_MATRIX_MULTIPLY:
        case INPLACE_MATRIX_MULTIPLY:
            UNSUPPORTED_MATH_OP(firstOp);
            return nullptr;
    }
    UNSUPPORTED_MATH_OP(secondOp);
    return nullptr;
}


inline PyObject* PyJitMath_TripleBinaryOpIntIntFloat(PyObject* a, PyObject* b, PyObject* c, int firstOp, int secondOp) {
    int overflow_a = 0;
    long long val_a = PyLong_AsLongLongAndOverflow(a, &overflow_a);
    if (overflow_a){
        return PyJitMath_TripleBinaryOpObjObjObj(a, b, c, firstOp, secondOp);
    }
    int overflow_b = 0;
    long long val_b = PyLong_AsLongLongAndOverflow(b, &overflow_b);
    if (overflow_b){
        return PyJitMath_TripleBinaryOpObjObjObj(a, b, c, firstOp, secondOp);
    }
    double val_c = PyFloat_AS_DOUBLE(c);
    double res = -1;
    switch(firstOp) {
        case BINARY_TRUE_DIVIDE:
            if (val_b == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            res = val_a / val_b;
            break;
        case BINARY_FLOOR_DIVIDE:
            if (val_b== 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            res = floor(val_a / val_b);
            break;
        case BINARY_POWER:
            res = pow(val_a, val_b);
            break;
        case BINARY_MODULO:
            if (val_b == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            res = dmod(val_a, val_b);
            break;
        case BINARY_MATRIX_MULTIPLY:
            UNSUPPORTED_MATH_OP(firstOp);
            return nullptr;
        case BINARY_MULTIPLY:
            res = val_a * val_b;
            break;
        case BINARY_SUBTRACT:
            res = val_a - val_b;
            break;
        case BINARY_ADD:
            res = val_a + val_b;
            break;
    }
    switch (secondOp){
        case INPLACE_POWER:
        case BINARY_POWER:
            return PyFloat_FromDouble(pow(val_c, res));
        case INPLACE_MULTIPLY:
        case BINARY_MULTIPLY:
            return PyFloat_FromDouble(val_c * res);
        case INPLACE_SUBTRACT:
        case BINARY_SUBTRACT:
            return PyFloat_FromDouble(val_c - res);
        case INPLACE_ADD:
        case BINARY_ADD:
            return PyFloat_FromDouble(val_c + res);
        case BINARY_TRUE_DIVIDE:
        case INPLACE_TRUE_DIVIDE:
            if (res == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            return PyFloat_FromDouble(val_c / res);
        case BINARY_FLOOR_DIVIDE:
        case INPLACE_FLOOR_DIVIDE:
            if (res == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            return PyFloat_FromDouble(floor(val_c / res));
        case BINARY_MODULO:
        case INPLACE_MODULO:
            if (res == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            return PyFloat_FromDouble(dmod(val_c, res));
        case BINARY_MATRIX_MULTIPLY:
        case INPLACE_MATRIX_MULTIPLY:
            UNSUPPORTED_MATH_OP(firstOp);
            return nullptr;
    }
    UNSUPPORTED_MATH_OP(secondOp);
    return nullptr;
}


inline PyObject* PyJitMath_TripleBinaryOpFloatFloatInt(PyObject* a, PyObject* b, PyObject* c, int firstOp, int secondOp) {
    double val_a = PyFloat_AS_DOUBLE(a);
    double val_b = PyFloat_AS_DOUBLE(b);
    int overflow_c = 0;
    long long val_c = PyLong_AsLongLongAndOverflow(c, &overflow_c);
    if (overflow_c){
        return PyJitMath_TripleBinaryOpObjObjObj(a, b, c, firstOp, secondOp);
    }

    double res = -1;
    switch(firstOp) {
        case BINARY_TRUE_DIVIDE:
            if (val_b == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            res = val_a / val_b;
            break;
        case BINARY_FLOOR_DIVIDE:
            if (val_b== 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            res = floor(val_a / val_b);
            break;
        case BINARY_POWER:
            res = pow(val_a, val_b);
            break;
        case BINARY_MODULO:
            if (val_b == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            res = dmod(val_a, val_b);
            break;
        case BINARY_MATRIX_MULTIPLY:
            UNSUPPORTED_MATH_OP(firstOp);
            return nullptr;
        case BINARY_MULTIPLY:
            res = val_a * val_b;
            break;
        case BINARY_SUBTRACT:
            res = val_a - val_b;
            break;
        case BINARY_ADD:
            res = val_a + val_b;
            break;
    }
    switch (secondOp){
        case INPLACE_POWER:
        case BINARY_POWER:
            return PyFloat_FromDouble(pow(val_c, res));
        case INPLACE_MULTIPLY:
        case BINARY_MULTIPLY:
            return PyFloat_FromDouble(val_c * res);
        case INPLACE_SUBTRACT:
        case BINARY_SUBTRACT:
            return PyFloat_FromDouble(val_c - res);
        case INPLACE_ADD:
        case BINARY_ADD:
            return PyFloat_FromDouble(val_c + res);
        case BINARY_TRUE_DIVIDE:
        case INPLACE_TRUE_DIVIDE:
            if (res == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            return PyFloat_FromDouble(val_c / res);
        case BINARY_FLOOR_DIVIDE:
        case INPLACE_FLOOR_DIVIDE:
            if (res == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            return PyFloat_FromDouble(floor(val_c / res));
        case BINARY_MODULO:
        case INPLACE_MODULO:
            if (res == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            return PyFloat_FromDouble(dmod(val_c, res));
        case BINARY_MATRIX_MULTIPLY:
        case INPLACE_MATRIX_MULTIPLY:
            UNSUPPORTED_MATH_OP(firstOp);
            return nullptr;
    }
    UNSUPPORTED_MATH_OP(secondOp);
    return nullptr;
}

inline PyObject* PyJitMath_TripleBinaryOpFloatIntFloat(PyObject* a, PyObject* b, PyObject* c, int firstOp, int secondOp) {
    double val_a = PyFloat_AS_DOUBLE(a);
    int overflow_b = 0;
    long long val_b = PyLong_AsLongLongAndOverflow(b, &overflow_b);
    if (overflow_b){
        return PyJitMath_TripleBinaryOpObjObjObj(a, b, c, firstOp, secondOp);
    }
    double val_c = PyFloat_AS_DOUBLE(c);

    double res = -1;
    switch(firstOp) {
        case BINARY_TRUE_DIVIDE:
            if (val_b == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            res = val_a / val_b;
            break;
        case BINARY_FLOOR_DIVIDE:
            if (val_b== 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            res = floor(val_a / val_b);
            break;
        case BINARY_POWER:
            res = pow(val_a, val_b);
            break;
        case BINARY_MODULO:
            if (val_b == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            res = dmod(val_a, val_b);
            break;
        case BINARY_MATRIX_MULTIPLY:
            UNSUPPORTED_MATH_OP(firstOp);
            return nullptr;
        case BINARY_MULTIPLY:
            res = val_a * val_b;
            break;
        case BINARY_SUBTRACT:
            res = val_a - val_b;
            break;
        case BINARY_ADD:
            res = val_a + val_b;
            break;
    }
    switch (secondOp){
        case INPLACE_POWER:
        case BINARY_POWER:
            return PyFloat_FromDouble(pow(val_c, res));
        case INPLACE_MULTIPLY:
        case BINARY_MULTIPLY:
            return PyFloat_FromDouble(val_c * res);
        case INPLACE_SUBTRACT:
        case BINARY_SUBTRACT:
            return PyFloat_FromDouble(val_c - res);
        case INPLACE_ADD:
        case BINARY_ADD:
            return PyFloat_FromDouble(val_c + res);
        case BINARY_TRUE_DIVIDE:
        case INPLACE_TRUE_DIVIDE:
            if (res == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            return PyFloat_FromDouble(val_c / res);
        case BINARY_FLOOR_DIVIDE:
        case INPLACE_FLOOR_DIVIDE:
            if (res == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            return PyFloat_FromDouble(floor(val_c / res));
        case BINARY_MODULO:
        case INPLACE_MODULO:
            if (res == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            return PyFloat_FromDouble(dmod(val_c, res));
        case BINARY_MATRIX_MULTIPLY:
        case INPLACE_MATRIX_MULTIPLY:
            UNSUPPORTED_MATH_OP(firstOp);
            return nullptr;
    }
    UNSUPPORTED_MATH_OP(secondOp);
    return nullptr;
}

inline PyObject* PyJitMath_TripleBinaryOpIntFloatFloat(PyObject* a, PyObject* b, PyObject* c, int firstOp, int secondOp) {
    int overflow = 0;
    long long val_a = PyLong_AsLongLongAndOverflow(a, &overflow);
    if (overflow){
        return PyJitMath_TripleBinaryOpObjObjObj(a, b, c, firstOp, secondOp);
    }
    double val_b = PyFloat_AS_DOUBLE(b);
    double val_c = PyFloat_AS_DOUBLE(c);
    double res = -1;
    switch(firstOp) {
        case BINARY_TRUE_DIVIDE:
            if (val_b == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            res = val_a / val_b;
            break;
        case BINARY_FLOOR_DIVIDE:
            if (val_b == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            res = floor(val_a / val_b);
            break;
        case BINARY_POWER:
            res = pow(val_a, val_b);
            break;
        case BINARY_MODULO:
            if (val_b == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            res = dmod(val_a, val_b);
            break;
        case BINARY_MATRIX_MULTIPLY:
            UNSUPPORTED_MATH_OP(firstOp);
            return nullptr;
        case BINARY_MULTIPLY:
            res = val_a * val_b;
            break;
        case BINARY_SUBTRACT:
            res = val_a - val_b;
            break;
        case BINARY_ADD:
            res = val_a + val_b;
            break;
    }
    switch (secondOp){
        case INPLACE_POWER:
        case BINARY_POWER:
            return PyFloat_FromDouble(pow(val_c, res));
        case INPLACE_MULTIPLY:
        case BINARY_MULTIPLY:
            return PyFloat_FromDouble(val_c * res);
        case INPLACE_SUBTRACT:
        case BINARY_SUBTRACT:
            return PyFloat_FromDouble(val_c - res);
        case INPLACE_ADD:
        case BINARY_ADD:
            return PyFloat_FromDouble(val_c + res);
        case BINARY_TRUE_DIVIDE:
        case INPLACE_TRUE_DIVIDE:
            if (res == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            return PyFloat_FromDouble(val_c / res);
        case BINARY_FLOOR_DIVIDE:
        case INPLACE_FLOOR_DIVIDE:
            if (res == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            return PyFloat_FromDouble(floor(val_c / res));
        case BINARY_MODULO:
        case INPLACE_MODULO:
            if (res == 0){
                PyErr_SetString(PyExc_ZeroDivisionError, "Cannot divide by zero");
                return nullptr;
            }
            return PyFloat_FromDouble(dmod(val_c, res));
        case BINARY_MATRIX_MULTIPLY:
        case INPLACE_MATRIX_MULTIPLY:
            UNSUPPORTED_MATH_OP(firstOp);
            return nullptr;
    }
    UNSUPPORTED_MATH_OP(secondOp);
    return nullptr;
}

inline PyObject* PyJitMath_TripleBinaryOpObjObjObj(PyObject* a, PyObject* b, PyObject* c, int firstOp, int secondOp) {
    PyObject* res = nullptr;
    PyObject* res2 = nullptr;
    switch(firstOp) {
        case BINARY_TRUE_DIVIDE:
            res = PyNumber_TrueDivide(a, b);
            break;
        case BINARY_FLOOR_DIVIDE:
            res = PyNumber_FloorDivide(a, b);
            break;
        case BINARY_MODULO:
            res = (PyUnicode_CheckExact(a) && (
                    !PyUnicode_Check(b) || PyUnicode_CheckExact(b))) ?
                       PyUnicode_Format(a, b) :
                       PyNumber_Remainder(a, b);
            break;
        case BINARY_MATRIX_MULTIPLY:
            res = PyNumber_MatrixMultiply(a, b);
            break;
        case BINARY_MULTIPLY:
            res = PyNumber_Multiply(a, b);
            break;
        case BINARY_SUBTRACT:
            res = PyNumber_Subtract(a, b);
            break;
        case BINARY_ADD:
            if (PyUnicode_CheckExact(a) && PyUnicode_CheckExact(b)) {
                PyUnicode_Append(&a, b);
                res = a;
                Py_INCREF(res);
            }
            else {
                res = PyNumber_Add(a, b);
            }

            break;
        case BINARY_POWER:
            res = PyNumber_Power(a, b, Py_None);
            break;
    }
    if (res == nullptr) {
        if (!PyErr_Occurred())
            PyErr_SetString(PyExc_ValueError, "Failure in math operation");
        return nullptr;
    }
    switch (secondOp){
        case BINARY_TRUE_DIVIDE:
            res2 = PyNumber_TrueDivide(c, res);
            break;
        case BINARY_FLOOR_DIVIDE:
            res2 = PyNumber_FloorDivide(c, res);
            break;
        case BINARY_POWER:
            res2 = PyNumber_Power(c, res, Py_None);
            break;
        case BINARY_MODULO:
            res2 =  (PyUnicode_CheckExact(c) && (
                    !PyUnicode_Check(res) || PyUnicode_CheckExact(res))) ?
                              PyUnicode_Format(c, res) :
                              PyNumber_Remainder(c, res);
            break;
        case BINARY_MATRIX_MULTIPLY:
            res2 = PyNumber_MatrixMultiply(c, res);
            break;
        case BINARY_MULTIPLY:
            res2 =  PyNumber_Multiply(c, res);
            break;
        case BINARY_SUBTRACT:
            res2 =  PyNumber_Subtract(c, res);
            break;
        case BINARY_ADD:
            if (PyUnicode_CheckExact(c) && PyUnicode_CheckExact(res)) {
                PyUnicode_Append(&c, res);
                res2 = c;
                Py_INCREF(res2);
            }
            else {
                res2 = PyNumber_Add(c, res);
            }

            break;
        case INPLACE_POWER:
            res2 =  PyNumber_InPlacePower(c, res, Py_None);
            break;
        case INPLACE_MULTIPLY:
            res2 =  PyNumber_InPlaceMultiply(c, res);
            break;
        case INPLACE_MATRIX_MULTIPLY:
            res2 =  PyNumber_InPlaceMatrixMultiply(c, res);
            break;
        case INPLACE_TRUE_DIVIDE:
            res2 =  PyNumber_InPlaceTrueDivide(c, res);
            break;
        case INPLACE_FLOOR_DIVIDE:
            res2 =  PyNumber_InPlaceFloorDivide(c, res);
            break;
        case INPLACE_MODULO:
            res2 =  PyNumber_InPlaceRemainder(c, res);
            break;
        case INPLACE_ADD:
            if (PyUnicode_CheckExact(c) && PyUnicode_CheckExact(res)) {
                PyUnicode_Append(&c, res);
                res2 = c;
                Py_INCREF(c);
            }
            else {
                res2 = PyNumber_InPlaceAdd(c, res);
            }
            break;
        case INPLACE_SUBTRACT:
            res2 =  PyNumber_InPlaceSubtract(c, res);
            break;
    }
    Py_DECREF(res);
    return res2;
}

inline PyObject* PyJitMath_TripleBinaryOpStrStrStr(PyObject* a, PyObject* b, PyObject* c, int firstOp, int secondOp) {
    PyObject* res;
    switch(firstOp) {
        case BINARY_TRUE_DIVIDE:
        case BINARY_FLOOR_DIVIDE:
        case BINARY_POWER:
        case BINARY_MATRIX_MULTIPLY:
        case BINARY_MULTIPLY:
        case BINARY_SUBTRACT:
            UNSUPPORTED_MATH_OP(firstOp);
            return nullptr;
        case BINARY_MODULO:
            res = PyUnicode_Format(a, b);
            Py_DECREF(a);
            Py_DECREF(b);
            break;
        case BINARY_ADD:
            res = a;
            PyUnicode_Append(&res, b);
            if (res == nullptr)
                return nullptr;
            Py_DECREF(b);
            break;
    }

    switch (secondOp){
        case INPLACE_POWER:
        case INPLACE_MULTIPLY:
        case INPLACE_MATRIX_MULTIPLY:
        case INPLACE_TRUE_DIVIDE:
        case INPLACE_FLOOR_DIVIDE:
        case INPLACE_SUBTRACT:
        case BINARY_TRUE_DIVIDE:
        case BINARY_FLOOR_DIVIDE:
        case BINARY_POWER:
        case BINARY_MATRIX_MULTIPLY:
        case BINARY_MULTIPLY:
        case BINARY_SUBTRACT:
            UNSUPPORTED_MATH_OP(firstOp);
            return nullptr;
        case BINARY_MODULO:
        case INPLACE_MODULO:
            PyUnicode_Format(c, res);
            Py_DECREF(c);
            Py_DECREF(res);
            return c;
        case INPLACE_ADD:
        case BINARY_ADD:
            PyUnicode_Append(&c, res);
            if (c == nullptr)
                return nullptr;
            Py_DECREF(res);
            Py_INCREF(c);
            return c;
    }
    UNSUPPORTED_MATH_OP(firstOp);
    return nullptr;
}
