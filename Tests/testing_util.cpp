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

/**
  Testing utility code.
*/

#include <catch2/catch.hpp>
#include "testing_util.h"
#include <Python.h>
#include <util.h>


PyCodeObject* CompileCode(const char* code) {
    auto globals = PyObject_ptr(PyDict_New());

    auto builtins = PyEval_GetBuiltins();
    PyDict_SetItemString(globals.get(), "__builtins__", builtins);

    auto locals = PyObject_ptr(PyDict_New());
    PyRun_String(code, Py_file_input, globals.get(), locals.get());
    if (PyErr_Occurred()) {
        PyErr_Print();
        PyErr_Clear();
        FAIL("error occurred during Python compilation");
        return nullptr;
    }
    auto func = PyObject_ptr(PyObject_GetItem(locals.get(), PyUnicode_FromString("f")));
    auto codeObj = (PyCodeObject*) PyObject_GetAttrString(func.get(), "__code__");
    return codeObj;
}

PyObject* CompileFunction(const char* code) {
    auto globals = PyObject_ptr(PyDict_New());

    auto builtins = PyEval_GetBuiltins();
    PyDict_SetItemString(globals.get(), "__builtins__", builtins);

    auto locals = PyObject_ptr(PyDict_New());
    PyRun_String(code, Py_file_input, globals.get(), locals.get());
    if (PyErr_Occurred()) {
        PyErr_Print();
        PyErr_Clear();
        FAIL("error occurred during Python compilation");
        return nullptr;
    }
    auto func = PyObject_ptr(PyObject_GetItem(locals.get(), PyUnicode_FromString("f")));
    auto codeObj = (PyCodeObject*) PyObject_GetAttrString(func.get(), "__code__");
    return PyFunction_New(reinterpret_cast<PyObject*>(codeObj), globals.get());
}

PyCodeObject* CompileCode(const char* code, vector<const char*> locals, vector<const char*> globals) {
    auto globals_dict = PyObject_ptr(PyDict_New());

    auto builtins = PyEval_GetBuiltins();
    PyDict_SetItemString(globals_dict.get(), "__builtins__", builtins);
    for (auto& local : globals)
        PyDict_SetItemString(globals_dict.get(), local, Py_None);

    auto locals_dict = PyObject_ptr(PyDict_New());
    for (auto& local : locals)
        PyDict_SetItemString(locals_dict.get(), local, Py_None);

    PyRun_String(code, Py_file_input, globals_dict.get(), locals_dict.get());
    if (PyErr_Occurred()) {
        PyErr_Print();
        PyErr_Clear();
        FAIL("error occurred during Python compilation");
        return nullptr;
    }
    auto func = PyObject_ptr(PyObject_GetItem(locals_dict.get(), PyUnicode_FromString("f")));
    auto codeObj = (PyCodeObject*) PyObject_GetAttrString(func.get(), "__code__");

    return codeObj;
}

void VerifyOldTest(AITestCase testCase) {
    PyErr_Clear();
    auto codeObj = CompileCode(testCase.m_code);

    AbstractInterpreter interpreter(codeObj);
    auto builtins = PyEval_GetBuiltins();
    auto globals_dict = PyObject_ptr(PyDict_New());
    auto profile = new PyjionCodeProfile();
    if (interpreter.interpret(builtins, globals_dict.get(), profile, Uncompiled) != Success) {
        FAIL("Failed to interpret code");
    }
    delete profile;
    testCase.verify(interpreter);

    Py_DECREF(codeObj);
}

/* Verify the inferred type stored in the locals array before a specified bytecode executes. */
VariableVerifier::VariableVerifier(size_t byteCodeIndex, size_t localIndex, AbstractValueKind kind, bool undefined) {
    m_byteCodeIndex = byteCodeIndex;
    m_localIndex = localIndex;
    m_undefined = undefined;
    m_kind = kind;
};

void VariableVerifier::verify(AbstractInterpreter& interpreter) {
    auto local = interpreter.getLocalInfo(m_byteCodeIndex, m_localIndex);
    CHECK(local.IsMaybeUndefined == m_undefined);
    CHECK(local.ValueInfo.Value->kind() == m_kind);
};
