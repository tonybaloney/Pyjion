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
#include "instructions.h"

using namespace std;

struct AbstractLocalInfo;

// Tracks block information for analyzing loops, exception blocks, and break opcodes.
struct AbsIntBlockInfo {
    py_opindex BlockStart, BlockEnd;

    AbsIntBlockInfo(py_opindex blockStart, py_opindex blockEnd) {
        BlockStart = blockStart;
        BlockEnd = blockEnd;
    }
};

// The abstract interpreter implementation.  The abstract interpreter performs
// static analysis of the Python byte code to determine what types are known.
// Ultimately this information will feedback into code generation allowing
// more efficient code to be produced.
//
// The abstract interpreter ultimately produces a set of states for each opcode
// before it has been executed.  It also produces an abstract value for the type
// that the function returns.
//
// The abstract interpreter walks the byte code updating the stack of the stack
// and locals based upon the opcode being performed and the existing state of the
// stack.  When it encounters a branch it will merge the current state in with the
// state for where we're branching to.  If the merge results in a new starting state
// that we haven't analyzed it will then queue the target opcode as the next starting
// point to be analyzed.
//
// If the branch is unconditional, or definitively taken based upon analysis, then
// we'll go onto the next starting opcode to be analyzed.
//
// Once we've processed all of the blocks of code in this manner the analysis
// is complete.

// Tracks the state of a local variable at each location in the function.
// Each local has a known type associated with it as well as whether or not
// the value is potentially undefined.  When a variable is definitely assigned
// IsMaybeUndefined is false.
//
// Initially all locals start out as being marked as IsMaybeUndefined and a special
// typeof Undefined.  The special type is really just for convenience to avoid
// having null types.  Merging with the undefined type will produce the other type.
// Assigning to a variable will cause the undefined marker to be removed, and the
// new type to be specified.
//
// When we merge locals if the undefined flag is specified from either side we will
// propagate it to the new state.  This could result in:
//
// State 1: Type != Undefined, IsMaybeUndefined = false
//      The value is definitely assigned and we have valid type information
//
// State 2: Type != Undefined, IsMaybeUndefined = true
//      The value is assigned in one code path, but not in another.
//
// State 3: Type == Undefined, IsMaybeUndefined = true
//      The value is definitely unassigned.
//
// State 4: Type == Undefined, IsMaybeUndefined = false
//      This should never happen as it means the Undefined
//      type has leaked out in an odd way
struct AbstractLocalInfo {
    AbstractLocalInfo() = default;

    AbstractValueWithSources ValueInfo;
    bool IsMaybeUndefined;

    AbstractLocalInfo(AbstractValueWithSources valueInfo, bool isUndefined = false) : ValueInfo(valueInfo) {
        IsMaybeUndefined = true;
        assert(valueInfo.Value != nullptr);
        assert(!(valueInfo.Value == &Undefined && !isUndefined));
        IsMaybeUndefined = isUndefined;
    }

    AbstractLocalInfo mergeWith(AbstractLocalInfo other) const {
        return {
            ValueInfo.mergeWith(other.ValueInfo),
            IsMaybeUndefined || other.IsMaybeUndefined
            };
    }

    bool operator== (AbstractLocalInfo other) {
        return other.ValueInfo == ValueInfo &&
            other.IsMaybeUndefined == IsMaybeUndefined;
    }
    bool operator!= (AbstractLocalInfo other) {
        return other.ValueInfo != ValueInfo ||
            other.IsMaybeUndefined != IsMaybeUndefined;
    }
};

// Represents the state of the program at each opcode.  Captures the state of both
// the Python stack and the local variables.  We store the state for each opcode in
// AbstractInterpreter.m_startStates which represents the state before the indexed
// opcode has been executed.
//
// The stack is a unique vector for each interpreter state.  There's currently no
// attempts at sharing because most instructions will alter the value stack.
//
// The locals are shared between InterpreterState's using a shared_ptr because the
// values of locals won't change between most opcodes (via CowVector).  When updating
// a local we first check if the locals are currently shared, and if not simply update
// them in place.  If they are shared then we will issue a copy.
class InterpreterState {
public:
    InterpreterStack mStack;
    CowVector<AbstractLocalInfo> mLocals;
    bool requiresPgcProbe = false;
    short pgcProbeSize = 0;

    InterpreterState() = default;

    explicit InterpreterState(size_t numLocals) {
        mLocals = CowVector<AbstractLocalInfo>(numLocals);
    }

    AbstractLocalInfo getLocal(size_t index) {
        return mLocals[index];
    }

    size_t localCount() {
        return mLocals.size();
    }

    void replaceLocal(size_t index, AbstractLocalInfo value) {
        mLocals.replace(index, value);
    }

    AbstractValueWithSources pop(py_opindex idx, size_t position) {
        if (mStack.empty())
            throw StackUnderflowException();
        auto res = mStack.back();
        mStack.pop_back();
        if (res.hasSource()){
            res.Sources->addConsumer(idx, position);
        }
        return res;
    }

    AbstractValueWithSources fromPgc(size_t stackPosition, PyTypeObject* pyTypeObject, PyObject* pyObject) {
        if (mStack.empty())
            throw StackUnderflowException();
        auto existing = mStack[mStack.size() - 1 - stackPosition];
        if (existing.hasSource() && existing.Sources->hasConstValue())
            return existing;
        if (pyTypeObject == nullptr)
            return existing;
        else {
            return AbstractValueWithSources(
                    new PgcValue(pyTypeObject, pyObject),
                    existing.Sources
            );
        }
    }

    void push(AbstractValueWithSources value) {
        mStack.push_back(value);
    }

    size_t stackSize() const {
        return mStack.size();
    }

    AbstractValueWithSources& operator[](const size_t index) {
        return mStack[index];
    }

    void push_n(const size_t n, const AbstractValueWithSources& value){
        mStack[mStack.size() - 1 - n] = value;
    }
};

enum ComprehensionType {
    COMP_NONE,
    COMP_LIST,
    COMP_DICT,
    COMP_SET
};

enum AbstractInterpreterResult {
    NoResult = 0,
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
    AbstractInterpreterResult result = NoResult;
    PyObject* instructionGraph = nullptr;
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
    // Tracks the interpreter state before each opcode
    unordered_map<py_opindex, InterpreterState> mStartStates;
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
    unordered_map<py_opindex, py_opindex> m_blockStarts;
    unordered_map<py_opindex, AbstractSource*> m_opcodeSources;
    // all values produced during abstract interpretation, need to be freed
    vector<AbstractValue*> m_values;
    vector<AbstractSource*> m_sources;
    vector<Local> m_raiseAndFreeLocals;
    unordered_map<py_oparg, Local> m_fastNativeLocals;
    unordered_map<py_oparg, StackEntryKind> m_fastNativeLocalKinds;
    IPythonCompiler* m_comp;
    // m_blockStack is like Python's f_blockstack which lives on the frame object, except we only maintain
    // it at compile time.  Blocks are pushed onto the stack when we enter a loop, the start of a try block,
    // or into a finally or exception handler.  Blocks are popped as we leave those protected regions.
    // When we pop a block associated with a try body we transform it into the correct block for the handler
    BlockStack m_blockStack;

    ExceptionHandlerManager m_exceptionHandler;
    // Labels that map from a Python byte code offset to an ilgen label.  This allows us to branch to any
    // byte code offset.
    unordered_map<py_opindex, Label> m_offsetLabels;
    // Tracks the current depth of the stack,  as well as if we have an object reference that needs to be freed.
    // True (STACK_KIND_OBJECT) if we have an object, false (STACK_KIND_VALUE) if we don't
    ValueStack m_stack;
    // Tracks the state of the stack when we perform a branch.  We copy the existing state to the map and
    // reload it when we begin processing at the stack.
    unordered_map<py_opindex, ValueStack> m_offsetStack;

    unordered_map<Py_ssize_t, Py_ssize_t> nameHashes;

    // Set of labels used for when we need to raise an error but have values on the stack
    // that need to be freed.  We have one set of labels which fall through to each other
    // before doing the raise:
    //      free2: <decref>/<pop>
    //      free1: <decref>/<pop>
    //      raise logic.
    //  This was so we don't need to have decref/frees spread all over the code
    vector<vector<Label>> m_raiseAndFree;
    unordered_set<py_opindex> m_jumpsTo;
    Label m_retLabel;
    Local m_retValue;
    unordered_map<py_opindex, bool> m_assignmentState;
    unordered_map<py_opindex, bool> m_unboxableProducers;
    unordered_map<py_opindex, Label> m_yieldOffsets;

#pragma warning (default:4251)

public:
    AbstractInterpreter(PyCodeObject *code, IPythonCompiler* compiler);
    ~AbstractInterpreter();

    AbstactInterpreterCompileResult compile(PyObject* builtins, PyObject* globals, PyjionCodeProfile* profile, PgcStatus pgc_status);
    AbstractInterpreterResult interpret(PyObject *builtins, PyObject *globals, PyjionCodeProfile *profile, PgcStatus status);

    void setLocalType(size_t index, PyObject* val);
    // Returns information about the specified local variable at a specific
    // byte code index.
    AbstractLocalInfo getLocalInfo(py_opindex byteCodeIndex, size_t localIndex);

    // Returns information about the stack at the specific byte code index.
    InterpreterStack& getStackInfo(py_opindex byteCodeIndex);

    AbstractValue* getReturnInfo();
    bool pgcProbeRequired(py_opindex byteCodeIndex, PgcStatus status);
    short pgcProbeSize(py_opindex byteCodeIndex);
    void enableTracing();
    void disableTracing();
    void enableProfiling();
    void disableProfiling();
    InstructionGraph* buildInstructionGraph();
private:
    AbstractValue* toAbstract(PyObject* obj);

    static bool mergeStates(InterpreterState& newState, InterpreterState& mergeTo);
    bool updateStartState(InterpreterState& newState, py_opindex index);
    void initStartingState();
    AbstractInterpreterResult preprocess();
    AbstractSource* newSource(AbstractSource* source) {
        m_sources.push_back(source);
        return source;
    }

    AbstractSource* addLocalSource(py_opindex opcodeIndex, py_oparg localIndex);
    AbstractSource* addConstSource(py_opindex opcodeIndex, py_oparg constIndex, PyObject* value);
    AbstractSource* addGlobalSource(py_opindex opcodeIndex, py_oparg constIndex, const char * name, PyObject* value);
    AbstractSource* addBuiltinSource(py_opindex opcodeIndex, py_oparg constIndex, const char * name, PyObject* value);

    void makeFunction(py_oparg oparg);
    bool canSkipLastiUpdate(py_opindex opcodeIndex);
    void buildTuple(py_oparg argCnt);
    void buildList(py_oparg argCnt);
    void extendListRecursively(Local list, py_oparg argCnt);
    void extendList(py_oparg argCnt);
    void buildSet(py_oparg argCnt);
    void buildMap(py_oparg argCnt);
    void emitPgcProbes(py_opindex pos, size_t size);

    Label getOffsetLabel(py_opindex jumpTo);
    void forIter(py_opindex loopIndex);
    void forIter(py_opindex loopIndex, AbstractValueWithSources* iterator);

    void yieldValue(py_opindex idx, size_t stackSize, InstructionGraph* graph);

    // Checks to see if we have a null value as the last value on our stack
    // indicating an error, and if so, branches to our current error handler.
    void errorCheck(const char* reason = nullptr, py_opindex curByte = 0);
    void invalidFloatErrorCheck(const char* reason = nullptr, py_opindex curByte = 0, py_opcode opcode = 0);
    void invalidIntErrorCheck(const char* reason = nullptr, py_opindex curByte = 0, py_opcode opcode = 0);
    void intErrorCheck(const char* reason = nullptr, py_opindex curByte = 0);

    vector<Label>& getRaiseAndFreeLabels(size_t blockId);
    void ensureRaiseAndFreeLocals(size_t localCount);

    void ensureLabels(vector<Label>& labels, size_t count);

    void branchRaise(const char* reason = nullptr, py_opindex curByte = 0, bool force=false);
    void raiseOnNegativeOne(py_opindex curByte);

    void unwindEh(ExceptionHandler* fromHandler, ExceptionHandler* toHandler = nullptr);

    ExceptionHandler * currentHandler();

    void markOffsetLabel(py_opindex index);
    void jumpAbsolute(py_opindex index, py_opindex from);

    void decStack(size_t size = 1);
    void incStack(size_t size = 1, StackEntryKind kind = STACK_KIND_OBJECT);
    void incStack(size_t size, LocalKind kind);

    AbstactInterpreterCompileResult compileWorker(PgcStatus status, InstructionGraph* graph);

    void loadConst(py_oparg constIndex, py_opindex opcodeIndex);
    void loadUnboxedConst(py_oparg constIndex, py_opindex opcodeIndex);

    void returnValue(py_opindex opcodeIndex);

    void storeFastUnboxed(py_oparg local);
    void loadFast(py_oparg local, py_opindex opcodeIndex);
    void loadFastUnboxed(py_oparg local, py_opindex opcodeIndex);
    void loadFastWorker(py_oparg local, bool checkUnbound, py_opindex curByte);

    void popExcept();

    void jumpIfOrPop(bool isTrue, py_opindex opcodeIndex, py_oparg offset);
    void popJumpIf(bool isTrue, py_opindex opcodeIndex, py_oparg offset);
    void unboxedPopJumpIf(bool isTrue, py_opindex opcodeIndex, py_oparg offset);
    void jumpIfNotExact(py_opindex opcodeIndex, py_oparg jumpTo);
    void testBoolAndBranch(Local value, bool isTrue, Label target);

    void unwindHandlers();

    void emitRaise(ExceptionHandler *handler);
    void popExcVars();
    void decExcVars(size_t count);
    void incExcVars(size_t count);
    void updateIntermediateSources();
    void escapeEdges(const vector<Edge>& edges, py_opindex curByte);
    void dumpEscapedLocalsToFrame(const unordered_map<py_oparg, AbstractValueKind>& locals, py_opindex at);
    void loadEscapedLocalsFromFrame(const unordered_map<py_oparg, AbstractValueKind>& locals, py_opindex at);
    void yieldJumps();
};
bool canReturnInfinity(py_opcode opcode);

// TODO : Fetch the range of interned integers from the interpreter state
#define IS_SMALL_INT(ival) (-5 <= (ival) && (ival) < 257)
#endif
