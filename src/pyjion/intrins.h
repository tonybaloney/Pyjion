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

#ifndef PYJION_INTRINS_H
#define PYJION_INTRINS_H

#include <Python.h>
#include <frameobject.h>
#include <vector>

#define NAME_ERROR_MSG \
    "name '%.200s' is not defined"

#define UNBOUNDLOCAL_ERROR_MSG \
    "local variable '%.200s' referenced before assignment"
#define UNBOUNDFREE_ERROR_MSG \
    "free variable '%.200s' referenced before assignment" \
    " in enclosing scope"

typedef struct  {
    PyObject_HEAD
    PyObject* object;
    PyObject* method;
} PyJitMethodLocation;

static PyTypeObject PyJitMethodLocation_Type = {
        PyObject_HEAD_INIT(nullptr)
        "pyjion.method_location",
        sizeof(PyJitMethodLocation),
        0,                                  /*tp_itemsize*/
        /* methods */
        (destructor)PyObject_Del,           /*tp_dealloc*/
        0,                                  /*tp_vectorcall_offset*/
        0,                                  /*tp_getattr*/
        0,                                  /*tp_setattr*/
        0,                                  /*tp_as_async*/
        0,                /*tp_repr*/
        0,                                  /*tp_as_number*/
        0,                                  /*tp_as_sequence*/
        0,                                  /*tp_as_mapping*/
        0,                                  /*tp_hash*/
        0,                                  /*tp_call*/
        0,                                  /*tp_str*/
        0,                                  /*tp_getattro*/
        0,                                  /*tp_setattro*/
        0,                                  /*tp_as_buffer*/
        Py_TPFLAGS_DEFAULT,                 /*tp_flags*/
        0,                                  /*tp_doc*/
        0,                                  /*tp_traverse*/
        0,                                  /*tp_clear*/
        0,                                  /*tp_richcompare*/
        0,                                   /*tp_weaklistoffset*/
        0,                                  /*tp_iter*/
        0,                                  /*tp_iternext*/
        0,                       /*tp_methods*/
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        PyType_GenericNew /* tp_new */
};

static void
format_exc_check_arg(PyObject *exc, const char *format_str, PyObject *obj);

static void
format_exc_unbound(PyCodeObject *co, int oparg);

PyObject* PyJit_Add(PyObject *left, PyObject *right);

PyObject* PyJit_Subscr(PyObject *left, PyObject *right);
PyObject* PyJit_SubscrIndex(PyObject *o, PyObject *key, Py_ssize_t index);
PyObject* PyJit_SubscrIndexHash(PyObject *o, PyObject *key, Py_ssize_t index, Py_hash_t hash);
PyObject* PyJit_SubscrDict(PyObject *o, PyObject *key);
PyObject* PyJit_SubscrDictHash(PyObject *o, PyObject *key, Py_hash_t hash);
PyObject* PyJit_SubscrList(PyObject *o, PyObject *key);
PyObject* PyJit_SubscrListIndex(PyObject *o, PyObject *key, Py_ssize_t index);
PyObject* PyJit_SubscrListSlice(PyObject *o, Py_ssize_t start,  Py_ssize_t stop);
PyObject* PyJit_SubscrListSliceStepped(PyObject *o,  Py_ssize_t start,  Py_ssize_t stop,  Py_ssize_t step);
PyObject* PyJit_SubscrListReversed(PyObject *o);

PyObject* PyJit_SubscrTuple(PyObject *o, PyObject *key);
PyObject* PyJit_SubscrTupleIndex(PyObject *o, PyObject *key, Py_ssize_t index);

PyObject* PyJit_RichCompare(PyObject *left, PyObject *right, size_t op);

PyObject* PyJit_CellGet(PyFrameObject* frame, size_t index);
void PyJit_CellSet(PyObject* value, PyFrameObject* frame, size_t index);

PyObject* PyJit_Contains(PyObject *left, PyObject *right);
PyObject* PyJit_NotContains(PyObject *left, PyObject *right);

PyObject* PyJit_NewFunction(PyObject* code, PyObject* qualname, PyFrameObject* frame);

PyObject* PyJit_LoadClosure(PyFrameObject* frame, size_t index);
PyObject* PyJit_SetClosure(PyObject* closure, PyObject* func);

PyObject* PyJit_BuildSlice(PyObject* start, PyObject* stop, PyObject* step);

PyObject* PyJit_UnaryPositive(PyObject* value);
PyObject* PyJit_UnaryNegative(PyObject* value);
PyObject* PyJit_UnaryNot(PyObject* value);
int PyJit_UnaryNot_Int(PyObject* value);
PyObject* PyJit_UnaryInvert(PyObject* value);

PyObject* PyJit_NewList(size_t size);
PyObject* PyJit_ListAppend(PyObject* list, PyObject* value);
PyObject* PyJit_SetAdd(PyObject* set, PyObject* value);
PyObject* PyJit_UpdateSet(PyObject* iterable, PyObject* set);
PyObject* PyJit_MapAdd(PyObject*map, PyObject*key, PyObject* value);

PyObject* PyJit_Multiply(PyObject *left, PyObject *right);
PyObject* PyJit_TrueDivide(PyObject *left, PyObject *right);
PyObject* PyJit_FloorDivide(PyObject *left, PyObject *right);
PyObject* PyJit_Power(PyObject *left, PyObject *right);
PyObject* PyJit_Modulo(PyObject *left, PyObject *right);
PyObject* PyJit_Subtract(PyObject *left, PyObject *right);
PyObject* PyJit_MatrixMultiply(PyObject *left, PyObject *right);
PyObject* PyJit_BinaryLShift(PyObject *left, PyObject *right);
PyObject* PyJit_BinaryRShift(PyObject *left, PyObject *right);
PyObject* PyJit_BinaryAnd(PyObject *left, PyObject *right);
PyObject* PyJit_BinaryXor(PyObject *left, PyObject *right);
PyObject* PyJit_BinaryOr(PyObject *left, PyObject *right);

PyObject* PyJit_InplacePower(PyObject *left, PyObject *right);
PyObject* PyJit_InplaceMultiply(PyObject *left, PyObject *right);
PyObject* PyJit_InplaceMatrixMultiply(PyObject *left, PyObject *right);
PyObject* PyJit_InplaceTrueDivide(PyObject *left, PyObject *right);
PyObject* PyJit_InplaceFloorDivide(PyObject *left, PyObject *right);
PyObject* PyJit_InplaceModulo(PyObject *left, PyObject *right);
PyObject* PyJit_InplaceAdd(PyObject *left, PyObject *right);
PyObject* PyJit_InplaceSubtract(PyObject *left, PyObject *right);
PyObject* PyJit_InplaceLShift(PyObject *left, PyObject *right);
PyObject* PyJit_InplaceRShift(PyObject *left, PyObject *right);
PyObject* PyJit_InplaceAnd(PyObject *left, PyObject *right);
PyObject* PyJit_InplaceXor(PyObject *left, PyObject *right);
PyObject* PyJit_InplaceOr(PyObject *left, PyObject *right);

int PyJit_PrintExpr(PyObject *value);

const char * ObjInfo(PyObject *obj);
void PyJit_PrepareException(PyObject** exc, PyObject**val, PyObject** tb, PyObject** oldexc, PyObject**oldVal, PyObject** oldTb);
void PyJit_UnwindEh(PyObject*exc, PyObject*val, PyObject*tb);

#define CANNOT_CATCH_MSG "catching classes that do not inherit from "\
                         "BaseException is not allowed"

PyObject* PyJit_CompareExceptions(PyObject*v, PyObject* w);

void PyJit_UnboundLocal(PyObject* name);

void PyJit_DebugTrace(char* msg);
void PyJit_DebugPtr(void* ptr);
void PyJit_DebugType(PyTypeObject* ty);
void PyJit_DebugPyObject(PyObject* obj);

void PyJit_PyErrRestore(PyObject*tb, PyObject*value, PyObject*exception);

PyObject* PyJit_ImportName(PyObject*level, PyObject*from, PyObject* name, PyFrameObject* f);

PyObject* PyJit_ImportFrom(PyObject*v, PyObject* name);

static int
import_all_from(PyObject *locals, PyObject *v);
int PyJit_ImportStar(PyObject*from, PyFrameObject* f);

PyObject* PyJit_CallArgs(PyObject* func, PyObject*callargs);
PyObject* PyJit_CallKwArgs(PyObject* func, PyObject*callargs, PyObject*kwargs);

PyObject* PyJit_KwCallN(PyObject *target, PyObject* args, PyObject* names);

void PyJit_PushFrame(PyFrameObject* frame);
void PyJit_PopFrame(PyFrameObject* frame);

void PyJit_EhTrace(PyFrameObject *f);

int PyJit_Raise(PyObject *exc, PyObject *cause);

PyObject* PyJit_LoadClassDeref(PyFrameObject* frame, size_t oparg);

PyObject* PyJit_ExtendList(PyObject *iterable, PyObject *list);

PyObject* PyJit_LoadAssertionError();

PyObject* PyJit_ListToTuple(PyObject *list);

int PyJit_StoreMap(PyObject *key, PyObject *value, PyObject* map);
int PyJit_StoreMapNoDecRef(PyObject *key, PyObject *value, PyObject* map);

PyObject* PyJit_DictUpdate(PyObject* other, PyObject* dict);
PyObject * PyJit_BuildDictFromTuples(PyObject *keys_and_values);
PyObject* PyJit_DictMerge(PyObject* dict, PyObject* other);

int PyJit_StoreSubscr(PyObject* value, PyObject *container, PyObject *index);
int PyJit_StoreSubscrIndex(PyObject* value, PyObject *container, PyObject *objIndex, Py_ssize_t index);
int PyJit_StoreSubscrIndexHash(PyObject* value, PyObject *container, PyObject *objIndex, Py_ssize_t index, Py_hash_t hash);
int PyJit_StoreSubscrSlice(PyObject* value, PyObject *container, PyObject *slice);
int PyJit_StoreSubscrDict(PyObject* value, PyObject *container, PyObject *index);
int PyJit_StoreSubscrDictHash(PyObject* value, PyObject *container, PyObject *index, Py_hash_t hash);
int PyJit_StoreSubscrList(PyObject* value, PyObject *container, PyObject *index);
int PyJit_StoreSubscrListIndex(PyObject* value, PyObject *container, PyObject *objIndex, Py_ssize_t index);
int PyJit_StoreSubscrListSlice(PyObject* value, PyObject *container, PyObject *slice);

int PyJit_DeleteSubscr(PyObject *container, PyObject *index);

PyObject* PyJit_CallN(PyObject *target, PyObject* args);

int PyJit_StoreGlobal(PyObject* v, PyFrameObject* f, PyObject* name);

int PyJit_DeleteGlobal(PyFrameObject* f, PyObject* name);

PyObject* PyJit_LoadGlobal(PyFrameObject* f, PyObject* name);
PyObject* PyJit_LoadGlobalHash(PyFrameObject* f, PyObject* name, Py_hash_t name_hash);

PyObject* PyJit_GetIter(PyObject* iterable);

PyObject* PyJit_IterNext(PyObject* iter);

PyObject* PyJit_PyTuple_New(ssize_t len);

PyObject* PyJit_BuildClass(PyFrameObject *f);

PyObject* PyJit_LoadAttr(PyObject* owner, PyObject* name);
PyObject* PyJit_LoadAttrHash(PyObject* owner, PyObject* key, Py_hash_t name_hash);

int PyJit_StoreAttr(PyObject* value, PyObject* owner, PyObject* name);

int PyJit_DeleteAttr(PyObject* owner, PyObject* name);

PyObject* PyJit_LoadName(PyFrameObject* f, PyObject* name);
PyObject* PyJit_LoadNameHash(PyFrameObject* f, PyObject* name, Py_hash_t name_hash);

int PyJit_StoreName(PyObject* v, PyFrameObject* f, PyObject* name);
int PyJit_DeleteName(PyFrameObject* f, PyObject* name);

PyObject* PyJit_Is(PyObject* lhs, PyObject* rhs);
PyObject* PyJit_IsNot(PyObject* lhs, PyObject* rhs);

int PyJit_Is_Bool(PyObject* lhs, PyObject* rhs);
int PyJit_IsNot_Bool(PyObject* lhs, PyObject* rhs);

inline int trace(PyThreadState *tstate, PyFrameObject *f, int ty, PyObject *args, Py_tracefunc func, PyObject* tracearg);
inline int protected_trace(PyThreadState* tstate, PyFrameObject* f, int ty, PyObject* arg, Py_tracefunc func, PyObject* tracearg);

void PyJit_TraceLine(PyFrameObject* f, int* instr_lb, int* instr_ub, int* instr_prev);
void PyJit_TraceFrameEntry(PyFrameObject* f);
void PyJit_TraceFrameExit(PyFrameObject* f);
void PyJit_ProfileFrameEntry(PyFrameObject* f);
void PyJit_ProfileFrameExit(PyFrameObject* f);
void PyJit_TraceFrameException(PyFrameObject* f);

PyObject* Call0(PyObject *target);
PyObject* Call1(PyObject *target, PyObject* arg0);
PyObject* Call2(PyObject *target, PyObject* arg0, PyObject* arg1);
PyObject* Call3(PyObject *target, PyObject* arg0, PyObject* arg1, PyObject* arg2);
PyObject* Call4(PyObject *target, PyObject* arg0, PyObject* arg1, PyObject* arg2, PyObject* arg3);
PyObject* Call5(PyObject *target, PyObject* arg0, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4);
PyObject* Call6(PyObject *target, PyObject* arg0, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4, PyObject* arg5);
PyObject* Call7(PyObject *target, PyObject* arg0, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4, PyObject* arg5, PyObject* arg6);
PyObject* Call8(PyObject *target, PyObject* arg0, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4, PyObject* arg5, PyObject* arg6, PyObject* arg7);
PyObject* Call9(PyObject *target, PyObject* arg0, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4, PyObject* arg5, PyObject* arg6, PyObject* arg7, PyObject* arg8);
PyObject* Call10(PyObject *target, PyObject* arg0, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4, PyObject* arg5, PyObject* arg6, PyObject* arg7, PyObject* arg8, PyObject* arg9);

extern PyObject* g_emptyTuple;

void PyJit_DecRef(PyObject* value);

PyObject* PyJit_UnicodeJoinArray(PyObject* items, ssize_t count);
PyObject* PyJit_FormatObject(PyObject* item, PyObject*fmtSpec);
PyObject* PyJit_FormatValue(PyObject* item);

PyJitMethodLocation * PyJit_LoadMethod(PyObject* object, PyObject* name, PyJitMethodLocation* method_info);

PyObject* MethCall0(PyObject* self, PyJitMethodLocation* method_info);
PyObject* MethCall1(PyObject* self, PyJitMethodLocation* method_info, PyObject* arg1);
PyObject* MethCall2(PyObject* self, PyJitMethodLocation* method_info, PyObject* arg1, PyObject* arg2);
PyObject* MethCall3(PyObject* self, PyJitMethodLocation* method_info, PyObject* arg1, PyObject* arg2, PyObject* arg3);
PyObject* MethCall4(PyObject* self, PyJitMethodLocation* method_info, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4);
PyObject* MethCall5(PyObject* self, PyJitMethodLocation* method_info, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4, PyObject* arg5);
PyObject* MethCall6(PyObject* self, PyJitMethodLocation* method_info, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4, PyObject* arg5, PyObject* arg6);
PyObject* MethCall7(PyObject* self, PyJitMethodLocation* method_info, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4, PyObject* arg5, PyObject* arg6, PyObject* arg7);
PyObject* MethCall8(PyObject* self, PyJitMethodLocation* method_info, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4, PyObject* arg5, PyObject* arg6, PyObject* arg7, PyObject* arg8);
PyObject* MethCall9(PyObject* self, PyJitMethodLocation* method_info, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4, PyObject* arg5, PyObject* arg6, PyObject* arg7, PyObject* arg8, PyObject* arg9);
PyObject* MethCall10(PyObject* self, PyJitMethodLocation* method_info, PyObject* arg1, PyObject* arg2, PyObject* arg3, PyObject* arg4, PyObject* arg5, PyObject* arg6, PyObject* arg7, PyObject* arg8, PyObject* arg9, PyObject* arg10);
PyObject* MethCallN(PyObject* self, PyJitMethodLocation* method_info, PyObject* args);

int PyJit_SetupAnnotations(PyFrameObject* frame);

PyObject* PyJit_GetListItemReversed(PyObject* list, size_t index);
#endif