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

#ifndef PYJION_ABSINT_H
#define PYJION_ABSINT_H

#include <Python.h>
#include <vector>
#include <unordered_map>

#include "pyjit.h"
#include "absvalue.h"
#include "cowvector.h"
#include "ipycomp.h"
#include "block.h"
#include "stack.h"
#include "exceptionhandling.h"
#include "intstate.h"
#include "bytecodeoperation.h"

using namespace std;

struct AbstractLocalInfo;
struct BytecodeOperationSequence;

// Tracks block information for analyzing loops, exception blocks, and break opcodes.
struct AbsIntBlockInfo {
    size_t BlockStart, BlockEnd;

    AbsIntBlockInfo(size_t blockStart, size_t blockEnd) {
        BlockStart = blockStart;
        BlockEnd = blockEnd;
    }
};

enum ComprehensionType {
    COMP_NONE,
    COMP_LIST,
    COMP_DICT,
    COMP_SET
};

enum AbstractInterpreterResult {
    Success = 1,

    // Failure codes
    CompilationException = 10,  // Exception within Pyjion
    CompilationJitFailure = 11, // JIT failed

    // Incompat codes.
    IncompatibleCompilerFlags  = 100,
    IncompatibleSize = 101,
    IncompatibleOpcode_Yield = 102,
    IncompatibleOpcode_WithExcept = 103,
    IncompatibleOpcode_With = 104,
    IncompatibleOpcode_Unknown = 110,
    IncompatibleFrameGlobal = 120,
};

struct AbstactInterpreterCompileResult {
    JittedCode* compiledCode = nullptr;
    AbstractInterpreterResult result;
};

class StackImbalanceException: public std::exception {
public:
    StackImbalanceException() : std::exception() {};
    const char * what () const noexcept override
    {
        return "Stack imbalance";
    }
};

#ifdef _WIN32
class __declspec(dllexport) AbstractInterpreter {
#pragma warning (disable:4251)
#else
class AbstractInterpreter {
#endif
    // ** Results produced:
    AbstractValue* mReturnValue;
    // ** Inputs:
    PyCodeObject* mCode;
    _Py_CODEUNIT *mByteCode;
    size_t mSize;
    Local mErrorCheckLocal;
    Local mExcVarsOnStack; // Counter of the number of exception variables on the stack.
    bool mTracingEnabled;
    bool mProfilingEnabled;
    Local mTracingInstrLowerBound;
    Local mTracingInstrUpperBound;
    Local mTracingLastInstr;


    // ** Data consumed during analysis:
    // Tracks the entry point for each POP_BLOCK opcode, so we can restore our
    // stack state back after the POP_BLOCK
    unordered_map<size_t, size_t> m_blockStarts;
    // all values produced during abstract interpretation, need to be freed
    vector<AbstractValue*> m_values;
    vector<AbstractSource*> m_sources;
    vector<Local> m_raiseAndFreeLocals;
    IPythonCompiler* m_comp;
    // m_blockStack is like Python's f_blockstack which lives on the frame object, except we only maintain
    // it at compile time.  Blocks are pushed onto the stack when we enter a loop, the start of a try block,
    // or into a finally or exception handler.  Blocks are popped as we leave those protected regions.
    // When we pop a block associated with a try body we transform it into the correct block for the handler
    BlockStack m_blockStack;

    ExceptionHandlerManager m_exceptionHandler;

    // Tracks the current depth of the stack,  as well as if we have an object reference that needs to be freed.
    // True (STACK_KIND_OBJECT) if we have an object, false (STACK_KIND_VALUE) if we don't
    ValueStack m_stack;
    // Tracks the state of the stack when we perform a branch.  We copy the existing state to the map and
    // reload it when we begin processing at the stack.
    unordered_map<size_t, ValueStack> m_offsetStack;

    unordered_map<int, ssize_t> nameHashes; // local, hash

    // Set of labels used for when we need to raise an error but have values on the stack
    // that need to be freed.  We have one set of labels which fall through to each other
    // before doing the raise:
    //      free2: <decref>/<pop>
    //      free1: <decref>/<pop>
    //      raise logic.
    //  This was so we don't need to have decref/frees spread all over the code
    vector<vector<Label>> m_raiseAndFree;
    unordered_set<size_t> m_jumpsTo;
    Label m_retLabel;
    Local m_retValue;
    unordered_map<int, bool> m_assignmentState;

#pragma warning (default:4251)

public:
    AbstractInterpreter(PyCodeObject *code, IPythonCompiler* compiler);
    ~AbstractInterpreter();

    AbstactInterpreterCompileResult compile(PyObject* builtins, PyObject* globals, PyjionCodeProfile* profile, PgcStatus pgc_status);
    AbstractInterpreterResult interpret(PyObject *builtins, PyObject *globals, PyjionCodeProfile *profile, PgcStatus status);

    void setLocalType(int index, PyObject* val);
    // Returns information about the specified local variable at a specific
    // byte code index.
    AbstractLocalInfo getLocalInfo(size_t byteCodeIndex, size_t localIndex);

    // Returns information about the stack at the specific byte code index.
    InterpreterStack& getStackInfo(size_t byteCodeIndex);

    AbstractValue* getReturnInfo();

    void enableTracing();
    void disableTracing();
    void enableProfiling();
    void disableProfiling();

private:
    AbstractValue* toAbstract(PyObject* obj);

    static bool mergeStates(InterpreterState& newState, InterpreterState& mergeTo);
    bool updateStartState(InterpreterState& newState, size_t index);
    void initStartingState();
    AbstractInterpreterResult preprocess();
    AbstractSource* newSource(AbstractSource* source) {
        m_sources.push_back(source);
        return source;
    }

    AbstractSource* addLocalSource(size_t opcodeIndex, size_t localIndex);
    AbstractSource* addConstSource(size_t opcodeIndex, size_t constIndex, PyObject* value);
    AbstractSource* addGlobalSource(size_t opcodeIndex, size_t constIndex, const char * name, PyObject* value);
    AbstractSource* addBuiltinSource(size_t opcodeIndex, size_t constIndex, const char * name, PyObject* value);
    AbstractSource* addPgcSource(size_t opcodeIndex);

    void makeFunction(int oparg);
    bool canSkipLastiUpdate(size_t opcodeIndex);
    void buildTuple(size_t argCnt);
    void buildList(size_t argCnt);
    void extendListRecursively(Local list, size_t argCnt);
    void extendList(size_t argCnt);
    void buildSet(size_t argCnt);

    void buildMap(size_t argCnt);

    Label getOffsetLabel(size_t jumpTo);
    void forIter(size_t loopIndex);
    void forIter(size_t loopIndex, AbstractValueWithSources* iterator);

    // Checks to see if we have a null value as the last value on our stack
    // indicating an error, and if so, branches to our current error handler.
    void errorCheck(const char* reason = nullptr, size_t curByte = ~0);
    void intErrorCheck(const char* reason = nullptr, size_t curByte = ~0);

    vector<Label>& getRaiseAndFreeLabels(size_t blockId);
    void ensureRaiseAndFreeLocals(size_t localCount);

    void ensureLabels(vector<Label>& labels, size_t count);

    void branchRaise(const char* reason = nullptr, size_t curByte = ~0);
    void raiseOnNegativeOne(size_t curByte);

    void unwindEh(ExceptionHandler* fromHandler, ExceptionHandler* toHandler = nullptr);

    ExceptionHandler * currentHandler();

    void jumpAbsolute(size_t index, size_t from);

    void decStack(size_t size = 1);

    void incStack(size_t size = 1, StackEntryKind kind = STACK_KIND_OBJECT);

    AbstactInterpreterCompileResult compileWorker(PgcStatus status);

    void storeFast(size_t local, size_t opcodeIndex);

    void loadConst(ssize_t constIndex, size_t opcodeIndex);

    void returnValue(size_t opcodeIndex);

    void loadFast(size_t local, size_t opcodeIndex);
    void loadFastWorker(size_t local, bool checkUnbound, int curByte);

    void popExcept();

    void unaryPositive(size_t opcodeIndex);
    void unaryNegative(size_t opcodeIndex);
    void unaryNot(size_t opcodeIndex);

    void jumpIfOrPop(bool isTrue, size_t opcodeIndex, size_t offset);
    void popJumpIf(bool isTrue, size_t opcodeIndex, size_t offset);
    void jumpIfNotExact(size_t opcodeIndex, size_t jumpTo);
    void testBoolAndBranch(Local value, bool isTrue, Label target);

    void unwindHandlers();

    void emitRaise(ExceptionHandler *handler);
    void popExcVars();
    void decExcVars(size_t count);
    void incExcVars(size_t count);

};

// TODO : Fetch the range of interned integers from the interpreter state
#define IS_SMALL_INT(ival) (-5 <= (ival) && (ival) < 257)
#endif
