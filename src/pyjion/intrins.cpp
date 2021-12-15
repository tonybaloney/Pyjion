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
*
* Portions lifted from CPython under the PSF license.
*/
#include "intrins.h"
#include "pyjit.h"

#ifdef _MSC_VER

#include <safeint.h>
using namespace msl::utilities;

#endif

PyObject* g_emptyTuple;

#include <dictobject.h>
#include <vector>

#define NAME_ERROR_MSG \
    "name '%.200s' is not defined"

#define UNBOUNDLOCAL_ERROR_MSG \
    "local variable '%.200s' referenced before assignment"
#define UNBOUNDFREE_ERROR_MSG                             \
    "free variable '%.200s' referenced before assignment" \
    " in enclosing scope"

#define ASSERT_ARG(arg)                                        \
    if ((arg) == nullptr) {                                    \
        PyErr_SetString(PyExc_ValueError,                      \
                        "Argument null in internal function"); \
        return nullptr;                                        \
    }

#define ASSERT_ARG_INT(arg)                                    \
    if ((arg) == nullptr) {                                    \
        PyErr_SetString(PyExc_ValueError,                      \
                        "Argument null in internal function"); \
        return -1;                                             \
    }

template<typename T>
void decref(T v) {
    Py_DECREF(v);
}

template<typename T, typename... Args>
void decref(T v, Args... args) {
    Py_DECREF(v);
    decref(args...);
}

static void
format_exc_check_arg(PyObject* exc, const char* format_str, PyObject* obj) {
    const char* obj_str;

    if (!obj)
        return;

    obj_str = _PyUnicode_AsString(obj);
    if (!obj_str)
        return;

    PyErr_Format(exc, format_str, obj_str);
}

static void
format_exc_unbound(PyCodeObject* co, int oparg) {
    PyObject* name;
    /* Don't stomp existing exception */
    if (PyErr_Occurred())
        return;
    if (oparg < PyTuple_GET_SIZE(co->co_cellvars)) {
        name = PyTuple_GET_ITEM(co->co_cellvars,
                                oparg);
        format_exc_check_arg(
                PyExc_UnboundLocalError,
                UNBOUNDLOCAL_ERROR_MSG,
                name);
    } else {
        name = PyTuple_GET_ITEM(co->co_freevars, oparg -
                                                         PyTuple_GET_SIZE(co->co_cellvars));
        format_exc_check_arg(PyExc_NameError,
                             UNBOUNDFREE_ERROR_MSG, name);
    }
}

PyObject* PyJit_Add(PyObject* left, PyObject* right) {
    PyObject* sum;
    if (PyUnicode_CheckExact(left) && PyUnicode_CheckExact(right)) {
        PyUnicode_Append(&left, right);
        sum = left;
    } else {
        sum = PyNumber_Add(left, right);
        Py_DECREF(left);
    }
    Py_DECREF(right);
    return sum;
}

PyObject* PyJit_Subscr(PyObject* left, PyObject* right) {
    auto res = PyObject_GetItem(left, right);
    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_SubscrIndex(PyObject* o, PyObject* key, Py_ssize_t index) {
    PySequenceMethods* ms;
    PyObject* res;

    if (o == nullptr || key == nullptr) {
        PyErr_SetString(PyExc_ValueError,
                        "Internal call, PyJit_SubscrIndex with key or container null");
        return nullptr;
    }

    ms = Py_TYPE(o)->tp_as_sequence;
    if (ms && ms->sq_item) {
        res = PySequence_GetItem(o, index);
    } else {
        res = PyObject_GetItem(o, key);
    }
    Py_DECREF(o);
    Py_DECREF(key);

    return res;
}

PyObject* PyJit_SubscrIndexHash(PyObject* o, PyObject* key, Py_ssize_t index, Py_hash_t hash) {
    if (PyDict_CheckExact(o))
        return PyJit_SubscrDictHash(o, key, hash);
    else
        return PyJit_SubscrIndex(o, key, index);
}

PyObject* PyJit_SubscrDict(PyObject* o, PyObject* key) {
    if (!PyDict_CheckExact(o))
        return PyJit_Subscr(o, key);

    PyObject* value = PyDict_GetItem(o, key);
    Py_XINCREF(value);
    if (value == nullptr && !PyErr_Occurred())
        _PyErr_SetKeyError(key);
    Py_DECREF(o);
    Py_DECREF(key);
    return value;
}

PyObject* PyJit_SubscrDictHash(PyObject* o, PyObject* key, Py_hash_t hash) {
    if (!PyDict_CheckExact(o))
        return PyJit_Subscr(o, key);
    PyObject* value = _PyDict_GetItem_KnownHash(o, key, hash);
    Py_XINCREF(value);
    if (value == nullptr && !PyErr_Occurred())
        _PyErr_SetKeyError(key);
    Py_DECREF(o);
    Py_DECREF(key);
    return value;
}

PyObject* PyJit_SubscrList(PyObject* o, PyObject* key) {
    if (!PyList_CheckExact(o))
        return PyJit_Subscr(o, key);
    PyObject* res;

    if (PyIndex_Check(key)) {
        Py_ssize_t key_value;
        key_value = PyNumber_AsSsize_t(key, PyExc_IndexError);
        if (key_value == -1 && PyErr_Occurred()) {
            res = nullptr;
        } else if (key_value < 0) {
            // Supports negative indexes without converting back to PyLong..
            res = PySequence_GetItem(o, key_value);
        } else {
            res = PyList_GetItem(o, key_value);
            Py_XINCREF(res);
        }
    } else {
        return PyJit_Subscr(o, key);
    }
    Py_DECREF(o);
    Py_DECREF(key);
    return res;
}

PyObject* PyJit_SubscrListIndex(PyObject* o, PyObject* key, Py_ssize_t index) {
    if (!PyList_CheckExact(o))
        return PyJit_Subscr(o, key);
    PyObject* res = PyList_GetItem(o, index);
    Py_XINCREF(res);
    Py_DECREF(o);
    Py_DECREF(key);
    return res;
}

PyObject* PyJit_SubscrListSliceStepped(PyObject* o, Py_ssize_t start, Py_ssize_t stop, Py_ssize_t step) {
    Py_ssize_t slicelength, i;
    size_t cur;
    PyObject* result = nullptr;
    PyListObject* self = (PyListObject*) o;
    PyObject* it;
    PyObject **src, **dest;
    if (!PyList_CheckExact(o)) {
        PyErr_SetString(PyExc_TypeError, "Invalid type for const slice");
        goto error;
    }
    if (start == PY_SSIZE_T_MIN)
        start = step < 0 ? PY_SSIZE_T_MAX : 0;
    if (stop == PY_SSIZE_T_MAX)
        stop = step < 0 ? PY_SSIZE_T_MIN : PY_SSIZE_T_MAX;
    slicelength = PySlice_AdjustIndices(Py_SIZE(o), &start, &stop,
                                        step);

    if (slicelength <= 0 && step > 0) {
        result = PyList_New(0);
    } else if (step == 1) {
        result = PyList_GetSlice(o, start, stop);
    } else {
        result = PyList_New(0);
        ((PyListObject*) result)->ob_item = PyMem_New(PyObject*, slicelength);
        if (((PyListObject*) result)->ob_item == nullptr) {
            goto error;
        }
        ((PyListObject*) result)->allocated = slicelength;
        src = self->ob_item;
        dest = ((PyListObject*) result)->ob_item;
        for (cur = start, i = 0; i < slicelength;
             cur += (size_t) step, i++) {
            it = src[cur];
            Py_INCREF(it);
            dest[i] = it;
        }
        Py_SET_SIZE(result, slicelength);
    }
error:
    Py_DECREF(o);
    return result;
}


PyObject* PyJit_SubscrListSlice(PyObject* o, Py_ssize_t start, Py_ssize_t stop) {
    Py_ssize_t slicelength;
    PyObject* result = nullptr;
    if (!PyList_CheckExact(o)) {
        PyErr_SetString(PyExc_TypeError, "Invalid type for const slice");
        goto error;
    }
    slicelength = PySlice_AdjustIndices(Py_SIZE(o), &start, &stop, 1);

    if (slicelength <= 0 && start > 0 && stop > 0) {
        result = PyList_New(0);
    } else {
        result = PyList_GetSlice(o, start, stop);
    }
error:
    Py_DECREF(o);
    return result;
}

// TODO: Rewrite this function more efficiently.
PyObject* PyJit_SubscrListReversed(PyObject* o) {
    Py_ssize_t slicelength = Py_SIZE(o), i;
    size_t cur;
    PyObject* result = nullptr;
    PyObject* it;
    PyObject **src, **dest;
    if (!PyList_CheckExact(o)) {
        PyErr_SetString(PyExc_TypeError, "Invalid type for const slice");
        goto error;
    }
    result = PyList_New(0);
    ((PyListObject*) result)->ob_item = PyMem_New(PyObject*, slicelength);
    if (((PyListObject*) result)->ob_item == nullptr) {
        goto error;
    }
    ((PyListObject*) result)->allocated = slicelength;
    src = ((PyListObject*) o)->ob_item;
    dest = ((PyListObject*) result)->ob_item;
    for (cur = slicelength - 1, i = 0; i < slicelength; cur--, i++) {
        it = src[cur];
        Py_INCREF(it);
        dest[i] = it;
    }
    Py_SET_SIZE(result, slicelength);
error:
    Py_DECREF(o);
    return result;
}

PyObject* PyJit_SubscrTuple(PyObject* o, PyObject* key) {
    if (!PyTuple_CheckExact(o))
        return PyJit_Subscr(o, key);
    PyObject* res;

    if (PyIndex_Check(key)) {
        Py_ssize_t key_value;
        key_value = PyNumber_AsSsize_t(key, PyExc_IndexError);
        if (key_value == -1 && PyErr_Occurred()) {
            res = nullptr;
        } else if (key_value < 0) {
            // Supports negative indexes without converting back to PyLong..
            res = PySequence_GetItem(o, key_value);
        } else {
            res = PyTuple_GetItem(o, key_value);
            Py_XINCREF(res);
        }
    } else {
        return PyJit_Subscr(o, key);
    }
    Py_DECREF(key);
    Py_DECREF(o);
    return res;
}

PyObject* PyJit_SubscrTupleIndex(PyObject* o, PyObject* key, Py_ssize_t index) {
    if (!PyTuple_CheckExact(o))
        return PyJit_Subscr(o, key);
    PyObject* res = PyTuple_GetItem(o, index);
    Py_XINCREF(res);
    Py_DECREF(o);
    Py_DECREF(key);
    return res;
}

PyObject* PyJit_RichCompare(PyObject* left, PyObject* right, size_t op) {
    auto res = PyObject_RichCompare(left, right, op);
    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_Contains(PyObject* left, PyObject* right) {
    auto res = PySequence_Contains(right, left);
    Py_DECREF(left);
    Py_DECREF(right);
    if (res < 0) {
        return nullptr;
    }
    auto ret = res ? Py_True : Py_False;
    Py_INCREF(ret);
    return ret;
}

PyObject* PyJit_NotContains(PyObject* left, PyObject* right) {
    auto res = PySequence_Contains(right, left);
    Py_DECREF(left);
    Py_DECREF(right);
    if (res < 0) {
        return nullptr;
    }
    auto ret = res ? Py_False : Py_True;
    Py_INCREF(ret);
    return ret;
}

PyObject* PyJit_NewFunction(PyObject* code, PyObject* qualname, PyFrameObject* frame) {
    auto res = PyFunction_NewWithQualName(code, frame->f_globals, qualname);
    Py_DECREF(code);
    Py_DECREF(qualname);
    return res;
}

PyObject* PyJit_LoadClosure(PyFrameObject* frame, int32_t index) {
    PyObject** cells = frame->f_localsplus + frame->f_code->co_nlocals;
    PyObject* value = cells[index];

    if (value == nullptr) {
        format_exc_unbound(frame->f_code, (int) index);
    } else {
        Py_INCREF(value);
    }
    return value;
}

PyObject* PyJit_SetClosure(PyObject* closure, PyObject* func) {
    PyFunction_SetClosure(func, closure);
    Py_DECREF(closure);
    return func;
}

PyObject* PyJit_BuildSlice(PyObject* start, PyObject* stop, PyObject* step) {
    auto slice = PySlice_New(start, stop, step);
    Py_DECREF(start);
    Py_DECREF(stop);
    Py_XDECREF(step);
    return slice;
}

PyObject* PyJit_UnaryPositive(PyObject* value) {
    ASSERT_ARG(value);
    auto res = PyNumber_Positive(value);
    Py_DECREF(value);
    return res;
}

PyObject* PyJit_UnaryNegative(PyObject* value) {
    ASSERT_ARG(value);
    auto res = PyNumber_Negative(value);
    Py_DECREF(value);
    return res;
}

PyObject* PyJit_UnaryNot(PyObject* value) {
    ASSERT_ARG(value);
    int err = PyObject_IsTrue(value);
    Py_DECREF(value);
    if (err == 0) {
        Py_INCREF(Py_True);
        return Py_True;
    } else if (err > 0) {
        Py_INCREF(Py_False);
        return Py_False;
    }
    return nullptr;
}

PyObject* PyJit_UnaryInvert(PyObject* value) {
    ASSERT_ARG(value);
    auto res = PyNumber_Invert(value);
    Py_DECREF(value);
    return res;
}

PyObject* PyJit_NewList(int32_t size) {
    auto list = PyList_New(size);
    return list;
}

PyObject* PyJit_ListAppend(PyObject* list, PyObject* value) {
    ASSERT_ARG(list);
    if (!PyList_CheckExact(list)) {
        PyErr_SetString(PyExc_TypeError, "Expected list to internal call");
        Py_DECREF(list);
        return nullptr;
    }
    int err = PyList_Append(list, value);
    Py_DECREF(value);
    if (err) {
        return nullptr;
    }
    return list;
}

PyObject* PyJit_SetAdd(PyObject* set, PyObject* value) {
    ASSERT_ARG(set);
    int err;
    err = PySet_Add(set, value);
    Py_DECREF(value);
    if (err != 0) {
        goto error;
    }
    return set;
error:
    return nullptr;
}

PyObject* PyJit_UpdateSet(PyObject* iterable, PyObject* set) {
    ASSERT_ARG(set);
    int res = _PySet_Update(set, iterable);
    Py_DECREF(iterable);
    if (res < 0)
        goto error;
    return set;
error:
    return nullptr;
}

PyObject* PyJit_MapAdd(PyObject* map, PyObject* key, PyObject* value) {
    ASSERT_ARG(map);
    if (!PyDict_Check(map)) {
        PyErr_SetString(PyExc_TypeError,
                        "invalid argument type to MapAdd");
        Py_DECREF(map);
        return nullptr;
    }
    int err = PyDict_SetItem(map, key, value); /* v[w] = u */
    Py_DECREF(value);
    Py_DECREF(key);
    if (err) {
        return nullptr;
    }
    return map;
}

PyObject* PyJit_Multiply(PyObject* left, PyObject* right) {
    auto res = PyNumber_Multiply(left, right);
    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_TrueDivide(PyObject* left, PyObject* right) {
    auto res = PyNumber_TrueDivide(left, right);
    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_FloorDivide(PyObject* left, PyObject* right) {
    auto res = PyNumber_FloorDivide(left, right);
    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_Power(PyObject* left, PyObject* right) {
    auto res = PyNumber_Power(left, right, Py_None);
    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_Modulo(PyObject* left, PyObject* right) {
    auto res = (PyUnicode_CheckExact(left) && (!PyUnicode_Check(right) || PyUnicode_CheckExact(right))) ? PyUnicode_Format(left, right) : PyNumber_Remainder(left, right);

    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_Subtract(PyObject* left, PyObject* right) {
    auto res = PyNumber_Subtract(left, right);
    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_MatrixMultiply(PyObject* left, PyObject* right) {
    auto res = PyNumber_MatrixMultiply(left, right);
    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_BinaryLShift(PyObject* left, PyObject* right) {
    auto res = PyNumber_Lshift(left, right);
    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_BinaryRShift(PyObject* left, PyObject* right) {
    auto res = PyNumber_Rshift(left, right);
    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_BinaryAnd(PyObject* left, PyObject* right) {
    auto res = PyNumber_And(left, right);
    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_BinaryXor(PyObject* left, PyObject* right) {
    auto res = PyNumber_Xor(left, right);
    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_BinaryOr(PyObject* left, PyObject* right) {
    auto res = PyNumber_Or(left, right);
    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_InplacePower(PyObject* left, PyObject* right) {
    auto res = PyNumber_InPlacePower(left, right, Py_None);
    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_InplaceMultiply(PyObject* left, PyObject* right) {
    auto res = PyNumber_InPlaceMultiply(left, right);
    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_InplaceMatrixMultiply(PyObject* left, PyObject* right) {
    auto res = PyNumber_InPlaceMatrixMultiply(left, right);
    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_InplaceTrueDivide(PyObject* left, PyObject* right) {
    auto res = PyNumber_InPlaceTrueDivide(left, right);
    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_InplaceFloorDivide(PyObject* left, PyObject* right) {
    auto res = PyNumber_InPlaceFloorDivide(left, right);
    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_InplaceModulo(PyObject* left, PyObject* right) {
    auto res = PyNumber_InPlaceRemainder(left, right);
    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_InplaceAdd(PyObject* left, PyObject* right) {
    PyObject* res;
    if (PyUnicode_CheckExact(left) && PyUnicode_CheckExact(right)) {
        PyUnicode_Append(&left, right);
        res = left;
    } else {
        res = PyNumber_InPlaceAdd(left, right);
        Py_DECREF(left);
    }
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_InplaceSubtract(PyObject* left, PyObject* right) {
    auto res = PyNumber_InPlaceSubtract(left, right);
    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_InplaceLShift(PyObject* left, PyObject* right) {
    auto res = PyNumber_InPlaceLshift(left, right);
    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_InplaceRShift(PyObject* left, PyObject* right) {
    auto res = PyNumber_InPlaceRshift(left, right);
    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_InplaceAnd(PyObject* left, PyObject* right) {
    auto res = PyNumber_InPlaceAnd(left, right);
    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_InplaceXor(PyObject* left, PyObject* right) {
    auto res = PyNumber_InPlaceXor(left, right);
    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

PyObject* PyJit_InplaceOr(PyObject* left, PyObject* right) {
    auto res = PyNumber_InPlaceOr(left, right);
    Py_DECREF(left);
    Py_DECREF(right);
    return res;
}

int PyJit_PrintExpr(PyObject* value) {
    _Py_IDENTIFIER(displayhook);
    PyObject* hook = _PySys_GetObjectId(&PyId_displayhook);
    PyObject* res;
    if (hook == nullptr) {
        PyErr_SetString(PyExc_RuntimeError,
                        "lost sys.displayhook");
        Py_DECREF(value);
        return 1;
    }
    res = PyObject_CallOneArg(hook, value);
    Py_DECREF(value);
    if (res == nullptr) {
        return 1;
    }
    Py_DECREF(res);
    return 0;
}

void PyJit_HandleException(PyObject** exc, PyObject** val, PyObject** tb, PyObject** oldexc, PyObject** oldval, PyObject** oldtb) {
    auto tstate = PyThreadState_GET();

    _PyErr_StackItem* exc_info = tstate->exc_info;
    // we take ownership of these into locals...
    if (tstate->curexc_type != nullptr) {
        *oldexc = exc_info->exc_type;
    } else {
        *oldexc = Py_None;
        Py_INCREF(Py_None);
    }
    *oldval = exc_info->exc_value;
    *oldtb = exc_info->exc_traceback;

    PyErr_Fetch(exc, val, tb);
    /* Make the raw exception data
    available to the handler,
    so a program can emulate the
    Python main loop. */
    PyErr_NormalizeException(
            exc, val, tb);
    if (tb != nullptr)
        PyException_SetTraceback(*val, *tb);
    else
        PyException_SetTraceback(*val, Py_None);
    Py_INCREF(*exc);
    exc_info->exc_type = *exc;
    Py_INCREF(*val);
    exc_info->exc_value = *val;
    exc_info->exc_traceback = *tb;
    if (*tb == nullptr)
        *tb = Py_None;
    Py_INCREF(*tb);
}

void PyJit_UnwindEh(PyObject* exc, PyObject* val, PyObject* tb) {
    auto tstate = PyThreadState_GET();
    if (val != nullptr && !PyExceptionInstance_Check(val)) {
        PyErr_SetString(PyExc_RuntimeError, "Error unwinding exception data");
        return;
    }
    auto exc_info = tstate->exc_info;
    auto oldtype = exc_info->exc_type;
    auto oldvalue = exc_info->exc_value;
    auto oldtraceback = exc_info->exc_traceback;
    exc_info->exc_type = exc;
    exc_info->exc_value = val;
    exc_info->exc_traceback = tb;
    Py_XDECREF(oldtype);
    Py_XDECREF(oldvalue);
    Py_XDECREF(oldtraceback);
}

#define CANNOT_CATCH_MSG "catching classes that do not inherit from " \
                         "BaseException is not allowed"

PyObject* PyJit_CompareExceptions(PyObject* v, PyObject* w) {
    if (PyTuple_Check(w)) {
        Py_ssize_t i, length;
        length = PyTuple_Size(w);
        for (i = 0; i < length; i += 1) {
            PyObject* exc = PyTuple_GET_ITEM(w, i);
            if (!PyExceptionClass_Check(exc)) {
                PyErr_SetString(PyExc_TypeError,
                                CANNOT_CATCH_MSG);
                Py_DECREF(v);
                Py_DECREF(w);
                return nullptr;
            }
        }
    } else {
        if (!PyExceptionClass_Check(w)) {
            PyErr_SetString(PyExc_TypeError,
                            CANNOT_CATCH_MSG);
            Py_DECREF(v);
            Py_DECREF(w);
            return nullptr;
        }
    }
    int res = PyErr_GivenExceptionMatches(v, w);
    Py_DECREF(v);
    Py_DECREF(w);
    v = res ? Py_True : Py_False;
    Py_INCREF(v);
    return v;
}

void PyJit_UnboundLocal(PyObject* name) {
    format_exc_check_arg(
            PyExc_UnboundLocalError,
            UNBOUNDLOCAL_ERROR_MSG,
            name);
}

void PyJit_DebugTrace(char* msg) {
    puts(msg);
}

void PyJit_DebugFault(char* msg, char* context, int32_t index, PyFrameObject* frame) {
    printf("%s %s at %s, %s line %d\n", msg, context, PyUnicode_AsUTF8(frame->f_code->co_filename), PyUnicode_AsUTF8(frame->f_code->co_name), PyCode_Addr2Line(frame->f_code, index));
    if (!PyErr_Occurred()) {
        printf("Instruction failed but no exception set.");
    }
}

void PyJit_DebugPtr(void* ptr) {
    printf("Pointer at %p\n", ptr);
}

void PyJit_DebugType(PyTypeObject* ty) {
    printf("Type at %p (%s)\n", ty, ty->tp_name);
}

void PyJit_DebugPyObject(PyObject* obj) {
    printf("Object at %p -- ", obj);
    printf("%s\n", PyUnicode_AsUTF8(PyObject_Repr(obj)));
}

void PyJit_PyErrRestore(PyObject* tb, PyObject* value, PyObject* exception) {
    if (exception == Py_None) {
        exception = nullptr;
    }
    PyErr_Restore(exception, value, tb);
}

PyObject* PyJit_ImportName(PyObject* level, PyObject* from, PyObject* name, PyFrameObject* f) {
    _Py_IDENTIFIER(__import__);
    PyThreadState* tstate = PyThreadState_GET();
    PyObject* imp_func = _PyDict_GetItemIdWithError(f->f_builtins, &PyId___import__);
    PyObject *args, *res;
    PyObject* stack[5];

    if (imp_func == nullptr) {
        PyErr_SetString(PyExc_ImportError,
                        "__import__ not found");
        return nullptr;
    }

    /* TODO: Add Fast path for not overloaded __import__. */
    Py_INCREF(imp_func);

    stack[0] = name;
    stack[1] = f->f_globals;
    stack[2] = f->f_locals == nullptr ? Py_None : f->f_locals;
    stack[3] = from;
    stack[4] = level;
#ifdef GIL
    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();
#endif
    res = _PyObject_FastCall(imp_func, stack, 5);
#ifdef GIL
    PyGILState_Release(gstate);
#endif
    Py_DECREF(imp_func);
    return res;
}

PyObject* PyJit_ImportFrom(PyObject* v, PyObject* name) {
    PyThreadState* tstate = PyThreadState_GET();
    _Py_IDENTIFIER(__name__);
    PyObject* x;
    PyObject *fullmodname, *pkgname, *pkgpath, *pkgname_or_unknown, *errmsg;

    if (_PyObject_LookupAttr(v, name, &x) != 0) {
        return x;
    }
    /* Issue #17636: in case this failed because of a circular relative
       import, try to fallback on reading the module directly from
       sys.modules. */
    pkgname = _PyObject_GetAttrId(v, &PyId___name__);
    if (pkgname == nullptr) {
        goto error;
    }
    if (!PyUnicode_Check(pkgname)) {
        Py_CLEAR(pkgname);
        goto error;
    }
    fullmodname = PyUnicode_FromFormat("%U.%U", pkgname, name);
    if (fullmodname == nullptr) {
        Py_DECREF(pkgname);
        return nullptr;
    }
    x = PyImport_GetModule(fullmodname);
    Py_DECREF(fullmodname);
    if (x == nullptr && !PyErr_Occurred()) {
        goto error;
    }
    Py_DECREF(pkgname);
    return x;
error:
    pkgpath = PyModule_GetFilenameObject(v);
    if (pkgname == nullptr) {
        pkgname_or_unknown = PyUnicode_FromString("<unknown module name>");
        if (pkgname_or_unknown == nullptr) {
            Py_XDECREF(pkgpath);
            return nullptr;
        }
    } else {
        pkgname_or_unknown = pkgname;
    }

    if (pkgpath == nullptr || !PyUnicode_Check(pkgpath)) {
        PyErr_Clear();
        errmsg = PyUnicode_FromFormat(
                "cannot import name %R from %R (unknown location)",
                name, pkgname_or_unknown);
        /* NULL checks for errmsg and pkgname done by PyErr_SetImportError. */
        PyErr_SetImportError(errmsg, pkgname, nullptr);
    } else {
        _Py_IDENTIFIER(__spec__);
        PyObject* spec = _PyObject_GetAttrId(v, &PyId___spec__);
        const char* fmt =
                _PyModuleSpec_IsInitializing(spec) ? "cannot import name %R from partially initialized module %R "
                                                     "(most likely due to a circular import) (%S)"
                                                   : "cannot import name %R from %R (%S)";
        Py_XDECREF(spec);

        errmsg = PyUnicode_FromFormat(fmt, name, pkgname_or_unknown, pkgpath);
        /* NULL checks for errmsg and pkgname done by PyErr_SetImportError. */
        PyErr_SetImportError(errmsg, pkgname, pkgpath);
    }

    Py_XDECREF(errmsg);
    Py_XDECREF(pkgname_or_unknown);
    Py_XDECREF(pkgpath);
    return nullptr;
}

static int
import_all_from(PyObject* locals, PyObject* v) {
    _Py_IDENTIFIER(__all__);
    _Py_IDENTIFIER(__dict__);
    PyObject* all = _PyObject_GetAttrId(v, &PyId___all__);
    PyObject *dict, *name, *value;
    int skip_leading_underscores = 0;
    int pos, err;

    if (all == nullptr) {
        if (!PyErr_ExceptionMatches(PyExc_AttributeError))
            return -1; /* Unexpected error */
        PyErr_Clear();
        dict = _PyObject_GetAttrId(v, &PyId___dict__);
        if (dict == nullptr) {
            if (!PyErr_ExceptionMatches(PyExc_AttributeError))
                return -1;
            PyErr_SetString(PyExc_ImportError,
                            "from-import-* object has no __dict__ and no __all__");
            return -1;
        }
        all = PyMapping_Keys(dict);
        Py_DECREF(dict);
        if (all == nullptr)
            return -1;
        skip_leading_underscores = 1;
    }

    for (pos = 0, err = 0;; pos++) {
        name = PySequence_GetItem(all, pos);
        if (name == nullptr) {
            if (!PyErr_ExceptionMatches(PyExc_IndexError))
                err = -1;
            else
                PyErr_Clear();
            break;
        }

        if (skip_leading_underscores &&
            PyUnicode_Check(name) &&
            PyUnicode_READY(name) != -1 &&
            PyUnicode_READ_CHAR(name, 0) == '_') {
            Py_DECREF(name);
            continue;
        }
        value = PyObject_GetAttr(v, name);
        if (value == nullptr)
            err = -1;
        else if (PyDict_CheckExact(locals))
            err = PyDict_SetItem(locals, name, value);
        else
            err = PyObject_SetItem(locals, name, value);
        Py_DECREF(name);
        Py_XDECREF(value);
        if (err != 0)
            break;
    }
    Py_DECREF(all);
    return err;
}

int PyJit_ImportStar(PyObject* from, PyFrameObject* f) {
    PyObject* locals;
    int err;
    if (PyFrame_FastToLocalsWithError(f) < 0)
        return 1;

    locals = f->f_locals;
    if (locals == nullptr) {
        PyErr_SetString(PyExc_SystemError,
                        "no locals found during 'import *'");
        return 1;
    }
    err = import_all_from(locals, from);
    PyFrame_LocalsToFast(f, 0);
    Py_DECREF(from);
    return err;
}

PyObject* PyJit_CallKwArgs(PyObject* func, PyObject* callargs, PyObject* kwargs) {
    PyObject* result = nullptr;
    if (!PyDict_CheckExact(kwargs)) {
        PyObject* d = PyDict_New();
        if (d == nullptr) {
            goto error;
        }
        if (PyDict_Update(d, kwargs) != 0) {
            Py_DECREF(d);
            /* PyDict_Update raises attribute
			* error (percolated from an attempt
			* to get 'keys' attribute) instead of
			* a type error if its second argument
			* is not a mapping.
			*/
            if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
                PyErr_Format(PyExc_TypeError,
                             "%.200s%.200s argument after ** "
                             "must be a mapping, not %.200s",
                             PyEval_GetFuncName(func),
                             PyEval_GetFuncDesc(func),
                             kwargs->ob_type->tp_name);
            }
            goto error;
        }
        Py_DECREF(kwargs);
        kwargs = d;
    }

    if (!PyTuple_CheckExact(callargs)) {
        if (Py_TYPE(callargs)->tp_iter == nullptr &&
            !PySequence_Check(callargs)) {
            PyErr_Format(PyExc_TypeError,
                         "%.200s%.200s argument after * "
                         "must be an iterable, not %.200s",
                         PyEval_GetFuncName(func),
                         PyEval_GetFuncDesc(func),
                         callargs->ob_type->tp_name);
            goto error;
        }

        auto tmp = PySequence_Tuple(callargs);
        if (tmp == nullptr) {
            goto error;
        }
        Py_DECREF(callargs);
        callargs = tmp;
    }
#ifdef GIL
    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();
#endif
    result = PyObject_Call(func, callargs, kwargs);
#ifdef GIL
    PyGILState_Release(gstate);
#endif
error:
    Py_DECREF(func);
    Py_DECREF(callargs);
    Py_DECREF(kwargs);
    return result;
}

PyObject* PyJit_CallArgs(PyObject* func, PyObject* callargs) {
    PyObject* result = nullptr;
    if (!PyTuple_CheckExact(callargs)) {
        if (Py_TYPE(callargs)->tp_iter == nullptr &&
            !PySequence_Check(callargs)) {
            PyErr_Format(PyExc_TypeError,
                         "%.200s%.200s argument after * "
                         "must be an iterable, not %.200s",
                         PyEval_GetFuncName(func),
                         PyEval_GetFuncDesc(func),
                         callargs->ob_type->tp_name);
            goto error;
        }
        auto tmp = PySequence_Tuple(callargs);
        if (tmp == nullptr) {
            goto error;
            return nullptr;
        }
        Py_DECREF(callargs);
        callargs = tmp;
    }
#ifdef GIL
    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();
#endif
    result = PyObject_Call(func, callargs, nullptr);
#ifdef GIL
    PyGILState_Release(gstate);
#endif
error:
    Py_DECREF(func);
    Py_DECREF(callargs);
    return result;
}

void PyJit_PushFrame(PyFrameObject* frame) {
    PyThreadState_GET()->frame = frame;
}

void PyJit_PopFrame(PyFrameObject* frame) {
    PyThreadState_GET()->frame = frame->f_back;
}

void PyJit_EhTrace(PyFrameObject* f) {
    PyTraceBack_Here(f);
}

bool PyJit_Raise(PyObject* exc, PyObject* cause) {
    PyObject *type = nullptr, *value = nullptr;

    if (exc == nullptr) {
        /* Reraise */
        PyThreadState* tstate = PyThreadState_GET();
        auto exc_info = _PyErr_GetTopmostException(tstate);
        PyObject* tb;
        type = exc_info->exc_type;
        value = exc_info->exc_value;
        tb = exc_info->exc_traceback;
        if (type == Py_None || type == nullptr) {
            PyErr_SetString(PyExc_RuntimeError,
                            "No active exception to reraise");
            return false;
        }
        Py_XINCREF(type);
        Py_XINCREF(value);
        Py_XINCREF(tb);
        PyErr_Restore(type, value, tb);
        return true;
    }

    /* We support the following forms of raise:
    raise
    raise <instance>
    raise <type> */

    if (PyExceptionClass_Check(exc)) {
        type = exc;
        value = _PyObject_CallNoArg(exc);
        if (value == nullptr)
            goto raise_error;
        if (!PyExceptionInstance_Check(value)) {
            PyErr_Format(PyExc_TypeError,
                         "calling %R should have returned an instance of "
                         "BaseException, not %R",
                         type, Py_TYPE(value));
            goto raise_error;
        }
    } else if (PyExceptionInstance_Check(exc)) {
        value = exc;
        type = PyExceptionInstance_Class(exc);
        Py_INCREF(type);
    } else {
        /* Not something you can raise.  You get an exception
        anyway, just not what you specified :-) */
        Py_DECREF(exc);
        PyErr_SetString(PyExc_TypeError,
                        "exceptions must derive from BaseException");
        goto raise_error;
    }

    if (cause) {
        PyObject* fixed_cause;
        if (PyExceptionClass_Check(cause)) {
            fixed_cause = _PyObject_CallNoArg(cause);
            if (fixed_cause == nullptr)
                goto raise_error;
            Py_DECREF(cause);
        } else if (PyExceptionInstance_Check(cause)) {
            fixed_cause = cause;
        } else if (cause == Py_None) {
            Py_DECREF(cause);
            fixed_cause = nullptr;
        } else {
            PyErr_SetString(PyExc_TypeError,
                            "exception causes must derive from "
                            "BaseException");
            goto raise_error;
        }
        PyException_SetCause(value, fixed_cause);
    }

    PyErr_SetObject(type, value);
    /* PyErr_SetObject incref's its arguments */
    Py_XDECREF(value);
    Py_XDECREF(type);
    return false;

raise_error:
    Py_XDECREF(value);
    Py_XDECREF(type);
    Py_XDECREF(cause);
    return false;
}

void PyJit_PopExcept(PyObject* exc_traceback, PyObject* exc_value, PyObject* exc_type, PyFrameObject* frame) {
    PyObject *type, *value, *traceback;
    _PyErr_StackItem* exc_info;
    auto tstate = PyThreadState_GET();
    PyTryBlock* b = PyFrame_BlockPop(frame);
    if (b->b_type != EXCEPT_HANDLER) {
        PyErr_SetString(PyExc_SystemError,
                        "popped block is not an except handler");
        return;// TODO : Throw this back up to the frame block
    }
    exc_info = tstate->exc_info;
    type = exc_info->exc_type;
    value = exc_info->exc_value;
    traceback = exc_info->exc_traceback;
    exc_info->exc_type = exc_type;
    exc_info->exc_value = exc_value;
    exc_info->exc_traceback = exc_traceback;
    Py_XDECREF(type);
    Py_XDECREF(value);
    Py_XDECREF(traceback);
}

PyObject* PyJit_LoadClassDeref(PyFrameObject* frame, int32_t oparg) {
    PyObject* value;
    PyCodeObject* co = frame->f_code;
    size_t idx = oparg - PyTuple_GET_SIZE(co->co_cellvars);
    if (idx >= ((size_t) PyTuple_GET_SIZE(co->co_freevars))) {
        PyErr_SetString(PyExc_RuntimeError, "Invalid cellref index");
        return nullptr;
    }
    auto name = PyTuple_GET_ITEM(co->co_freevars, idx);
    auto locals = frame->f_locals;
    if (PyDict_CheckExact(locals)) {
        value = PyDict_GetItem(locals, name);
        Py_XINCREF(value);
    } else {
        value = PyObject_GetItem(locals, name);
        if (value == nullptr && PyErr_Occurred()) {
            if (!PyErr_ExceptionMatches(PyExc_KeyError)) {
                return nullptr;
            }
            PyErr_Clear();
        }
    }
    if (!value) {
        auto freevars = frame->f_localsplus + co->co_nlocals;
        PyObject* cell = freevars[oparg];
        value = PyCell_GET(cell);
        if (value == nullptr) {
            format_exc_unbound(co, (int) oparg);
            return nullptr;
        }
        Py_INCREF(value);
    }

    return value;
}

PyObject* PyJit_ExtendList(PyObject* iterable, PyObject* list) {
    ASSERT_ARG(list);
    if (!PyList_CheckExact(list)) {
        PyErr_SetString(PyExc_TypeError, "Expected list to internal function PyJit_ExtendList");
        return nullptr;
    }
    PyObject* none_val = _PyList_Extend((PyListObject*) list, iterable);
    if (none_val == nullptr) {
        if (Py_TYPE(iterable)->tp_iter == nullptr && !PySequence_Check(iterable)) {
            PyErr_Format(PyExc_TypeError,
                         "argument must be an iterable, not %.200s",
                         iterable->ob_type->tp_name);
            goto error;
        }
        Py_DECREF(iterable);
        goto error;
    }
    Py_DECREF(none_val);
    Py_DECREF(iterable);
    return list;
error:
    return nullptr;
}

PyObject* PyJit_ListToTuple(PyObject* list) {
    PyObject* res = PyList_AsTuple(list);
    Py_DECREF(list);
    return res;
}

int PyJit_StoreMap(PyObject* key, PyObject* value, PyObject* map) {
    if (!PyDict_CheckExact(map)) {
        PyErr_SetString(PyExc_TypeError, "Expected dict to internal function PyJit_StoreMap");
        return -1;
    }
    ASSERT_ARG_INT(value);
    auto res = PyDict_SetItem(map, key, value);
    Py_DECREF(key);
    Py_DECREF(value);
    return res;
}

int PyJit_StoreMapNoDecRef(PyObject* key, PyObject* value, PyObject* map) {
    ASSERT_ARG_INT(map);
    ASSERT_ARG_INT(value);
    if (!PyDict_CheckExact(map)) {
        PyErr_SetString(PyExc_TypeError, "Expected dict to internal function PyJit_StoreMapNoDecRef");
        return -1;
    }
    auto res = PyDict_SetItem(map, key, value);
    return res;
}

PyObject* PyJit_BuildDictFromTuples(PyObject* keys_and_values) {
    ASSERT_ARG(keys_and_values);
    auto len = PyTuple_GET_SIZE(keys_and_values) - 1;
    PyObject* keys = PyTuple_GET_ITEM(keys_and_values, len);
    if (keys == nullptr) {
        if (!PyErr_Occurred())
            PyErr_Format(PyExc_TypeError, "Cannot build dict, keys are null.");
        return nullptr;
    }
    if (!PyTuple_Check(keys)) {
        if (!PyErr_Occurred())
            PyErr_Format(PyExc_TypeError, "Cannot build dict, keys are %s,not tuple type.", keys->ob_type->tp_name);
        return nullptr;
    }
    auto map = _PyDict_NewPresized(len);
    if (map == nullptr) {
        goto error;
    }
    for (auto i = 0; i < len; i++) {
        int err;
        PyObject* key = PyTuple_GET_ITEM(keys, i);
        PyObject* value = PyTuple_GET_ITEM(keys_and_values, i);
        err = PyDict_SetItem(map, key, value);
        if (err != 0) {
            Py_DECREF(map);
            goto error;
        }
    }
error:
    Py_DECREF(keys_and_values);// will decref 'keys' tuple as part of its dealloc routine
    return map;
}

PyObject* PyJit_LoadAssertionError() {
    PyObject* value = PyExc_AssertionError;
    Py_INCREF(value);
    return value;
}

PyObject* PyJit_DictUpdate(PyObject* other, PyObject* dict) {
    ASSERT_ARG(dict);
    if (PyDict_Update(dict, other) < 0) {
        if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
            PyErr_Format(PyExc_TypeError,
                         "'%.200s' object is not a mapping",
                         Py_TYPE(other)->tp_name);
        }
        goto error;
    }

    Py_DECREF(other);
    return dict;
error:
    Py_DECREF(other);
    return nullptr;
}

PyObject* PyJit_DictMerge(PyObject* dict, PyObject* other) {
    ASSERT_ARG(dict);
    if (_PyDict_MergeEx(dict, other, 2) < 0) {
        if (PyErr_ExceptionMatches(PyExc_AttributeError)) {
            PyErr_Format(PyExc_TypeError,
                         "'%.200s' object is not a mapping",
                         Py_TYPE(other)->tp_name);
        }
        goto error;
    }

    Py_DECREF(other);
    return dict;
error:
    Py_DECREF(other);
    return nullptr;
}

int PyJit_StoreSubscr(PyObject* value, PyObject* container, PyObject* index) {
    auto res = PyObject_SetItem(container, index, value);
    Py_DECREF(index);
    Py_DECREF(value);
    Py_DECREF(container);
    return res;
}

int PyJit_StoreSubscrIndex(PyObject* value, PyObject* container, PyObject* objIndex, Py_ssize_t index) {
    PyMappingMethods* m;
    int res;

    if (container == nullptr || objIndex == nullptr || value == nullptr) {
        return -1;
    }
    m = Py_TYPE(container)->tp_as_mapping;
    if (m && m->mp_ass_subscript) {
        res = m->mp_ass_subscript(container, objIndex, value);
    } else if (Py_TYPE(container)->tp_as_sequence) {
        res = PySequence_SetItem(container, index, value);
    } else {
        PyErr_Format(PyExc_TypeError,
                     "'%.200s' object does not support item assignment",
                     Py_TYPE(container)->tp_name);
        res = -1;
    }

    Py_DECREF(objIndex);
    Py_DECREF(value);
    Py_DECREF(container);
    return res;
}

int PyJit_StoreSubscrIndexHash(PyObject* value, PyObject* container, PyObject* objIndex, Py_ssize_t index, Py_hash_t hash) {
    if (PyDict_CheckExact(container))
        return PyJit_StoreSubscrDictHash(value, container, objIndex, hash);
    else
        return PyJit_StoreSubscrIndex(value, container, objIndex, index);
}

int PyJit_StoreSubscrDict(PyObject* value, PyObject* container, PyObject* index) {
    if (!PyDict_CheckExact(container))// just incase we got the type wrong.
        return PyJit_StoreSubscr(value, container, index);
    auto res = PyDict_SetItem(container, index, value);
    Py_DECREF(index);
    Py_DECREF(value);
    Py_DECREF(container);
    return res;
}

int PyJit_StoreSubscrDictHash(PyObject* value, PyObject* container, PyObject* index, Py_hash_t hash) {
    if (!PyDict_CheckExact(container))// just incase we got the type wrong.
        return PyJit_StoreSubscr(value, container, index);
    auto res = _PyDict_SetItem_KnownHash(container, index, value, hash);
    Py_DECREF(index);
    Py_DECREF(value);
    Py_DECREF(container);
    return res;
}

int PyJit_StoreSubscrList(PyObject* value, PyObject* container, PyObject* index) {
    int res;
    if (!PyList_CheckExact(container))// just incase we got the type wrong.
        return PyJit_StoreSubscr(value, container, index);
    if (PyIndex_Check(index)) {
        Py_ssize_t key_value;
        key_value = PyNumber_AsSsize_t(index, PyExc_IndexError);
        if (key_value == -1 && PyErr_Occurred()) {
            res = -1;
        } else if (key_value < 0) {
            // Supports negative indexes without converting back to PyLong..
            res = PySequence_SetItem(container, key_value, value);
        } else {
            res = PyList_SetItem(container, key_value, value);
            Py_INCREF(value);
        }
    } else {
        return PyJit_StoreSubscr(value, container, index);
    }
    Py_DECREF(index);
    Py_DECREF(value);
    Py_DECREF(container);
    return res;
}

int PyJit_StoreSubscrListIndex(PyObject* value, PyObject* container, PyObject* objIndex, Py_ssize_t index) {
    int res;
    if (!PyList_CheckExact(container))// just incase we got the type wrong.
        return PyJit_StoreSubscr(value, container, objIndex);
    res = PyList_SetItem(container, index, value);
    Py_INCREF(value);
    Py_DECREF(objIndex);
    Py_DECREF(value);
    Py_DECREF(container);
    return res;
}

int PyJit_DeleteSubscr(PyObject* container, PyObject* index) {
    auto res = PyObject_DelItem(container, index);
    Py_DECREF(index);
    Py_DECREF(container);
    return res;
}

PyObject* PyJit_CallN(PyObject* target, PyObject* args, PyTraceInfo* trace_info) {
    PyObject* res;
    auto tstate = PyThreadState_GET();
    if (!PyTuple_Check(args)) {
        PyErr_Format(PyExc_TypeError,
                     "invalid arguments for function call");
        Py_DECREF(args);
        return nullptr;
    }

    if (PyCFunction_Check(target)) {
        const auto args_vec_size = PyTuple_Size(args);
        auto* args_vec = new PyObject*[args_vec_size];
        for (int i = 0; i < args_vec_size; ++i) {
            auto* arg = PyTuple_GET_ITEM(args, i);
            assert(i < args_vec_size);
            args_vec[i] = arg;
            Py_INCREF(arg);
        }
#ifdef GIL
        PyGILState_STATE gstate;
        gstate = PyGILState_Ensure();
#endif
        if (tstate->cframe->use_tracing && tstate->c_profilefunc) {
            // Call the function with profiling hooks
            int trace_res = trace(tstate, tstate->frame, PyTrace_C_CALL, target, tstate->c_profilefunc, tstate->c_profileobj, trace_info);

            if (trace_res != 0) {
                PyGILState_Release(gstate);
                return nullptr;
            }
            res = PyObject_Vectorcall(target, args_vec, args_vec_size | PY_VECTORCALL_ARGUMENTS_OFFSET, nullptr);
            if (res == nullptr)
                trace(tstate, tstate->frame, PyTrace_C_EXCEPTION, target, tstate->c_profilefunc, tstate->c_profileobj, trace_info);
            else
                trace(tstate, tstate->frame, PyTrace_C_RETURN, target, tstate->c_profilefunc, tstate->c_profileobj, trace_info);
        } else {
            // Regular function call
            res = PyObject_Vectorcall(target, args_vec, args_vec_size | PY_VECTORCALL_ARGUMENTS_OFFSET, nullptr);
        }
#ifdef GIL
        PyGILState_Release(gstate);
#endif
        for (int i = 0; i < args_vec_size; ++i) {
            Py_DECREF(args_vec[i]);
        }
        delete[] args_vec;
    } else {
#ifdef GIL
        PyGILState_STATE gstate;
        gstate = PyGILState_Ensure();
#endif
        res = PyObject_Call(target, args, nullptr);
#ifdef GIL
        PyGILState_Release(gstate);
#endif
    }
    Py_DECREF(args);
    Py_DECREF(target);
    return res;
}

int PyJit_StoreGlobal(PyObject* v, PyFrameObject* f, PyObject* name) {
    int err = PyDict_SetItem(f->f_globals, name, v);
    Py_DECREF(v);
    return err;
}

int PyJit_DeleteGlobal(PyFrameObject* f, PyObject* name) {
    return PyDict_DelItem(f->f_globals, name);
}

PyObject*
PyJit_PyDict_LoadGlobal(PyDictObject* globals, PyDictObject* builtins, PyObject* key) {
    auto res = PyDict_GetItem((PyObject*) globals, key);
    if (res != nullptr) {
        return res;
    }

    return PyDict_GetItem((PyObject*) builtins, key);
}


PyObject* PyJit_LoadGlobal(PyFrameObject* f, PyObject* name) {
    PyObject* v;
    if (PyDict_CheckExact(f->f_globals) && PyDict_CheckExact(f->f_builtins)) {
        v = PyJit_PyDict_LoadGlobal((PyDictObject*) f->f_globals,
                                    (PyDictObject*) f->f_builtins,
                                    name);
        if (v == nullptr) {
            if (!PyErr_Occurred())
                format_exc_check_arg(PyExc_NameError, NAME_ERROR_MSG, name);
            return nullptr;
        }
        Py_INCREF(v);
    } else {
        /* Slow-path if globals or builtins is not a dict */
        v = PyObject_GetItem(f->f_globals, name);
        if (v == nullptr) {
            v = PyObject_GetItem(f->f_builtins, name);
            if (v == nullptr) {
                if (PyErr_ExceptionMatches(PyExc_KeyError))
                    format_exc_check_arg(
                            PyExc_NameError,
                            NAME_ERROR_MSG, name);
                return nullptr;
            } else {
                PyErr_Clear();
            }
        }
    }
    return v;
}

PyObject* PyJit_GetUnboxedIter(PyObject* iterable) {
    if (PyRange_Check(iterable)) {
        auto* r = (py_rangeobject*) iterable;
        auto it = PyObject_New(pyjion_rangeiterobject, &PyjionRangeIter_Type);
        if (it == nullptr)
            return nullptr;

        it->start = PyLong_AsLongLong(r->start);
        it->step = PyLong_AsLongLong(r->step);
        it->len = PyLong_AsLongLong(r->length);
        it->index = 0;
        Py_DECREF(iterable);
        return (PyObject*) it;
    } else {
        PyErr_SetString(PyExc_TypeError, "Iterable is not a range iterator. Cannot unbox.");
        return nullptr;
    }
}

PyObject* PyJit_GetIter(PyObject* iterable) {
    auto res = PyObject_GetIter(iterable);
    Py_DECREF(iterable);
    return res;
}

PyObject* PyJit_IterNext(PyObject* iter) {
    if (iter == nullptr) {
        if (PyErr_Occurred())
            return (PyObject*) SIG_ITER_ERROR;// this shouldn't happen!
        PyErr_Format(PyExc_TypeError,
                     "Unable to iterate, iterator is null.");
        return (PyObject*) SIG_ITER_ERROR;
    } else if (!PyIter_Check(iter)) {
        PyErr_Format(PyExc_TypeError,
                     "Unable to iterate, %s is not iterable.",
                     PyObject_Repr(iter));
        return (PyObject*) SIG_ITER_ERROR;
    }
#ifdef DEBUG
    assert(!PyjionRangeIter_Check(iter));
#endif
#ifdef GIL
    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();
#endif
    auto res = (*iter->ob_type->tp_iternext)(iter);
#ifdef GIL
    PyGILState_Release(gstate);
#endif
    if (res == nullptr) {
        if (PyErr_Occurred()) {
            if (!PyErr_ExceptionMatches(PyExc_StopIteration)) {
                return (PyObject*) SIG_ITER_ERROR;
            }
            PyErr_Clear();
            return (PyObject*) SIG_STOP_ITER;
        } else {
            return (PyObject*) SIG_STOP_ITER;
        }
    }
    return res;
}

PyObject* PyJit_IterNextUnboxed(PyObject* iter) {
    if (PyjionRangeIter_Check(iter)) {
        return (*iter->ob_type->tp_iternext)(iter);
    } else if (iter->ob_type == &PyRangeIter_Type) {
        auto next = (*iter->ob_type->tp_iternext)(iter);
        if (next == nullptr){
            return (PyObject*) SIG_STOP_ITER;
        }
        return (PyObject*)PyLong_AsLongLong(next); // Unbox the value
    } else {
        // Err.. I'm outta ideas
        PyErr_SetString(PyExc_ValueError, "Invalid type in PyJit_IterNextUnboxed");
        return 0;
    }
}

PyObject* PyJit_CellGet(PyFrameObject* frame, int32_t index) {
    PyObject** cells = frame->f_localsplus + frame->f_code->co_nlocals;
    PyObject* value = PyCell_GET(cells[index]);

    if (value == nullptr) {
        format_exc_unbound(frame->f_code, (int) index);
    } else {
        Py_INCREF(value);
    }
    return value;
}

void PyJit_CellSet(PyObject* value, PyFrameObject* frame, int32_t index) {
    PyObject** cells = frame->f_localsplus + frame->f_code->co_nlocals;
    auto cell = cells[index];
    if (cell == nullptr) {
        cells[index] = PyCell_New(value);
    } else {
        auto oldobj = PyCell_Get(cell);
        PyCell_Set(cell, value);
        Py_XDECREF(oldobj);
    }
}

PyObject* PyJit_BuildClass(PyFrameObject* f) {
    _Py_IDENTIFIER(__build_class__);

    PyObject* bc;
    if (PyDict_CheckExact(f->f_builtins)) {
        bc = _PyDict_GetItemIdWithError(f->f_builtins, &PyId___build_class__);
        if (bc == nullptr) {
            PyErr_SetString(PyExc_NameError,
                            "__build_class__ not found");
            return nullptr;
        }
        Py_INCREF(bc);
    } else {
        PyObject* build_class_str = _PyUnicode_FromId(&PyId___build_class__);
        if (build_class_str == nullptr) {
            return nullptr;
        }
        bc = PyObject_GetItem(f->f_builtins, build_class_str);
        if (bc == nullptr) {
            if (PyErr_ExceptionMatches(PyExc_KeyError)) {
                PyErr_SetString(PyExc_NameError, "__build_class__ not found");
                return nullptr;
            }
        }
    }
    return bc;
}

PyObject* PyJit_LoadAttr(PyObject* owner, PyObject* name) {
    PyObject* res = PyObject_GetAttr(owner, name);
    Py_DECREF(owner);
    return res;
}

PyObject* PyJit_LoadAttrHash(PyObject* owner, PyObject* key, Py_hash_t name_hash) {
    auto obj_dict = _PyObject_GetDictPtr(owner);
    if (obj_dict == nullptr || *obj_dict == nullptr) {
        return _PyObject_GenericGetAttrWithDict(owner, key, nullptr, 0);
    }
    PyObject* value = _PyDict_GetItem_KnownHash(*obj_dict, key, name_hash);
    Py_XINCREF(value);
    if (value == nullptr && !PyErr_Occurred())
        _PyErr_SetKeyError(key);
    Py_DECREF(owner);
    return value;
}

int PyJit_StoreAttr(PyObject* value, PyObject* owner, PyObject* name) {
    int res = PyObject_SetAttr(owner, name, value);
    Py_DECREF(owner);
    Py_DECREF(value);
    return res;
}

int PyJit_DeleteAttr(PyObject* owner, PyObject* name) {
    int res = PyObject_DelAttr(owner, name);
    Py_DECREF(owner);
    return res;
}

int PyJit_SetupAnnotations(PyFrameObject* frame) {
    _Py_IDENTIFIER(__annotations__);
    int err;
    PyObject* ann_dict;
    if (frame->f_locals == nullptr) {
        PyErr_Format(PyExc_SystemError,
                     "no locals found when setting up annotations");
        return -1;
    }
    /* check if __annotations__ in locals()... */
    if (PyDict_CheckExact(frame->f_locals)) {
        ann_dict = _PyDict_GetItemIdWithError(frame->f_locals,
                                              &PyId___annotations__);
        if (ann_dict == nullptr) {
            if (PyErr_Occurred()) {
                return -1;
            }
            /* ...if not, create a new one */
            ann_dict = PyDict_New();
            if (ann_dict == nullptr) {
                return -1;
            }
            err = _PyDict_SetItemId(frame->f_locals,
                                    &PyId___annotations__, ann_dict);
            Py_DECREF(ann_dict);
            if (err != 0) {
                return -1;
            }
        }
    } else {
        /* do the same if locals() is not a dict */
        PyObject* ann_str = _PyUnicode_FromId(&PyId___annotations__);
        if (ann_str == nullptr) {
            return -1;
        }
        ann_dict = PyObject_GetItem(frame->f_locals, ann_str);
        if (ann_dict == nullptr) {
            if (!PyErr_ExceptionMatches(PyExc_KeyError)) {
                return -1;
            }
            PyErr_Clear();
            ann_dict = PyDict_New();
            if (ann_dict == nullptr) {
                return -1;
            }
            err = PyObject_SetItem(frame->f_locals, ann_str, ann_dict);
            Py_DECREF(ann_dict);
            if (err != 0) {
                return -1;
            }
        } else {
            Py_DECREF(ann_dict);
        }
    }
    return 0;
}

PyObject* PyJit_LoadName(PyFrameObject* f, PyObject* name) {
    PyObject* locals = f->f_locals;
    PyObject* v;
    if (locals == nullptr) {
        PyErr_Format(PyExc_SystemError,
                     "no locals when loading %R", name);
        return nullptr;
    }
    if (PyDict_CheckExact(locals)) {
        v = PyDict_GetItem(locals, name);
        Py_XINCREF(v);
    } else {
        v = PyObject_GetItem(locals, name);
        if (v == nullptr && PyErr_Occurred()) {
            if (!PyErr_ExceptionMatches(PyExc_KeyError))
                return nullptr;
            PyErr_Clear();
        }
    }
    if (v == nullptr) {
        v = PyDict_GetItem(f->f_globals, name);
        Py_XINCREF(v);
        if (v == nullptr) {
            if (PyDict_CheckExact(f->f_builtins)) {
                v = PyDict_GetItem(f->f_builtins, name);
                if (v == nullptr) {
                    format_exc_check_arg(
                            PyExc_NameError,
                            NAME_ERROR_MSG, name);
                    return nullptr;
                }
                Py_INCREF(v);
            } else {
                v = PyObject_GetItem(f->f_builtins, name);
                if (v == nullptr) {
                    if (PyErr_ExceptionMatches(PyExc_KeyError))
                        format_exc_check_arg(
                                PyExc_NameError,
                                NAME_ERROR_MSG, name);
                    return nullptr;
                }
            }
        }
    }
    return v;
}

PyObject* PyJit_LoadNameHash(PyFrameObject* f, PyObject* name, Py_hash_t name_hash) {
    PyObject* locals = f->f_locals;
    PyObject* v;
    if (locals == nullptr) {
        PyErr_Format(PyExc_SystemError,
                     "no locals when loading %R", name);
        return nullptr;
    }
    if (PyDict_CheckExact(locals)) {
        v = _PyDict_GetItem_KnownHash(locals, name, name_hash);
        Py_XINCREF(v);
    } else {
        v = PyObject_GetItem(locals, name);
        if (v == nullptr && PyErr_Occurred()) {
            if (!PyErr_ExceptionMatches(PyExc_KeyError))
                return nullptr;
            PyErr_Clear();
        }
    }
    if (v == nullptr) {
        v = _PyDict_GetItem_KnownHash(f->f_globals, name, name_hash);
        Py_XINCREF(v);
        if (v == nullptr) {
            if (PyDict_CheckExact(f->f_builtins)) {
                v = _PyDict_GetItem_KnownHash(f->f_builtins, name, name_hash);
                if (v == nullptr) {
                    format_exc_check_arg(
                            PyExc_NameError,
                            NAME_ERROR_MSG, name);
                    return nullptr;
                }
                Py_INCREF(v);
            } else {
                v = PyObject_GetItem(f->f_builtins, name);
                if (v == nullptr) {
                    if (PyErr_ExceptionMatches(PyExc_KeyError))
                        format_exc_check_arg(
                                PyExc_NameError,
                                NAME_ERROR_MSG, name);
                    return nullptr;
                }
            }
        }
    }
    return v;
}

int PyJit_StoreName(PyObject* v, PyFrameObject* f, PyObject* name) {
    PyObject* ns = f->f_locals;
    int err;
    if (ns == nullptr) {
        PyErr_Format(PyExc_SystemError,
                     "no locals found when storing %R", name);
        Py_DECREF(v);
        return 1;
    }
    if (PyDict_CheckExact(ns))
        err = PyDict_SetItem(ns, name, v);
    else
        err = PyObject_SetItem(ns, name, v);
    Py_DECREF(v);
    return err;
}

int PyJit_DeleteName(PyFrameObject* f, PyObject* name) {
    PyObject* ns = f->f_locals;
    int err;
    if (ns == nullptr) {
        PyErr_Format(PyExc_SystemError,
                     "no locals when deleting %R", name);
        return 1;
    }
    err = PyObject_DelItem(ns, name);
    if (err != 0) {
        format_exc_check_arg(PyExc_NameError,
                             NAME_ERROR_MSG,
                             name);
    }
    return err;
}

template<typename T>
inline PyObject* Call(PyObject* target, PyTraceInfo* trace_info) {
    return Call0(target, trace_info);
}

template<typename T, typename... Args>
inline PyObject* VectorCall(PyObject* target, PyTraceInfo* trace_info, Args... args) {
    auto tstate = PyThreadState_GET();
    PyObject* res = nullptr;
    PyObject* _args[sizeof...(args)] = {args...};
#ifdef GIL
    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();
#endif
    if (tstate->cframe->use_tracing && tstate->c_profilefunc) {
        // Call the function with profiling hooks
        int trace_res = trace(tstate, tstate->frame, PyTrace_C_CALL, target, tstate->c_profilefunc, tstate->c_profileobj, trace_info);

        if (trace_res != 0) {
            PyGILState_Release(gstate);
            return nullptr;
        }

        res = _PyObject_VectorcallTstate(tstate, target, _args, sizeof...(args) | PY_VECTORCALL_ARGUMENTS_OFFSET, nullptr);
        if (res == nullptr)
            trace(tstate, tstate->frame, PyTrace_C_EXCEPTION, target, tstate->c_profilefunc, tstate->c_profileobj, trace_info);
        else
            trace(tstate, tstate->frame, PyTrace_C_RETURN, target, tstate->c_profilefunc, tstate->c_profileobj, trace_info);
    } else {
        // Regular function call
        res = _PyObject_VectorcallTstate(tstate, target, _args, sizeof...(args) | PY_VECTORCALL_ARGUMENTS_OFFSET, nullptr);
    }

#ifdef GIL
    PyGILState_Release(gstate);
#endif
    return res;
}

inline PyObject* VectorCall0(PyObject* target, PyTraceInfo* trace_info) {
    auto tstate = PyThreadState_GET();
    PyObject* res = nullptr;
#ifdef GIL
    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();
#endif
    if (tstate->cframe->use_tracing && tstate->c_profilefunc) {
        // Call the function with profiling hooks
        int trace_res = trace(tstate, tstate->frame, PyTrace_C_CALL, target, tstate->c_profilefunc, tstate->c_profileobj, trace_info);

        if (trace_res != 0) {
            PyGILState_Release(gstate);
            return nullptr;
        }
        res = _PyObject_VectorcallTstate(tstate, target, nullptr, 0 | PY_VECTORCALL_ARGUMENTS_OFFSET, nullptr);
        if (res == nullptr)
            trace(tstate, tstate->frame, PyTrace_C_EXCEPTION, target, tstate->c_profilefunc, tstate->c_profileobj, trace_info);
        else
            trace(tstate, tstate->frame, PyTrace_C_RETURN, target, tstate->c_profilefunc, tstate->c_profileobj, trace_info);
    } else {
        // Regular function call
        res = _PyObject_VectorcallTstate(tstate, target, nullptr, 0 | PY_VECTORCALL_ARGUMENTS_OFFSET, nullptr);
    }

#ifdef GIL
    PyGILState_Release(gstate);
#endif
    return res;
}

template<typename T, typename... Args>
inline PyObject* MethCall(PyObject* target, PyTraceInfo* trace_info, Args... args) {
    if (target == nullptr) {
        if (!PyErr_Occurred())
            PyErr_Format(PyExc_TypeError,
                         "missing target in call");
        return nullptr;
    }
    PyObject* res = VectorCall<PyObject*>(target, trace_info, args...);

    Py_DECREF(target);
    for (auto& i : {args...})
        Py_DECREF(i);

    return res;
}

template<typename T, typename... Args>
inline PyObject* Call(PyObject* target, PyTraceInfo* trace_info, Args... args) {
    auto tstate = PyThreadState_GET();
    PyObject* res = nullptr;
    if (target == nullptr) {
        if (!PyErr_Occurred())
            PyErr_Format(PyExc_TypeError,
                         "missing target in call");
        return nullptr;
    }
    if (PyCFunction_Check(target)) {
        res = VectorCall<PyObject*>(target, trace_info, args...);
    } else {
        // TODO : Optimize this to reflect the changes to call_function using vectorcalls
        auto t_args = PyTuple_New(sizeof...(args));
        if (t_args == nullptr) {
            goto error;
        }
        PyObject* _args[sizeof...(args)] = {args...};
        for (int i = 0; i < sizeof...(args); i++) {
            ASSERT_ARG(_args[i]);
            PyTuple_SetItem(t_args, i, _args[i]);
            Py_INCREF(_args[i]);
        }
#ifdef GIL
        PyGILState_STATE gstate;
        gstate = PyGILState_Ensure();
#endif
        res = PyObject_Call(target, t_args, nullptr);
#ifdef GIL
        PyGILState_Release(gstate);
#endif
        Py_DECREF(t_args);
    }
error:
    Py_DECREF(target);
    for (auto& i : {args...})
        Py_DECREF(i);

    return res;
}

PyObject* Call0(PyObject* target, PyTraceInfo* trace_info) {
    PyObject* res = nullptr;
    if (PyErr_Occurred())
        return nullptr;
    if (target == nullptr) {
        if (!PyErr_Occurred())
            PyErr_Format(PyExc_TypeError,
                         "missing target in call");
        return nullptr;
    }
#ifdef GIL
    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();
#endif
    if (PyCFunction_Check(target)) {
        res = VectorCall0(target, trace_info);
    } else {
        res = PyObject_CallNoArgs(target);
    }
#ifdef GIL
    PyGILState_Release(gstate);
#endif
    Py_DECREF(target);
    return res;
}

PyObject* Call1(PyObject* target, PyObject* arg0, PyTraceInfo* trace_info) {
    return Call<PyObject*>(target, trace_info, arg0);
}

PyObject* Call2(PyObject* target, PyObject* arg0, PyObject* arg1, PyTraceInfo* trace_info) {
    return Call<PyObject*>(target, trace_info, arg0, arg1);
}

PyObject* Call3(PyObject* target, PyObject* arg0, PyObject* arg1, PyObject* arg2, PyTraceInfo* trace_info) {
    return Call<PyObject*>(target, trace_info, arg0, arg1, arg2);
}

PyObject* Call4(PyObject* target, PyObject* arg0, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyTraceInfo* trace_info) {
    return Call<PyObject*>(target, trace_info, arg0, arg1, arg2, arg3);
}

PyObject* Call5(PyObject* target, PyObject* arg0, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4, PyTraceInfo* trace_info) {
    return Call<PyObject*>(target, trace_info, arg0, arg1, arg2, arg3, arg4);
}

PyObject* Call6(PyObject* target, PyObject* arg0, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4, PyObject* arg5, PyTraceInfo* trace_info) {
    return Call<PyObject*>(target, trace_info, arg0, arg1, arg2, arg3, arg4, arg5);
}

PyObject* Call7(PyObject* target, PyObject* arg0, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4, PyObject* arg5, PyObject* arg6, PyTraceInfo* trace_info) {
    return Call<PyObject*>(target, trace_info, arg0, arg1, arg2, arg3, arg4, arg5, arg6);
}

PyObject* Call8(PyObject* target, PyObject* arg0, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4, PyObject* arg5, PyObject* arg6, PyObject* arg7, PyTraceInfo* trace_info) {
    return Call<PyObject*>(target, trace_info, arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
}

PyObject* Call9(PyObject* target, PyObject* arg0, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4, PyObject* arg5, PyObject* arg6, PyObject* arg7, PyObject* arg8, PyTraceInfo* trace_info) {
    return Call<PyObject*>(target, trace_info, arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);
}

PyObject* Call10(PyObject* target, PyObject* arg0, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4, PyObject* arg5, PyObject* arg6, PyObject* arg7, PyObject* arg8, PyObject* arg9, PyTraceInfo* trace_info) {
    return Call<PyObject*>(target, trace_info, arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9);
}

PyObject* MethCall0(PyObject* self, PyObject* method, PyTraceInfo* trace_info) {
    PyObject* res = nullptr;
    if (self != nullptr)
        res = MethCall<PyObject*>(method, trace_info, self);
    else {
        res = Call0(method, trace_info);
    }
    return res;
}

PyObject* MethCall1(PyObject* self, PyObject* method, PyObject* arg1, PyTraceInfo* trace_info) {
    PyObject* res;
    if (self != nullptr)
        res = MethCall<PyObject*>(method, trace_info, self, arg1);
    else
        res = MethCall<PyObject*>(method, trace_info, arg1);
    return res;
}

PyObject* MethCall2(PyObject* self, PyObject* method, PyObject* arg1, PyObject* arg2, PyTraceInfo* trace_info) {
    PyObject* res;
    if (self != nullptr)
        res = MethCall<PyObject*>(method, trace_info, self, arg1, arg2);
    else
        res = MethCall<PyObject*>(method, trace_info, arg1, arg2);
    return res;
}

PyObject* MethCall3(PyObject* self, PyObject* method, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyTraceInfo* trace_info) {
    PyObject* res;
    if (self != nullptr)
        res = MethCall<PyObject*>(method, trace_info, self, arg1, arg2, arg3);
    else
        res = MethCall<PyObject*>(method, trace_info, arg1, arg2, arg3);
    return res;
}

PyObject* MethCall4(PyObject* self, PyObject* method, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4, PyTraceInfo* trace_info) {
    PyObject* res;
    if (self != nullptr)
        res = MethCall<PyObject*>(method, trace_info, self, arg1, arg2, arg3, arg4);
    else
        res = MethCall<PyObject*>(method, trace_info, arg1, arg2, arg3, arg4);
    return res;
}

PyObject* MethCall5(PyObject* self, PyObject* method, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4, PyObject* arg5, PyTraceInfo* trace_info) {
    PyObject* res;
    if (self != nullptr)
        res = MethCall<PyObject*>(method, trace_info, self, arg1, arg2, arg3, arg4, arg5);
    else
        res = MethCall<PyObject*>(method, trace_info, arg1, arg2, arg3, arg4, arg5);
    return res;
}

PyObject* MethCall6(PyObject* self, PyObject* method, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4, PyObject* arg5, PyObject* arg6, PyTraceInfo* trace_info) {
    PyObject* res;
    if (self != nullptr)
        res = MethCall<PyObject*>(method, trace_info, self, arg1, arg2, arg3, arg4, arg5, arg6);
    else
        res = MethCall<PyObject*>(method, trace_info, arg1, arg2, arg3, arg4, arg5, arg6);
    return res;
}

PyObject* MethCall7(PyObject* self, PyObject* method, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4, PyObject* arg5, PyObject* arg6, PyObject* arg7, PyTraceInfo* trace_info) {
    PyObject* res;
    if (self != nullptr)
        res = MethCall<PyObject*>(method, trace_info, self, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
    else
        res = MethCall<PyObject*>(method, trace_info, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
    return res;
}

PyObject* MethCall8(PyObject* self, PyObject* method, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4, PyObject* arg5, PyObject* arg6, PyObject* arg7, PyObject* arg8, PyTraceInfo* trace_info) {
    PyObject* res;
    if (self != nullptr)
        res = MethCall<PyObject*>(method, trace_info, self, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);
    else
        res = MethCall<PyObject*>(method, trace_info, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8);
    return res;
}

PyObject* MethCall9(PyObject* self, PyObject* method, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4, PyObject* arg5, PyObject* arg6, PyObject* arg7, PyObject* arg8, PyObject* arg9, PyTraceInfo* trace_info) {
    PyObject* res;
    if (self != nullptr)
        res = MethCall<PyObject*>(method, trace_info, self, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9);
    else
        res = MethCall<PyObject*>(method, trace_info, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9);
    return res;
}

PyObject* MethCall10(PyObject* self, PyObject* method, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4, PyObject* arg5, PyObject* arg6, PyObject* arg7, PyObject* arg8, PyObject* arg9, PyObject* arg10, PyTraceInfo* trace_info) {
    PyObject* res;
    if (self != nullptr)
        res = MethCall<PyObject*>(method, trace_info, self, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10);
    else
        res = MethCall<PyObject*>(method, trace_info, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10);
    return res;
}

PyObject* MethCallN(PyObject* self, PyObject* method, PyObject* args, PyTraceInfo* trace_info) {
    PyObject* res;
    auto tstate = PyThreadState_GET();
    if (!PyTuple_Check(args)) {
        PyErr_Format(PyExc_TypeError,
                     "invalid arguments for method call");
        Py_DECREF(args);
        return nullptr;
    }
    if (self != nullptr) {
        auto target = method;
        if (target == nullptr) {
            PyErr_Format(PyExc_ValueError,
                         "cannot resolve method call");
            Py_DECREF(args);
            return nullptr;
        }
        auto obj = self;
        // We allocate an additional two slots. One is for the `self` argument since we're
        // executing a method. The other is to leave space at the beginning of the vector so we
        // can use the `PY_VECTORCALL_ARGUMENTS_OFFSET` flag and avoid an allocation in the callee.
        const auto args_vec_size = PyTuple_Size(args) + 2;
        auto* args_vec = new PyObject*[args_vec_size];
        args_vec[1] = obj;
        Py_INCREF(obj);
        for (int i = 0; i < PyTuple_Size(args); ++i) {
            auto* arg = PyTuple_GET_ITEM(args, i);
            assert(i + 2 < args_vec_size);
            args_vec[i + 2] = arg;
            Py_INCREF(arg);
        }
#ifdef GIL
        PyGILState_STATE gstate;
        gstate = PyGILState_Ensure();
#endif
        if (tstate->cframe->use_tracing && tstate->c_profilefunc) {
            // Call the function with profiling hooks
            int trace_res = trace(tstate, tstate->frame, PyTrace_C_CALL, target, tstate->c_profilefunc, tstate->c_profileobj, trace_info);

            if (trace_res != 0) {
                PyGILState_Release(gstate);
                return nullptr;
            }
            res = PyObject_Vectorcall(target, args_vec + 1, (args_vec_size - 1) | PY_VECTORCALL_ARGUMENTS_OFFSET, nullptr);
            if (res == nullptr)
                trace(tstate, tstate->frame, PyTrace_C_EXCEPTION, target, tstate->c_profilefunc, tstate->c_profileobj, trace_info);
            else
                trace(tstate, tstate->frame, PyTrace_C_RETURN, target, tstate->c_profilefunc, tstate->c_profileobj, trace_info);
        } else {
            // Regular function call
            res = PyObject_Vectorcall(target, args_vec + 1, (args_vec_size - 1) | PY_VECTORCALL_ARGUMENTS_OFFSET, nullptr);
        }
#ifdef GIL
        PyGILState_Release(gstate);
#endif
        for (int i = 1; i < args_vec_size; ++i) {
            Py_DECREF(args_vec[i]);
        }
        delete[] args_vec;
        Py_DECREF(args);
        Py_DECREF(target);
        Py_DECREF(obj);
        return res;
    } else {
        auto target = method;
#ifdef GIL
        PyGILState_STATE gstate;
        gstate = PyGILState_Ensure();
#endif
        res = PyObject_Call(target, args, nullptr);
#ifdef GIL
        PyGILState_Release(gstate);
#endif
        Py_DECREF(args);
        Py_DECREF(target);
        return res;
    }
}

PyObject* PyJit_KwCallN(PyObject* target, PyObject* args, PyObject* names) {
    PyObject *result = nullptr, *kwArgs = nullptr;

    auto argCount = PyTuple_Size(args) - PyTuple_Size(names);
    PyObject* posArgs;
    posArgs = PyTuple_New(argCount);
    if (posArgs == nullptr) {
        goto error;
    }
    for (auto i = 0; i < argCount; i++) {
        auto item = PyTuple_GetItem(args, i);
        Py_INCREF(item);
        if (PyTuple_SetItem(posArgs, i, item) == -1) {
            goto error;
        }
    }
    kwArgs = PyDict_New();
    if (kwArgs == nullptr) {
        goto error;
    }

    for (auto i = 0; i < PyTuple_GET_SIZE(names); i++) {
        PyDict_SetItem(
                kwArgs,
                PyTuple_GET_ITEM(names, i),
                PyTuple_GET_ITEM(args, i + argCount));
    }
#ifdef GIL
    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();
#endif
    result = PyObject_Call(target, posArgs, kwArgs);
#ifdef GIL
    PyGILState_Release(gstate);
#endif
error:
    Py_XDECREF(kwArgs);
    Py_XDECREF(posArgs);
    Py_DECREF(target);
    Py_DECREF(args);
    Py_DECREF(names);
    return result;
}

PyObject* PyJit_PyTuple_New(int32_t len) {
    return PyTuple_New(len);
}

PyObject* PyJit_Is(PyObject* lhs, PyObject* rhs) {
    auto res = lhs == rhs ? Py_True : Py_False;
    Py_DECREF(lhs);
    Py_DECREF(rhs);
    Py_INCREF(res);
    return res;
}

PyObject* PyJit_IsNot(PyObject* lhs, PyObject* rhs) {
    auto res = lhs == rhs ? Py_False : Py_True;
    Py_DECREF(lhs);
    Py_DECREF(rhs);
    Py_INCREF(res);
    return res;
}

int PyJit_Is_Bool(PyObject* lhs, PyObject* rhs) {
    Py_DECREF(lhs);
    Py_DECREF(rhs);
    return lhs == rhs;
}

int PyJit_IsNot_Bool(PyObject* lhs, PyObject* rhs) {
    Py_DECREF(lhs);
    Py_DECREF(rhs);
    return lhs != rhs;
}

void PyJit_DecRef(PyObject* value) {
    Py_XDECREF(value);
}

PyObject* PyJit_UnicodeJoinArray(PyObject* items, ssize_t count) {
    auto empty = PyUnicode_New(0, 0);
#ifdef DEBUG
    for (size_t i = 0; i < count; i++) {
        if (PyTuple_GET_ITEM(items, i) == nullptr){
            PyErr_Format(PyExc_ValueError, "Invalid item at index %d", i);
            return nullptr;
        }
    }
#endif
    auto res = _PyUnicode_JoinArray(empty, ((PyTupleObject*)items)->ob_item, count);
    Py_DECREF(items);
    Py_DECREF(empty);
    return res;
}

PyObject* PyJit_FormatObject(PyObject* item, PyObject* fmtSpec) {
    auto res = PyObject_Format(item, fmtSpec);
    Py_DECREF(item);
    Py_DECREF(fmtSpec);
    return res;
}

int PyJit_LoadMethod(PyObject* obj, PyObject* name, PyObject** method, PyObject** self) {
    PyObject* meth = nullptr;
    int meth_found = _PyObject_GetMethod(obj, name, &meth);

    if (meth == nullptr) {
        /* Most likely attribute wasn't found. */
        return -1;
    }

    if (meth_found) {
        /* We can bypass temporary bound method object.
                   meth is unbound method and obj is self.

                   meth | self | arg1 | ... | argN
                 */
        *method = meth;
        *self = obj;  // self
    }
    else {
        /* meth is not an unbound method (but a regular attr, or
                   something was returned by a descriptor protocol).  Set
                   the second element of the stack to NULL, to signal
                   CALL_METHOD that it's not a method call.

                   NULL | meth | arg1 | ... | argN
                */
        *method = meth;
        Py_DECREF(obj);
        *self = nullptr;
    }
    return 0;
}

PyObject* PyJit_FormatValue(PyObject* item) {
    if (PyUnicode_CheckExact(item)) {
        return item;
    }

    auto res = PyObject_Format(item, nullptr);
    Py_DECREF(item);
    return res;
}

inline int trace(PyThreadState* tstate, PyFrameObject* f, int ty, PyObject* args, Py_tracefunc func, PyObject* tracearg, PyTraceInfo* trace_info) {
    if (func == nullptr)
        return -1;
    if (tstate->tracing)
        return 0;
    tstate->tracing++;
    tstate->cframe->use_tracing = 0;
    if (f->f_lasti < 0) {
        f->f_lineno = f->f_code->co_firstlineno;
    } else {
        initialize_trace_info(trace_info, f);
        f->f_lineno = _PyCode_CheckLineNumber(f->f_lasti * 2, &trace_info->bounds);
    }
    int result = func(tracearg, f, ty, args);
    tstate->cframe->use_tracing = ((tstate->c_tracefunc != nullptr) || (tstate->c_profilefunc != nullptr));
    tstate->tracing--;
    return result;
}

inline void initialize_trace_info(PyTraceInfo* trace_info, PyFrameObject* frame) {
    if (trace_info->code != frame->f_code) {
        trace_info->code = frame->f_code;
        const char* linetable = PyBytes_AS_STRING(trace_info->code->co_linetable);
        Py_ssize_t length = PyBytes_GET_SIZE(trace_info->code->co_linetable);
        trace_info->bounds.opaque.lo_next = linetable;
        trace_info->bounds.opaque.limit = trace_info->bounds.opaque.lo_next + length;
        trace_info->bounds.ar_start = -1;
        trace_info->bounds.ar_end = 0;
        trace_info->bounds.opaque.computed_line = trace_info->code->co_firstlineno;
        trace_info->bounds.ar_line = -1;
    }
}

void PyJit_TraceLine(PyFrameObject* f, int instr_prev, PyTraceInfo* trace_info) {
    int result = 0;
    auto tstate = PyThreadState_GET();
    /* If the last instruction falls at the start of a line or if it
       represents a jump backwards, update the frame's line number and
       then call the trace function if we're tracing source lines.
    */
    initialize_trace_info(trace_info, f);
    int lastline = _PyCode_CheckLineNumber(instr_prev * 2, &trace_info->bounds);
    int line = _PyCode_CheckLineNumber(f->f_lasti * 2, &trace_info->bounds);
    if (line != -1 && f->f_trace_lines) {
        /* Trace backward edges or if line number has changed */
        if (f->f_lasti < instr_prev || line != lastline) {
            result = protected_trace(tstate, f, PyTrace_LINE, Py_None, tstate->c_tracefunc, tstate->c_traceobj, trace_info);
        }
    }
    /* Always emit an opcode event if we're tracing all opcodes. */
    if (f->f_trace_opcodes) {
        result = protected_trace(tstate, f, PyTrace_OPCODE, Py_None, tstate->c_tracefunc, tstate->c_traceobj, trace_info);
    }
    // TODO : Handle line trace exception
    // return result;
}

inline int protected_trace(
        PyThreadState* tstate, PyFrameObject* f,
        int ty, PyObject* arg,
        Py_tracefunc func, PyObject* tracearg,
        PyTraceInfo* trace_info) {
    int result = 0;
    PyObject *type, *value, *traceback;
    PyErr_Fetch(&type, &value, &traceback);

    if (tstate->tracing)
        return 0;
    result = trace(tstate, f, ty, arg, func, tracearg, trace_info);

    if (result == 0) {
        PyErr_Restore(type, value, traceback);
        return 0;
    } else {
        Py_XDECREF(type);
        Py_XDECREF(value);
        Py_XDECREF(traceback);
        return -1;
    }
}

void PyJit_TraceFrameEntry(PyFrameObject* f, PyTraceInfo* trace_info) {
    auto tstate = PyThreadState_GET();
    if (trace_info->cframe.use_tracing && tstate->c_tracefunc != nullptr) {
        protected_trace(tstate, f, PyTrace_CALL, Py_None, tstate->c_tracefunc, tstate->c_traceobj, trace_info);
    }
}

void PyJit_TraceFrameExit(PyFrameObject* f, PyTraceInfo* trace_info, PyObject* returnValue) {
    auto tstate = PyThreadState_GET();
    if (trace_info->cframe.use_tracing && tstate->c_tracefunc != nullptr) {
        protected_trace(tstate, f, PyTrace_RETURN, returnValue, tstate->c_tracefunc, tstate->c_traceobj, trace_info);
    }
}

void PyJit_ProfileFrameEntry(PyFrameObject* f, PyTraceInfo* trace_info) {
    auto tstate = PyThreadState_GET();
    if (trace_info->cframe.use_tracing && tstate->c_profilefunc != nullptr) {
        protected_trace(tstate, f, PyTrace_CALL, Py_None, tstate->c_profilefunc, tstate->c_profileobj, trace_info);
    }
}

void PyJit_ProfileFrameExit(PyFrameObject* f, PyTraceInfo* trace_info, PyObject* returnValue) {
    auto tstate = PyThreadState_GET();
    if (trace_info->cframe.use_tracing && tstate->c_profilefunc != nullptr) {
        protected_trace(tstate, f, PyTrace_RETURN, returnValue, tstate->c_profilefunc, tstate->c_profileobj, trace_info);
    }
}

void PyJit_TraceFrameException(PyFrameObject* f, PyTraceInfo* trace_info) {
    auto tstate = PyThreadState_GET();
    if (trace_info->cframe.use_tracing && tstate->c_tracefunc != nullptr) {
        PyObject *type, *value, *traceback, *orig_traceback, *arg;
        int result = 0;
        PyErr_Fetch(&type, &value, &orig_traceback);
        if (value == nullptr) {
            value = Py_None;
            Py_INCREF(value);
        }
        if (type == nullptr)
            type = PyExc_Exception;
        PyErr_NormalizeException(&type, &value, &orig_traceback);
        traceback = (orig_traceback != nullptr) ? orig_traceback : Py_None;
        arg = PyTuple_Pack(3, type, value, traceback);
        if (arg == nullptr) {
            PyErr_Restore(type, value, orig_traceback);
            return;
        }

        if (tstate->tracing)
            return;
        result = trace(tstate, f, PyTrace_EXCEPTION, arg, tstate->c_tracefunc, tstate->c_traceobj, trace_info);
        Py_DECREF(arg);
        if (result == 0) {
            PyErr_Restore(type, value, orig_traceback);
        } else {
            Py_XDECREF(type);
            Py_XDECREF(value);
            Py_XDECREF(orig_traceback);
        }
    }
}

PyObject* PyJit_GetListItemReversed(PyObject* list, size_t index) {
    return PyList_GET_ITEM(list, PyList_GET_SIZE(list) - index - 1);
}

double PyJit_LongTrueDivide(long long x, long long y) {
    if (y == 0) {
        PyErr_SetString(PyExc_ZeroDivisionError, "Divide by zero");
        return INFINITY;
    }
    return (double) x / (double) y;
}

long long PyJit_LongFloorDivide(long long x, long long y) {
    if (y == 0) {
        PyErr_SetString(PyExc_ZeroDivisionError, "Divide by zero");
        return MAXLONG;
    }
    // C++ handles -ve divisors weirdly, use normal long division for +ve
    if (0 < (x ^ y)) {
        return x / y;
    } else {
        lldiv_t res = lldiv(x, y);
        return (res.rem) ? res.quot - 1 : res.quot;
    }
}

long long PyJit_LongMod(long long x, long long y) {
    if (y == 0) {
        PyErr_SetString(PyExc_ZeroDivisionError, "Divide by zero");
        return MAXLONG;
    }
    // C++ handles -ve divisors weirdly, use normal long division for +ve
    if (0 < (x ^ y)) {
        return x % y;
    } else {
        return (y + (x % y)) % y;
    }
}

long long PyJit_LongPow(long long base, long long exp) {
    long long result = 1;
    for (;;) {
        if (exp & 1)
            result *= base;
        exp >>= 1;
        if (!exp)
            break;
        base *= base;
    }
    return result;
}

/* determine whether x is an odd integer or not;  assumes that
   x is not an infinity or nan. */
#define DOUBLE_IS_ODD_INTEGER(x) (fmod(fabs(x), 2.0) == 1.0)

/*
 * An unboxed copy of float_pow() from the CPython code base, with the
 * exception of complex number support.
 */
double PyJit_DoublePow(double iv, double iw) {
    int negate_result = 0;

    /* Sort out special cases here instead of relying on pow() */
    if (iw == 0) { /* v**0 is 1, even 0**0 */
        return 1.0;
    }
    if (Py_IS_NAN(iv)) { /* nan**w = nan, unless w == 0 */
        return iv;
    }
    if (Py_IS_NAN(iw)) { /* v**nan = nan, unless v == 1; 1**nan = 1 */
        return iv == 1.0 ? 1.0 : iw;
    }
    if (Py_IS_INFINITY(iw)) {
        /* v**inf is: 0.0 if abs(v) < 1; 1.0 if abs(v) == 1; inf if
         *     abs(v) > 1 (including case where v infinite)
         *
         * v**-inf is: inf if abs(v) < 1; 1.0 if abs(v) == 1; 0.0 if
         *     abs(v) > 1 (including case where v infinite)
         */
        iv = fabs(iv);
        if (iv == 1.0)
            return 1.0;
        else if ((iw > 0.0) == (iv > 1.0))
            return fabs(iw); /* return inf */
        else
            return 0.0;
    }
    if (Py_IS_INFINITY(iv)) {
        /* (+-inf)**w is: inf for w positive, 0 for w negative; in
         *     both cases, we need to add the appropriate sign if w is
         *     an odd integer.
         */
        int iw_is_odd = DOUBLE_IS_ODD_INTEGER(iw);
        if (iw > 0.0)
            return iw_is_odd ? iv : fabs(iv);
        else
            return iw_is_odd ? copysign(0.0, iv) : 0.0;
    }
    if (iv == 0.0) { /* 0**w is: 0 for w positive, 1 for w zero
                         (already dealt with above), and an error
                         if w is negative. */
        int iw_is_odd = DOUBLE_IS_ODD_INTEGER(iw);
        if (iw < 0.0) {
            PyErr_SetString(PyExc_ZeroDivisionError,
                            "0.0 cannot be raised to a "
                            "negative power");
            return INFINITY;
        }
        /* use correct sign if iw is odd */
        return iw_is_odd ? iv : 0.0;
    }

    if (iv < 0.0) {
        /* Whether this is an error is a mess, and bumps into libm
         * bugs so we have to figure it out ourselves.
         */
        if (iw != floor(iw)) {
            /* Negative numbers raised to fractional powers
             * become complex.
             */
            PyErr_SetString(PyExc_ValueError,
                            "negative numbers raised to fractional powers not supported.");
            return INFINITY;
        }
        /* iw is an exact integer, albeit perhaps a very large
         * one.  Replace iv by its absolute value and remember
         * to negate the pow result if iw is odd.
         */
        iv = -iv;
        negate_result = DOUBLE_IS_ODD_INTEGER(iw);
    }

    if (iv == 1.0) { /* 1**w is 1, even 1**inf and 1**nan */
        /* (-1) ** large_integer also ends up here.  Here's an
         * extract from the comments for the previous
         * implementation explaining why this special case is
         * necessary:
         *
         * -1 raised to an exact integer should never be exceptional.
         * Alas, some libms (chiefly glibc as of early 2003) return
         * NaN and set EDOM on pow(-1, large_int) if the int doesn't
         * happen to be representable in a *C* integer.  That's a
         * bug.
         */
        return negate_result ? -1.0 : 1.0;
    }

    /* Now iv and iw are finite, iw is nonzero, and iv is
     * positive and not equal to 1.0.  We finally allow
     * the platform pow to step in and do the rest.
     */
    errno = 0;
    double ix = pow(iv, iw);
    Py_ADJUST_ERANGE1(ix);
    if (negate_result)
        ix = -ix;

    if (errno != 0) {
        /* We don't expect any errno value other than ERANGE, but
         * the range of libm bugs appears unbounded.
         */
        PyErr_SetFromErrno(errno == ERANGE ? PyExc_OverflowError : PyExc_ValueError);
        return INFINITY;
    }
    return ix;
}



void PyJit_PgcGuardException(PyObject* obj, const char* expected) {
    assert(PyjionUnboxingError != nullptr);
    if (PyErr_Occurred())
        return;
    PyErr_Format(PyjionUnboxingError,
                 "Optimizations are invalid. Pyjion PGC expected %s, but %s is a %s. Try disabling PGC pyjion.config(pgc=False) or lowering the optimization level to avoid hitting this error.",
                 expected,
                 PyUnicode_AsUTF8(PyObject_Repr(obj)),
                 obj->ob_type->tp_name);
}

PyObject* PyJit_BlockPop(PyFrameObject* frame) {
    if (frame->f_iblock <= 0) {
#ifdef DEBUG
        printf("Warning: block underflow at %d %s %s line %d\n", frame->f_lasti, PyUnicode_AsUTF8(frame->f_code->co_filename),
               PyUnicode_AsUTF8(frame->f_code->co_name), frame->f_lineno);
#endif
        return nullptr;
    }
    return reinterpret_cast<PyObject*>(PyFrame_BlockPop(frame));
}

int64_t PyJit_LongAsLongLong(PyObject* vv, int* failure) {
    if (vv == nullptr) {
        PyErr_SetString(PyExc_ValueError,
                        "Pyjion failed to unbox the integer because it is not initialized.");
        *failure = 1;
        return 0;
    }
    if (PyLong_Check(vv)){
        int64_t result = PyLong_AsLongLong(vv);
        if (result == -1 && PyErr_Occurred()) {
            PyErr_Clear();
            PyErr_Format(PyExc_OverflowError,
                         "Pyjion failed to unbox the integer %s because it is too large. Try disabling PGC pyjion.config(pgc=False) to avoid hitting this error.",
                         PyUnicode_AsUTF8(PyObject_Repr(vv)));
            *failure = 1;
            return MAXLONG;
        }
        return result;
    } else if (PyBool_Check(vv)) {
        return Py_IsTrue(vv) ? 1 : 0;
    } else {
        *failure = 1;
        PyJit_PgcGuardException(vv, "int");
        return MAXLONG;
    }
}

int8_t PyJit_UnboxBool(PyObject* o, int* failure) {
    if (PyBool_Check(o)){
        return Py_IsTrue(o) ? 1 : 0;
    }
    if (PyLong_Check(o)){
        long val = PyLong_AsLong(o);
        if (val == 0 || val == 1) {
            Py_DECREF(o);
            return (int8_t)val;
        }
        *failure = 1;
        PyJit_PgcGuardException(o, "bool");
        return false;
    }
    *failure = 1;
    PyJit_PgcGuardException(o, "bool");
    return false;
}

int PyJit_StoreByteArrayUnboxed(int64_t value, PyObject* array, int64_t index){
    if (index < 0 || index >= Py_SIZE(array)) {
        PyErr_SetString(PyExc_IndexError, "bytearray index out of range");
        Py_DECREF(array);
        return -1;
    }

    if (value < 0 || value > 255) {
        PyErr_SetString(PyExc_ValueError, "byte must be in range(0, 256)");
        Py_DECREF(array);
        return -1;
    }

    PyByteArray_AS_STRING(array)[index] = (char)value;
    Py_DECREF(array);
    return 0;
}