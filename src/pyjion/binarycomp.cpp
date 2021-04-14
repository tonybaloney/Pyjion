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


#include <windows.h>
#include <corjit.h>

#ifdef WINDOWS
#include <libloaderapi.h>
typedef void(__cdecl* JITSTARTUP)(ICorJitHost*);
#endif

#include <Python.h>
#include "pycomp.h"
#include "pyjit.h"
#include "pyjitmath.h"

using namespace std;
extern BaseModule g_module;

void PythonCompiler::emit_binary_object(int opcode) {
    switch (opcode) {
        case BINARY_ADD:
            m_il.emit_call(METHOD_ADD_TOKEN); break;
        case BINARY_TRUE_DIVIDE:
            m_il.emit_call(METHOD_DIVIDE_TOKEN); break;
        case BINARY_FLOOR_DIVIDE:
            m_il.emit_call(METHOD_FLOORDIVIDE_TOKEN); break;
        case BINARY_POWER:
            m_il.emit_call(METHOD_POWER_TOKEN); break;
        case BINARY_MODULO:
            m_il.emit_call(METHOD_MODULO_TOKEN); break;
        case BINARY_MATRIX_MULTIPLY:
            m_il.emit_call(METHOD_MATRIX_MULTIPLY_TOKEN); break;
        case BINARY_LSHIFT:
            m_il.emit_call(METHOD_BINARY_LSHIFT_TOKEN); break;
        case BINARY_RSHIFT:
            m_il.emit_call(METHOD_BINARY_RSHIFT_TOKEN); break;
        case BINARY_AND:
            m_il.emit_call(METHOD_BINARY_AND_TOKEN); break;
        case BINARY_XOR:
            m_il.emit_call(METHOD_BINARY_XOR_TOKEN); break;
        case BINARY_OR:
            m_il.emit_call(METHOD_BINARY_OR_TOKEN); break;
        case BINARY_MULTIPLY:
            m_il.emit_call(METHOD_MULTIPLY_TOKEN); break;
        case BINARY_SUBTRACT:
            m_il.emit_call(METHOD_SUBTRACT_TOKEN); break;
        case INPLACE_POWER:
            m_il.emit_call(METHOD_INPLACE_POWER_TOKEN); break;
        case INPLACE_MULTIPLY:
            m_il.emit_call(METHOD_INPLACE_MULTIPLY_TOKEN); break;
        case INPLACE_MATRIX_MULTIPLY:
            m_il.emit_call(METHOD_INPLACE_MATRIX_MULTIPLY_TOKEN); break;
        case INPLACE_TRUE_DIVIDE:
            m_il.emit_call(METHOD_INPLACE_TRUE_DIVIDE_TOKEN); break;
        case INPLACE_FLOOR_DIVIDE:
            m_il.emit_call(METHOD_INPLACE_FLOOR_DIVIDE_TOKEN); break;
        case INPLACE_MODULO:
            m_il.emit_call(METHOD_INPLACE_MODULO_TOKEN); break;
        case INPLACE_ADD:
            m_il.emit_call(METHOD_INPLACE_ADD_TOKEN); break;
        case INPLACE_SUBTRACT:
            m_il.emit_call(METHOD_INPLACE_SUBTRACT_TOKEN); break;
        case INPLACE_LSHIFT:
            m_il.emit_call(METHOD_INPLACE_LSHIFT_TOKEN); break;
        case INPLACE_RSHIFT:
            m_il.emit_call(METHOD_INPLACE_RSHIFT_TOKEN); break;
        case INPLACE_AND:
            m_il.emit_call(METHOD_INPLACE_AND_TOKEN); break;
        case INPLACE_XOR:
            m_il.emit_call(METHOD_INPLACE_XOR_TOKEN); break;
        case INPLACE_OR:
            m_il.emit_call(METHOD_INPLACE_OR_TOKEN); break;
    }
}

void PythonCompiler::emit_binary_object(int opcode, AbstractValueWithSources left, AbstractValueWithSources right) {
    int nb_slot = -1;
    int sq_slot = -1;
    int fallback_token;

    switch (opcode) {
        case BINARY_ADD:
            nb_slot = offsetof(PyNumberMethods, nb_add);
            sq_slot = offsetof(PySequenceMethods, sq_concat);
            fallback_token = METHOD_ADD_TOKEN;
            break;
        case BINARY_TRUE_DIVIDE:
            nb_slot = offsetof(PyNumberMethods, nb_true_divide); fallback_token = METHOD_DIVIDE_TOKEN;break;
        case BINARY_FLOOR_DIVIDE:
            nb_slot = offsetof(PyNumberMethods, nb_floor_divide); fallback_token = METHOD_FLOORDIVIDE_TOKEN;break;
        case BINARY_POWER:
            nb_slot = offsetof(PyNumberMethods, nb_power); fallback_token = METHOD_POWER_TOKEN;break;
        case BINARY_MODULO:
            nb_slot = offsetof(PyNumberMethods, nb_remainder); fallback_token = METHOD_MODULO_TOKEN;break;
        case BINARY_MATRIX_MULTIPLY:
            nb_slot = offsetof(PyNumberMethods, nb_matrix_multiply); fallback_token = METHOD_MATRIX_MULTIPLY_TOKEN;break;
        case BINARY_LSHIFT:
            nb_slot = offsetof(PyNumberMethods, nb_lshift); fallback_token = METHOD_BINARY_LSHIFT_TOKEN;break;
        case BINARY_RSHIFT:
            nb_slot = offsetof(PyNumberMethods, nb_rshift); fallback_token = METHOD_BINARY_RSHIFT_TOKEN;break;
        case BINARY_AND:
            nb_slot = offsetof(PyNumberMethods, nb_and); fallback_token = METHOD_BINARY_AND_TOKEN;break;
        case BINARY_XOR:
            nb_slot = offsetof(PyNumberMethods, nb_xor); fallback_token = METHOD_BINARY_XOR_TOKEN; break;
        case BINARY_OR:
            nb_slot = offsetof(PyNumberMethods, nb_or); fallback_token = METHOD_BINARY_OR_TOKEN;break;
        case BINARY_MULTIPLY:
            nb_slot = offsetof(PyNumberMethods, nb_multiply); sq_slot = offsetof(PySequenceMethods, sq_repeat); fallback_token = METHOD_MULTIPLY_TOKEN;break;
        case BINARY_SUBTRACT:
            nb_slot = offsetof(PyNumberMethods, nb_subtract); fallback_token = METHOD_SUBTRACT_TOKEN;break;
        case INPLACE_POWER:
            nb_slot = offsetof(PyNumberMethods, nb_inplace_power); fallback_token = METHOD_INPLACE_POWER_TOKEN;break;
        case INPLACE_MULTIPLY:
            nb_slot = offsetof(PyNumberMethods, nb_inplace_multiply); fallback_token = METHOD_INPLACE_MULTIPLY_TOKEN;break;
        case INPLACE_MATRIX_MULTIPLY:
            nb_slot = offsetof(PyNumberMethods, nb_inplace_matrix_multiply); fallback_token = METHOD_INPLACE_MATRIX_MULTIPLY_TOKEN;break;
        case INPLACE_TRUE_DIVIDE:
            nb_slot = offsetof(PyNumberMethods, nb_inplace_true_divide); fallback_token = METHOD_INPLACE_TRUE_DIVIDE_TOKEN; break;
        case INPLACE_FLOOR_DIVIDE:
            nb_slot = offsetof(PyNumberMethods, nb_inplace_floor_divide); fallback_token = METHOD_INPLACE_FLOOR_DIVIDE_TOKEN; break;
        case INPLACE_MODULO:
            nb_slot = offsetof(PyNumberMethods, nb_inplace_remainder); fallback_token = METHOD_INPLACE_MODULO_TOKEN; ;break;
        case INPLACE_ADD:
            nb_slot = offsetof(PyNumberMethods, nb_inplace_add); fallback_token = METHOD_INPLACE_ADD_TOKEN;break;
        case INPLACE_SUBTRACT:
            nb_slot = offsetof(PyNumberMethods, nb_inplace_subtract); fallback_token = METHOD_INPLACE_SUBTRACT_TOKEN; break;
        case INPLACE_LSHIFT:
            nb_slot = offsetof(PyNumberMethods, nb_inplace_lshift); fallback_token = METHOD_INPLACE_LSHIFT_TOKEN;break;
        case INPLACE_RSHIFT:
            nb_slot = offsetof(PyNumberMethods, nb_inplace_rshift); fallback_token = METHOD_INPLACE_RSHIFT_TOKEN;break;
        case INPLACE_AND:
            nb_slot = offsetof(PyNumberMethods, nb_inplace_and); fallback_token = METHOD_INPLACE_AND_TOKEN;break;
        case INPLACE_XOR:
            nb_slot = offsetof(PyNumberMethods, nb_inplace_xor); fallback_token = METHOD_INPLACE_XOR_TOKEN;break;
        case INPLACE_OR:
            nb_slot = offsetof(PyNumberMethods, nb_inplace_or); fallback_token = METHOD_INPLACE_OR_TOKEN;break;
    }

    bool emit_guard = (right.hasValue() && left.hasValue() && left.Value->known() && right.Value->known()) &&
                      (left.Value->needsGuard() || right.Value->needsGuard());
    Label execute_fallback = emit_define_label();
    Label skip_fallback = emit_define_label();
    Local leftLocal = emit_define_local(LK_Pointer);
    Local rightLocal = emit_define_local(LK_Pointer);
    emit_store_local(rightLocal);
    emit_store_local(leftLocal);

    if (emit_guard){
        emit_load_local(leftLocal);
        LD_FIELD(PyObject, ob_type);
        emit_ptr(left.Value->pythonType());
        emit_branch(BranchNotEqual, execute_fallback);
        emit_load_local(rightLocal);
        LD_FIELD(PyObject, ob_type);
        emit_ptr(right.Value->pythonType());
        emit_branch(BranchNotEqual, execute_fallback);
    }

    if (opcode == BINARY_POWER || opcode == INPLACE_POWER)
        emit_known_binary_op_power(left, right, leftLocal, rightLocal, nb_slot, sq_slot, fallback_token);
    else if (opcode == BINARY_MULTIPLY || opcode == INPLACE_MULTIPLY)
        emit_known_binary_op_multiply(left, right, leftLocal, rightLocal, nb_slot, sq_slot, fallback_token);
    else if (opcode == BINARY_ADD || opcode == INPLACE_ADD)
        emit_known_binary_op_add(left, right, leftLocal, rightLocal, nb_slot, sq_slot, fallback_token);
    else
        emit_known_binary_op(left, right, leftLocal, rightLocal, nb_slot, sq_slot, fallback_token);

    if (emit_guard){
        emit_branch(BranchAlways, skip_fallback);
        emit_mark_label(execute_fallback);

        emit_load_local(leftLocal);
        emit_load_local(rightLocal);
        m_il.emit_call(fallback_token);
        
        emit_mark_label(skip_fallback);
    }
    emit_free_local(leftLocal);
    emit_free_local(rightLocal);
}

void PythonCompiler::emit_known_binary_op(AbstractValueWithSources &left, AbstractValueWithSources &right,
                                          Local leftLocal, Local rightLocal,
                                          int nb_slot, int sq_slot, int fallback_token) {
    binaryfunc binaryfunc_left = nullptr;
    binaryfunc binaryfunc_right = nullptr;

    if (right.hasValue() && left.hasValue() && left.Value->known() && right.Value->known()){
        auto leftType = left.Value->pythonType();
        if (leftType->tp_as_number != nullptr){
            binaryfunc_left = (*(binaryfunc*)(& ((char*)leftType->tp_as_number)[nb_slot]));
        }

        auto rightType = right.Value->pythonType();
        if (rightType->tp_as_number != nullptr){
            binaryfunc_right = (*(binaryfunc*)(& ((char*)rightType->tp_as_number)[nb_slot]));
        }
    }

    /* Order operations are tried until either a valid result or error:
    w.op(v,w)[*], v.op(v,w), w.op(v,w)

    [*] only when Py_TYPE(v) != Py_TYPE(w) && Py_TYPE(w) is a subclass of
            Py_TYPE(v)
    */
    int left_func_token = -1, right_func_token = -1;

    if (binaryfunc_left) {
        left_func_token = g_module.AddMethod(CORINFO_TYPE_NATIVEINT,
                                             vector<Parameter>{
                                                     Parameter(CORINFO_TYPE_NATIVEINT),
                                                     Parameter(CORINFO_TYPE_NATIVEINT)},
                                             (void *) binaryfunc_left);
    }
    if (binaryfunc_right) {
        right_func_token = g_module.AddMethod(CORINFO_TYPE_NATIVEINT,
                                              vector<Parameter>{
                                                      Parameter(CORINFO_TYPE_NATIVEINT),
                                                      Parameter(CORINFO_TYPE_NATIVEINT)},
                                              (void *) binaryfunc_right);
    }

    if (binaryfunc_left != nullptr){
        // Add the function signature for this binaryfunc.
        Label leftImplemented = emit_define_label();
        Label rightImplemented = emit_define_label();
        emit_load_local(leftLocal);
        emit_load_local(rightLocal);
        m_il.emit_call(left_func_token);
        m_il.dup();
        emit_ptr(Py_NotImplemented);

        emit_branch(BranchNotEqual, leftImplemented);
        m_il.pop();
        emit_load_local(leftLocal);
        emit_load_local(rightLocal);
        if (binaryfunc_right){
            m_il.emit_call(right_func_token);
            m_il.dup();
            emit_ptr(Py_NotImplemented);
            emit_branch(BranchNotEqual, rightImplemented);
            m_il.pop();
            emit_pyerr_setstring(PyExc_TypeError, "Operation not supported on left-hand or right-hand operand.");
            emit_null();
            emit_mark_label(rightImplemented);
            emit_mark_label(leftImplemented);

            emit_load_local(leftLocal);
            decref();
            emit_load_local(rightLocal);
            decref();
        } else {
            Label hasDecref = emit_define_label();
            m_il.emit_call(fallback_token);
            emit_branch(BranchAlways, hasDecref);
            emit_mark_label(leftImplemented);
            emit_load_local(leftLocal);
            decref();
            emit_load_local(rightLocal);
            decref();
            emit_mark_label(hasDecref);
        }
    } else if (binaryfunc_right != nullptr) {
        // Add the function signature for this binaryfunc.
        Label rightNotImplemented = emit_define_label();
        emit_load_local(leftLocal);
        emit_load_local(rightLocal);
        m_il.emit_call(right_func_token);
        m_il.dup();
        emit_ptr(Py_NotImplemented);
        emit_branch(BranchNotEqual, rightNotImplemented);
        m_il.pop();
        emit_pyerr_setstring(PyExc_TypeError, "Operation not supported on right-hand operand.");
        emit_null();
        emit_mark_label(rightNotImplemented);
        emit_load_local(leftLocal);
        decref();
        emit_load_local(rightLocal);
        decref();
    } else {
        emit_load_local(leftLocal);
        emit_load_local(rightLocal);
        m_il.emit_call(fallback_token);
    }
}

void PythonCompiler::emit_known_binary_op_multiply(AbstractValueWithSources &left, AbstractValueWithSources &right,
                                                   Local leftLocal, Local rightLocal,
                                                   int nb_slot, int sq_slot, int fallback_token) {
    binaryfunc binaryfunc_left = nullptr;
    binaryfunc binaryfunc_right = nullptr;
    bool left_sequence = false, right_sequence = false;

    if (right.hasValue() && left.hasValue() && left.Value->known() && right.Value->known()){
        auto leftType = left.Value->pythonType();
        if (leftType->tp_as_number != nullptr){
            binaryfunc_left = (*(binaryfunc*)(& ((char*)leftType->tp_as_number)[nb_slot]));
        }
        if (binaryfunc_left == nullptr && leftType->tp_as_sequence != nullptr && sq_slot != -1){
            binaryfunc_left = (*(binaryfunc*)(& ((char*)leftType->tp_as_sequence)[sq_slot]));
            left_sequence = true;
        }
        auto rightType = right.Value->pythonType();
        if (rightType->tp_as_number != nullptr){
            binaryfunc_right = (*(binaryfunc*)(& ((char*)rightType->tp_as_number)[nb_slot]));
        }
        if (binaryfunc_right == nullptr && rightType->tp_as_sequence != nullptr && sq_slot != -1){
            binaryfunc_right = (*(binaryfunc*)(& ((char*)rightType->tp_as_sequence)[sq_slot]));
            right_sequence = true;
        }
    }
    /* Order operations are tried until either a valid result or error:
    w.op(v,w)[*], v.op(v,w), w.op(v,w)

    [*] only when Py_TYPE(v) != Py_TYPE(w) && Py_TYPE(w) is a subclass of
            Py_TYPE(v)
    */
    int left_func_token = -1, right_func_token = -1;

    if (binaryfunc_left) {
        left_func_token = g_module.AddMethod(CORINFO_TYPE_NATIVEINT,
                                             vector<Parameter>{
                                                     Parameter(CORINFO_TYPE_NATIVEINT),
                                                     Parameter(CORINFO_TYPE_NATIVEINT)},
                                             (void *) binaryfunc_left);
    }
    if (binaryfunc_right) {
        right_func_token = g_module.AddMethod(CORINFO_TYPE_NATIVEINT,
                                              vector<Parameter>{
                                                      Parameter(CORINFO_TYPE_NATIVEINT),
                                                      Parameter(CORINFO_TYPE_NATIVEINT)},
                                              (void *) binaryfunc_right);
    }

    if (binaryfunc_left != nullptr){
        // Add the function signature for this binaryfunc.
        Label leftImplemented = emit_define_label();
        Label rightImplemented = emit_define_label();
        emit_load_local(leftLocal);

        if (left_sequence){
            /* Support for [sequence] * value */
            if (right.hasSource() && right.Sources->hasConstValue() && right.Value->kind() == AVK_Integer){
                // Shortcut for const numeric values.
                m_il.ld_i(dynamic_cast<ConstSource *>(right.Sources)->getNumericValue());
            } else {
                emit_load_local(rightLocal);
                emit_null();
                m_il.emit_call(METHOD_NUMBER_AS_SSIZET);
            }
        } else {
            emit_load_local(rightLocal);
        }
        m_il.emit_call(left_func_token);
        m_il.dup();
        emit_ptr(Py_NotImplemented);
        emit_branch(BranchNotEqual, leftImplemented);
        if (binaryfunc_right){
            /* This is complicated.
             * Python supports
             * 5 * [sequence_type]
             * As a call to type->tp_as_sequence->tp_repeat,
             * tp_repeat takes two arguments, container and length. But we swap the values.
             * See PyNumber_Multiply for the reference implementation
             */
            m_il.pop();
            if (right_sequence){
                emit_load_local(rightLocal);
                if (left.hasSource() && left.Sources->hasConstValue() && left.Value->kind() == AVK_Integer){
                    m_il.ld_i(dynamic_cast<ConstSource *>(left.Sources)->getNumericValue());
                } else {
                    emit_load_local(leftLocal);
                    emit_null();
                    m_il.emit_call(METHOD_NUMBER_AS_SSIZET);
                }
            } else {
                emit_load_local(leftLocal);
                emit_load_local(rightLocal);
            }

            m_il.emit_call(right_func_token);
            m_il.dup();
            emit_ptr(Py_NotImplemented);
            emit_branch(BranchNotEqual, rightImplemented);
            m_il.pop();
            emit_pyerr_setstring(PyExc_TypeError, "Multiplication operator not supported on left-hand or right-hand operand.");
            emit_null();
            emit_mark_label(rightImplemented);
            emit_mark_label(leftImplemented);

            emit_load_local(leftLocal);
            decref();
            emit_load_local(rightLocal);
            decref();
        } else {
            m_il.pop();
            emit_load_local(leftLocal);
            emit_load_local(rightLocal);
            Label hasDecref = emit_define_label();
            m_il.emit_call(fallback_token);
            emit_branch(BranchAlways, hasDecref);
            emit_mark_label(leftImplemented);
            emit_load_local(leftLocal);
            decref();
            emit_load_local(rightLocal);
            decref();
            emit_mark_label(hasDecref);
        }
    } else if (binaryfunc_right != nullptr) {
        // Add the function signature for this binaryfunc.
        Label rightImplemented = emit_define_label();
        emit_load_local(leftLocal);
        emit_load_local(rightLocal);
        m_il.emit_call(right_func_token);
        m_il.dup();
        emit_ptr(Py_NotImplemented);
        emit_branch(BranchNotEqual, rightImplemented);
        m_il.pop();
        emit_pyerr_setstring(PyExc_TypeError, "Multiplication operator not supported on right-hand operand.");
        emit_null();
        emit_mark_label(rightImplemented);
        emit_load_local(leftLocal);
        decref();
        emit_load_local(rightLocal);
        decref();
    } else {
        emit_load_local(leftLocal);
        emit_load_local(rightLocal);
        m_il.emit_call(fallback_token);
    }
}

void PythonCompiler::emit_known_binary_op_add(AbstractValueWithSources &left, AbstractValueWithSources &right,
                                              Local leftLocal, Local rightLocal,
                                              int nb_slot, int sq_slot, int fallback_token) {
    binaryfunc binaryfunc_left = nullptr;
    binaryfunc binaryfunc_right = nullptr;

    if (right.hasValue() && left.hasValue() && left.Value->known() && right.Value->known()){
        auto leftType = left.Value->pythonType();
        if (leftType != nullptr && leftType->tp_as_number != nullptr){
            binaryfunc_left = (*(binaryfunc*)(& ((char*)leftType->tp_as_number)[nb_slot]));
        }
        if (binaryfunc_left == nullptr && leftType != nullptr && leftType->tp_as_sequence != nullptr && sq_slot != -1){
            binaryfunc_left = (*(binaryfunc*)(& ((char*)leftType->tp_as_sequence)[sq_slot]));
        }
        auto rightType = right.Value->pythonType();
        if (rightType != nullptr && rightType->tp_as_number != nullptr){
            binaryfunc_right = (*(binaryfunc*)(& ((char*)rightType->tp_as_number)[nb_slot]));
        }
        if (binaryfunc_right == nullptr && rightType != nullptr && rightType->tp_as_sequence != nullptr && sq_slot != -1){
            binaryfunc_right = (*(binaryfunc*)(& ((char*)rightType->tp_as_sequence)[sq_slot]));
        }
    }
    /* Order operations are tried until either a valid result or error:
    w.op(v,w)[*], v.op(v,w), w.op(v,w)

    [*] only when Py_TYPE(v) != Py_TYPE(w) && Py_TYPE(w) is a subclass of
            Py_TYPE(v)
    */
    int left_func_token = -1, right_func_token = -1;

    if (binaryfunc_left) {
        left_func_token = g_module.AddMethod(CORINFO_TYPE_NATIVEINT,
                                             vector<Parameter>{
                                                     Parameter(CORINFO_TYPE_NATIVEINT),
                                                     Parameter(CORINFO_TYPE_NATIVEINT)},
                                             (void *) binaryfunc_left);
    }
    if (binaryfunc_right) {
        right_func_token = g_module.AddMethod(CORINFO_TYPE_NATIVEINT,
                                              vector<Parameter>{
                                                      Parameter(CORINFO_TYPE_NATIVEINT),
                                                      Parameter(CORINFO_TYPE_NATIVEINT)},
                                              (void *) binaryfunc_right);
    }

    if (binaryfunc_left != nullptr){
        // Add the function signature for this binaryfunc.
        Label leftImplemented = emit_define_label();
        Label rightImplemented = emit_define_label();

        emit_load_local(leftLocal);
        emit_load_local(rightLocal);

        m_il.emit_call(left_func_token);
        m_il.dup();
        emit_ptr(Py_NotImplemented);
        emit_branch(BranchNotEqual, leftImplemented);
        if (binaryfunc_right){
            m_il.pop();
            emit_load_local(leftLocal);
            emit_load_local(rightLocal);
            m_il.emit_call(right_func_token);
            m_il.dup();
            emit_ptr(Py_NotImplemented);
            emit_branch(BranchNotEqual, rightImplemented);
            m_il.pop();
            emit_pyerr_setstring(PyExc_TypeError, "Add not supported on left-hand or right-hand operand.");
            emit_null();
            emit_mark_label(rightImplemented);
            emit_mark_label(leftImplemented);

            emit_load_local(leftLocal);
            decref();
            emit_load_local(rightLocal);
            decref();
        } else {
            m_il.pop();
            emit_load_local(leftLocal);
            emit_load_local(rightLocal);
            Label hasDecref = emit_define_label();
            m_il.emit_call(fallback_token);
            emit_branch(BranchAlways, hasDecref);
            emit_mark_label(leftImplemented);
            emit_load_local(leftLocal);
            decref();
            emit_load_local(rightLocal);
            decref();
            emit_mark_label(hasDecref);
        }
    } else if (binaryfunc_right != nullptr) {
        // Add the function signature for this binaryfunc.
        Label rightImplemented = emit_define_label();

        emit_load_local(leftLocal);
        emit_load_local(rightLocal);
        m_il.emit_call(right_func_token);
        m_il.dup();
        emit_ptr(Py_NotImplemented);
        emit_branch(BranchNotEqual, rightImplemented);
        m_il.pop();
        emit_pyerr_setstring(PyExc_TypeError, "Add not supported on right-hand operand.");
        emit_null();
        emit_mark_label(rightImplemented);
        emit_load_local(leftLocal);
        decref();
        emit_load_local(rightLocal);
        decref();
    } else {
        emit_load_local(leftLocal);
        emit_load_local(rightLocal);
        m_il.emit_call(fallback_token);
    }
}

void PythonCompiler::emit_known_binary_op_power(AbstractValueWithSources &left, AbstractValueWithSources &right,
                                                Local leftLocal, Local rightLocal,
                                                int nb_slot, int sq_slot, int fallback_token) {
    binaryfunc binaryfunc_left = nullptr;
    binaryfunc binaryfunc_right = nullptr;

    if (right.hasValue() && left.hasValue() && left.Value->known() && right.Value->known()){
        auto leftType = left.Value->pythonType();
        if (leftType->tp_as_number != nullptr){
            binaryfunc_left = (*(binaryfunc*)(& ((char*)leftType->tp_as_number)[nb_slot]));
        }
        auto rightType = right.Value->pythonType();
        if (rightType->tp_as_number != nullptr){
            binaryfunc_right = (*(binaryfunc*)(& ((char*)rightType->tp_as_number)[nb_slot]));
        }
    }

    /* Order operations are tried until either a valid result or error:
    w.op(v,w)[*], v.op(v,w), w.op(v,w)

    [*] only when Py_TYPE(v) != Py_TYPE(w) && Py_TYPE(w) is a subclass of
            Py_TYPE(v)
    */
    int left_func_token = -1, right_func_token = -1;

    if (binaryfunc_left) {
        left_func_token = g_module.AddMethod(CORINFO_TYPE_NATIVEINT,
                                             vector<Parameter>{
                                                     Parameter(CORINFO_TYPE_NATIVEINT),
                                                     Parameter(CORINFO_TYPE_NATIVEINT),
                                                     Parameter(CORINFO_TYPE_NATIVEINT)},
                                             (void *) binaryfunc_left);
    }
    if (binaryfunc_right) {
        right_func_token = g_module.AddMethod(CORINFO_TYPE_NATIVEINT,
                                              vector<Parameter>{
                                                      Parameter(CORINFO_TYPE_NATIVEINT),
                                                      Parameter(CORINFO_TYPE_NATIVEINT),
                                                      Parameter(CORINFO_TYPE_NATIVEINT)},
                                              (void *) binaryfunc_right);
    }

    if (binaryfunc_left != nullptr){
        // Add the function signature for this binaryfunc.
        Label leftImplemented = emit_define_label();
        Label rightImplemented = emit_define_label();
        emit_load_local(leftLocal);
        emit_load_local(rightLocal);
        emit_ptr(Py_None);
        m_il.emit_call(left_func_token);
        m_il.dup();
        emit_ptr(Py_NotImplemented);
        emit_branch(BranchNotEqual, leftImplemented);
        if (binaryfunc_right){
            m_il.pop();
            emit_load_local(leftLocal);
            emit_load_local(rightLocal);
            emit_ptr(Py_None);
            m_il.emit_call(right_func_token);
            m_il.dup();
            emit_ptr(Py_NotImplemented);
            emit_branch(BranchNotEqual, rightImplemented);
            m_il.pop();
            emit_pyerr_setstring(PyExc_TypeError, "Power not supported on left-hand or right-hand operand.");
            emit_null();
            emit_mark_label(rightImplemented);
            emit_mark_label(leftImplemented);

            emit_load_local(leftLocal);
            decref();
            emit_load_local(rightLocal);
            decref();
        } else {
            m_il.pop();
            emit_load_local(leftLocal);
            emit_load_local(rightLocal);
            Label hasDecref = emit_define_label();
            m_il.emit_call(fallback_token);
            emit_branch(BranchAlways, hasDecref);
            emit_mark_label(leftImplemented);
            emit_load_local(leftLocal);
            decref();
            emit_load_local(rightLocal);
            decref();
            emit_mark_label(hasDecref);
        }
    } else if (binaryfunc_right != nullptr) {
        // Add the function signature for this binaryfunc.
        Label rightImplemented = emit_define_label();
        emit_load_local(leftLocal);
        emit_load_local(rightLocal);
        emit_ptr(Py_None);
        m_il.emit_call(right_func_token);
        m_il.dup();
        emit_ptr(Py_NotImplemented);
        emit_branch(BranchNotEqual, rightImplemented);
        m_il.pop();
        emit_pyerr_setstring(PyExc_TypeError, "Power not supported on right-hand operand.");
        emit_null();
        emit_mark_label(rightImplemented);
        emit_load_local(leftLocal);
        decref();
        emit_load_local(rightLocal);
        decref();
    } else {
        emit_load_local(leftLocal);
        emit_load_local(rightLocal);
        m_il.emit_call(fallback_token);
    }
}

void PythonCompiler::emit_triple_binary_op(int firstOp, int secondOp) {
    m_il.ld_i4(firstOp);
    m_il.ld_i4(secondOp);
    m_il.emit_call(METHOD_TRIPLE_BINARY_OP);
}