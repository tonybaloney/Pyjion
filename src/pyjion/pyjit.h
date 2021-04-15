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

#ifndef PYJION_PYJIT_H
#define PYJION_PYJIT_H

#define FEATURE_NO_HOST

#include <cstdint>
#include <windows.h>
#include <cwchar>
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <climits>
#include <cstring>
#include <cfloat>
#include <share.h>
#include <intrin.h>

#include <Python.h>

#include <vector>
#include <unordered_map>

#include <frameobject.h>
#include <Python.h>

using namespace std;

class PyjionCodeProfile{
    unordered_map<size_t, unordered_map<size_t, PyTypeObject *>> stackTypes;
    unordered_map<size_t, unordered_map<size_t, PyObject *>> stackValues;
public:
    void record(size_t opcodePosition, size_t stackPosition, PyObject* obj);
    PyTypeObject* getType(size_t opcodePosition, size_t stackPosition);
    PyObject* getValue(size_t opcodePosition, size_t stackPosition);
};


void capturePgcStackValue(PyjionCodeProfile* profile, PyObject* value, size_t opcodePosition, int stackPosition);
class PyjionJittedCode;

bool JitInit();
PyObject* PyJit_ExecuteAndCompileFrame(PyjionJittedCode* state, PyFrameObject *frame, PyThreadState* tstate, PyjionCodeProfile* profile);
static inline PyObject* PyJit_ExecuteJittedFrame(void* state, PyFrameObject*frame, PyThreadState* tstate, PyjionCodeProfile* profile);
PyObject* PyJit_EvalFrame(PyThreadState *, PyFrameObject *, int);
PyjionJittedCode* PyJit_EnsureExtra(PyObject* codeObject);

typedef PyObject* (*Py_EvalFunc)(PyjionJittedCode*, struct _frame*, PyThreadState*, PyjionCodeProfile*);

typedef struct PyjionSettings {
    bool tracing = false;
    bool profiling = false;
    bool pgc = true; // Profile-guided-compilation
    unsigned short optimizationLevel = 1;
    int recursionLimit = DEFAULT_RECURSION_LIMIT;
    size_t codeObjectSizeLimit = DEFAULT_CODEOBJECT_SIZE_LIMIT;

    // Optimizations
    bool opt_inlineIs = OPTIMIZE_IS; // OPT-1
    bool opt_inlineDecref = OPTIMIZE_DECREF; // OPT-2
    bool opt_internRichCompare = OPTIMIZE_INTERN_COMPARE; // OPT-3
	bool opt_nativeLocals = OPTIMIZE_NATIVE_LOCALS; // OPT-4
	bool opt_inlineFramePushPop = OPTIMIZE_PUSH_FRAME; // OPT-5
    bool opt_knownStoreSubscr = OPTIMIZE_KNOWN_STORE_SUBSCR; // OPT-6
    bool opt_knownBinarySubscr = OPTIMIZE_KNOWN_BINARY_SUBSCR; // OPT-7
    bool opt_tripleBinaryFunctions = OPTIMIZE_BINARY_FUNCTIONS; // OPT-8
    bool opt_inlineIterators = OPTIMIZE_ITERATORS; // OPT-9
    bool opt_hashedNames = OPTIMIZE_HASHED_NAMES; // OPT-10
    bool opt_subscrSlice = OPTIMIZE_BINARY_SLICE; // OPT-11
    bool opt_builtinMethods = OPTIMIZE_BUILTIN_METHODS; // OPT-12
    bool opt_typeSlotLookups = OPTIMIZE_TYPESLOT_LOOKUPS; // OPT-13
    bool opt_functionCalls = OPTIMIZE_FUNCTION_CALLS; // OPT-14
    bool opt_loadAttr = OPTIMIZE_LOAD_ATTR; // OPT-15
} PyjionSettings;

static PY_UINT64_T HOT_CODE = 0;

extern PyjionSettings g_pyjionSettings;

#define OPT_ENABLED(opt) g_pyjionSettings.opt_ ## opt

void PyjionJitFree(void* obj);

int Pyjit_CheckRecursiveCall(PyThreadState *tstate, const char *where);
static int Pyjit_EnterRecursiveCall(const char *where);
static void Pyjit_LeaveRecursiveCall();


/* Jitted code object.  This object is returned from the JIT implementation.  The JIT can allocate
a jitted code object and fill in the state for which is necessary for it to perform an evaluation. */
enum PgcStatus {
    Uncompiled = 0,
    CompiledWithProbes = 1,
    Optimized = 2
};

PgcStatus nextPgcStatus(PgcStatus status);

class PyjionJittedCode {
public:
	PY_UINT64_T j_run_count;
	bool j_failed;
	short j_compile_result;
	Py_EvalFunc j_addr;
	PY_UINT64_T j_specialization_threshold;
	PyObject* j_code;
	PyjionCodeProfile* j_profile;
    unsigned char* j_il;
    unsigned int j_ilLen;
    unsigned long j_nativeSize;
    PgcStatus j_pgc_status;

	explicit PyjionJittedCode(PyObject* code) {
		j_code = code;
		j_run_count = 0;
		j_failed = false;
		j_addr = nullptr;
		j_specialization_threshold = HOT_CODE;
		j_il = nullptr;
		j_ilLen = 0;
		j_nativeSize = 0;
		j_profile = new PyjionCodeProfile();
		j_pgc_status = Uncompiled;
		Py_INCREF(code);
	}

	~PyjionJittedCode();
};

void setOptimizationLevel(unsigned short level);
#endif