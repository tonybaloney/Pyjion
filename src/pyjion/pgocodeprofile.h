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

#ifndef PYJION_PGOCODEPROFILE
#define PYJION_PGOCODEPROFILE

#include <Python.h>
#include <unordered_map>

using namespace std;

/* Jitted code object.  This object is returned from the JIT implementation.  The JIT can allocate
a jitted code object and fill in the state for which is necessary for it to perform an evaluation. */
enum PgcStatus {
    Uncompiled = 0,
    CompiledWithProbes = 1,
    Optimized = 2
};

PgcStatus nextPgcStatus(PgcStatus status);

class PyjionCodeProfile{
    unordered_map<size_t, unordered_map<size_t, PyTypeObject *>> stackTypes;
    unordered_map<size_t, unordered_map<size_t, PyObject *>> stackValues;
    unordered_map<size_t, size_t> allocations;
public:
    void record(size_t opcodePosition, size_t stackPosition, PyObject* obj);
    PyTypeObject* getType(size_t opcodePosition, size_t stackPosition);
    PyObject* getValue(size_t opcodePosition, size_t stackPosition);
    PgcStatus status = Uncompiled;
    void captureMalloc(size_t size);
    ~PyjionCodeProfile();
};

void capturePgcStackValue(PyjionCodeProfile* profile, PyObject* value, size_t opcodePosition, int stackPosition);

#endif //PYJION_PGOCODEPROFILE
