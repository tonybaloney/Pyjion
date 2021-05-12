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
#include <opcode.h>
#include <object.h>
#include <deque>
#include <unordered_map>
#include <algorithm>
#include "bytecodeoperation.h"
#include "absint.h"
#include "pyjit.h"
#include "pyjitmath.h"

#define SIZEOF_CODEUNIT sizeof(_Py_CODEUNIT)
#define GET_OPARG(index)  _Py_OPARG(mByteCode[(index)/SIZEOF_CODEUNIT])
#define GET_OPCODE(index) _Py_OPCODE(mByteCode[(index)/SIZEOF_CODEUNIT])

#define PGC_READY() g_pyjionSettings.pgc && profile != nullptr

#define PGC_PROBE(count) pgcRequired = true; pgcSize = count;

#define PGC_UPDATE_STACK(count) \
    if (pgc_status == PgcStatus::CompiledWithProbes) {                      \
        for (int pos = 0; pos < (count) ; pos++) \
            op.state.push_n(pos, op.state.fromPgc(pos, profile->getType(op.index, pos), profile->getValue(op.index, pos), op.addPgcSource())); \
    }

AbstractInterpreter::AbstractInterpreter(PyCodeObject *code, IPythonCompiler* comp) : mReturnValue(&Undefined), mCode(code), m_comp(comp) {
    mByteCode = (_Py_CODEUNIT *)PyBytes_AS_STRING(code->co_code);
    mSize = PyBytes_Size(code->co_code);
    mTracingEnabled = false;
    mProfilingEnabled = false;

    if (comp != nullptr) {
        m_retLabel = comp->emit_define_label();
        m_retValue = comp->emit_define_local();
        mErrorCheckLocal = comp->emit_define_local();
    }
    initStartingState();
}

AbstractInterpreter::~AbstractInterpreter() {
   // clean up any dynamically allocated objects...
   // TODO : Deconstruct via bytecodeoperationsequence
}

AbstractInterpreterResult AbstractInterpreter::preprocess() {
    if (mCode->co_flags & (CO_COROUTINE | CO_GENERATOR | CO_ITERABLE_COROUTINE | CO_ASYNC_GENERATOR)) {
        // Don't compile co-routines or generators.  We can't rely on
        // detecting yields because they could be optimized out.
        return IncompatibleCompilerFlags;
    }

    for (int i = 0; i < mCode->co_argcount; i++) {
        // all parameters are initially definitely assigned
        m_assignmentState[i] = true;
    }
    if (mSize >= g_pyjionSettings.codeObjectSizeLimit){
        return IncompatibleSize;
    }

    int oparg;
    vector<bool> ehKind;
    vector<AbsIntBlockInfo> blockStarts;
    for (size_t curByte = 0; curByte < mSize; curByte += SIZEOF_CODEUNIT) {
        auto opcodeIndex = curByte;
        auto byte = GET_OPCODE(curByte);
        oparg = GET_OPARG(curByte);
    processOpCode:
        while (!blockStarts.empty() &&
            opcodeIndex >= blockStarts[blockStarts.size() - 1].BlockEnd) {
            auto blockStart = blockStarts.back();
            blockStarts.pop_back();
            m_blockStarts[opcodeIndex] = blockStart.BlockStart;
        }

        switch (byte) { // NOLINT(hicpp-multiway-paths-covered)
            case POP_EXCEPT:
            case POP_BLOCK:
            {
                if (!blockStarts.empty()) {
                    auto blockStart = blockStarts.back();
                    blockStarts.pop_back();
                    m_blockStarts[opcodeIndex] = blockStart.BlockStart;
                }
                break;
            }
            case EXTENDED_ARG:
            {
                curByte += SIZEOF_CODEUNIT;
                oparg = (oparg << 8) | GET_OPARG(curByte);
                byte = GET_OPCODE(curByte);
                goto processOpCode;
            }
            case YIELD_FROM:
            case YIELD_VALUE:
                return IncompatibleOpcode_Yield;

            case DELETE_FAST:
                if (oparg < mCode->co_argcount) {
                    // this local is deleted, so we need to check for assignment
                    m_assignmentState[oparg] = false;
                }
                break;
            case SETUP_WITH:
            case SETUP_ASYNC_WITH:
            case SETUP_FINALLY:
            case FOR_ITER:
                blockStarts.emplace_back(opcodeIndex, oparg + curByte + SIZEOF_CODEUNIT);
                ehKind.push_back(true);
                break;
            case LOAD_GLOBAL:
            {
                auto name = PyUnicode_AsUTF8(PyTuple_GetItem(mCode->co_names, oparg));
                if (!strcmp(name, "vars") || 
                    !strcmp(name, "dir") || 
                    !strcmp(name, "locals") || 
                    !strcmp(name, "eval") ||
                    !strcmp(name, "exec")) {
                    // TODO: Support for frame globals
                    // In the future we might be able to do better, e.g. keep locals in fast locals,
                    // but for now this is a known limitation that if you load vars/dir we won't
                    // optimize your code, and if you alias them you won't get the correct behavior.
                    // Longer term we should patch vars/dir/_getframe and be able to provide the
                    // correct values from generated code.
#ifdef DEBUG
                    printf("Skipping function because it contains frame globals.");
#endif
                    return IncompatibleFrameGlobal;
                }
            }
            break;
            case JUMP_FORWARD:
                m_jumpsTo.insert(oparg + curByte + SIZEOF_CODEUNIT);
                break;
            case JUMP_ABSOLUTE:
            case JUMP_IF_FALSE_OR_POP:
            case JUMP_IF_TRUE_OR_POP:
            case JUMP_IF_NOT_EXC_MATCH:
            case POP_JUMP_IF_TRUE:
            case POP_JUMP_IF_FALSE:
                m_jumpsTo.insert(oparg);
                break;
        }
    }
    if (OPT_ENABLED(hashedNames)){
        for (int i = 0; i < PyTuple_Size(mCode->co_names); i++) {
            nameHashes[i] = PyObject_Hash(PyTuple_GetItem(mCode->co_names, i));
        }
    }
    return Success;
}

void AbstractInterpreter::setLocalType(size_t index, PyObject* val) {
    if (val != nullptr) {
        auto localInfo = AbstractLocalInfo(new ArgumentValue(Py_TYPE(val), val));
        localInfo.ValueInfo.Sources = newSource(new LocalSource());
        mByteCodeSequence[0].state.replaceLocal(index, localInfo);
    }
}

void AbstractInterpreter::initStartingState() {
    InterpreterState lastState = InterpreterState(mCode->co_nlocals);

    int localIndex = 0;
    for (int i = 0; i < mCode->co_argcount + mCode->co_kwonlyargcount; i++) {
        // all parameters are initially definitely assigned
        lastState.replaceLocal(localIndex++, AbstractLocalInfo(&Any));
    }

    if (mCode->co_flags & CO_VARARGS) {
        lastState.replaceLocal(localIndex++, AbstractLocalInfo(&Tuple));
    }
    if (mCode->co_flags & CO_VARKEYWORDS) {
        lastState.replaceLocal(localIndex++, AbstractLocalInfo(&Dict));
    }

    for (; localIndex < mCode->co_nlocals; localIndex++) {
        // All locals are initially undefined
        lastState.replaceLocal(localIndex, AbstractLocalInfo(&Undefined, true));
    }

    for (; localIndex < mCode->co_nlocals; localIndex++) {
        // All locals are initially undefined
        lastState.replaceLocal(localIndex, AbstractLocalInfo(&Undefined, true));
    }

    updateStartState(lastState, 0);
}

AbstractInterpreterResult
AbstractInterpreter::interpret(PyObject *builtins, PyObject *globals, PyjionCodeProfile *profile, PgcStatus pgc_status) {
    auto preprocessResult = preprocess();
    if (preprocessResult != Success) {
        return preprocessResult;
    }

    // walk all the blocks in the code one by one, analyzing them, and enqueing any
    // new blocks that we encounter from branches.
    deque<size_t> queue;
    queue.push_back(0);
    vector<const char*> utf8_names ;
    for (size_t i = 0; i < PyTuple_Size(mCode->co_names); i++)
        utf8_names.push_back(PyUnicode_AsUTF8(PyTuple_GetItem(mCode->co_names, i)));

    do {
        auto cur = queue.front();
        queue.pop_front();
        for (auto & op : mByteCodeSequence) {
            bool pgcRequired = false;
            short pgcSize = 0;
        processOpCode:

            size_t curStackLen = op.state.stackSize();
            int jump = 0;
            bool skipEffect = false;
            switch (op.opcode) {
                case EXTENDED_ARG: {
                    auto curByte = op.index + SIZEOF_CODEUNIT;
                    op.oparg = (op.oparg << 8) | GET_OPARG(curByte);
                    op.opcode = GET_OPCODE(curByte);
                    updateStartState(op.state, curByte);
                    goto processOpCode;
                }
                case NOP:
                    break;
                case ROT_TWO: {
                    auto top = op.state.pop();
                    auto second = op.state.pop();

                    op.source = AbstractSource::combine(top.Sources, second.Sources);
                    op.state.push(top);
                    op.state.push(second);
                    break;
                }
                case ROT_THREE: {
                    auto top = op.state.pop();
                    auto second = op.state.pop();
                    auto third = op.state.pop();

                    op.source = AbstractSource::combine(
                            top.Sources,
                            AbstractSource::combine(second.Sources, third.Sources));
                    op.state.push(top);
                    op.state.push(third);
                    op.state.push(second);
                    break;
                }
                case ROT_FOUR: {
                    auto top = op.state.pop();
                    auto second = op.state.pop();
                    auto third = op.state.pop();
                    auto fourth = op.state.pop();

                    op.source = AbstractSource::combine(
                            top.Sources,
                            AbstractSource::combine(second.Sources,
                                                    AbstractSource::combine(third.Sources, fourth.Sources)));

                    op.state.push(top);
                    op.state.push(fourth);
                    op.state.push(third);
                    op.state.push(second);
                    break;
                }
                case POP_TOP:
                    op.state.pop();
                    break;
                case DUP_TOP:
                    op.state.push(op.state[op.state.stackSize() - 1]);
                    break;
                case DUP_TOP_TWO: {
                    auto top = op.state[op.state.stackSize() - 1];
                    auto second = op.state[op.state.stackSize() - 2];
                    op.state.push(second);
                    op.state.push(top);
                    break;
                }
                case RERAISE: {
                    op.state.pop();
                    op.state.pop();
                    op.state.pop();
                    break;
                }
                case LOAD_CONST: {
                    PyObject* item = PyTuple_GetItem(mCode->co_consts, op.oparg);
                    auto constSource = op.addConstSource(op.oparg, item);
                    auto value = AbstractValueWithSources(
                            toAbstract(item),
                            constSource
                    );
                    op.state.push(value);
                    break;
                }
                case LOAD_FAST: {
                    auto localSource = op.addLocalSource(op.oparg);
                    auto local = op.state.getLocal(op.oparg);

                    local.ValueInfo.Sources = AbstractSource::combine(localSource, local.ValueInfo.Sources);

                    op.state.push(local.ValueInfo);
                    break;
                }
                case STORE_FAST: {
                    auto valueInfo = op.state.pop();
                    op.source = valueInfo.Sources;
                    op.state.replaceLocal(op.oparg, AbstractLocalInfo(valueInfo, valueInfo.Value == &Undefined));
                }
                    break;
                case DELETE_FAST:
                    // We need to box any previous stores so we can delete them...  Otherwise
                    // we won't know if we should raise an unbound local error
                    op.state.replaceLocal(op.oparg, AbstractLocalInfo(&Undefined, true));
                    break;
                case BINARY_SUBSCR:
                case BINARY_TRUE_DIVIDE:
                case BINARY_FLOOR_DIVIDE:
                case BINARY_POWER:
                case BINARY_MODULO:
                case BINARY_MATRIX_MULTIPLY:
                case BINARY_LSHIFT:
                case BINARY_RSHIFT:
                case BINARY_AND:
                case BINARY_XOR:
                case BINARY_OR:
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
                case INPLACE_LSHIFT:
                case INPLACE_RSHIFT:
                case INPLACE_AND:
                case INPLACE_XOR:
                case INPLACE_OR: {
                    AbstractValueWithSources two;
                    AbstractValueWithSources one;
                    if (PGC_READY()){
                        PGC_PROBE(2);
                        PGC_UPDATE_STACK(2);
                    }
                    two = op.state.pop();
                    one = op.state.pop();

                    auto out = one.Value->binary(one.Sources, op.opcode, two);
                    op.state.push(out);
                }
                break;
                case POP_JUMP_IF_FALSE: {
                    auto value = op.state.pop();

                    // merge our current state into the branched to location...
                    if (updateStartState(op.state, op)) {
                        queue.push_back(op.oparg);
                    }

                    if (value.Value->isAlwaysFalse()) {
                        // We're always jumping, we don't need to process the following opcodes...
                        goto next;
                    }

                    // we'll continue processing after the jump with our new state...
                    break;
                }
                case POP_JUMP_IF_TRUE: {
                    auto value = op.state.pop();

                    // merge our current state into the branched to location...
                    if (updateStartState(op.state, op.oparg)) {
                        queue.push_back(op.oparg);
                    }

                    if (value.Value->isAlwaysTrue()) {
                        // We're always jumping, we don't need to process the following opcodes...
                        goto next;
                    }

                    // we'll continue processing after the jump with our new state...
                    break;
                }
                case JUMP_IF_TRUE_OR_POP: {
                    auto curState = op.state;
                    if (updateStartState(op.state, op.oparg)) {
                        queue.push_back(op.oparg);
                    }
                    auto value = op.state.pop();
                    if (value.Value->isAlwaysTrue()) {
                        // we always jump, no need to analyze the following instructions...
                        goto next;
                    }
                }
                    break;
                case JUMP_IF_FALSE_OR_POP: {
                    auto curState = op.state;
                    if (updateStartState(op.state, op.oparg)) {
                        queue.push_back(op.oparg);
                    }
                    auto value = op.state.pop();
                    if (value.Value->isAlwaysFalse()) {
                        // we always jump, no need to analyze the following instructions...
                        goto next;
                    }
                }
                    break;
                case JUMP_IF_NOT_EXC_MATCH:
                    op.state.pop();
                    op.state.pop();

                    if (updateStartState(op.state, op.oparg)) {
                        queue.push_back(op.oparg);
                    }
                    goto next;
                case JUMP_ABSOLUTE:
                    if (updateStartState(op.state, op.oparg)) {
                        queue.push_back(op.oparg);
                    }
                    // Done processing this basic block, we'll need to see a branch
                    // to the following opcodes before we'll process them.
                    goto next;
                case JUMP_FORWARD:
                    if (updateStartState(op.state, (size_t) op.oparg + op.index + SIZEOF_CODEUNIT)) {
                        queue.push_back((size_t) op.oparg + op.index + SIZEOF_CODEUNIT);
                    }
                    // Done processing this basic block, we'll need to see a branch
                    // to the following opcodes before we'll process them.
                    goto next;
                case RETURN_VALUE: {
                    auto retValue = op.state.pop();
                    mReturnValue = mReturnValue->mergeWith(retValue.Value);
                    }
                    goto next;
                case LOAD_NAME:
                    // Used to load __name__ for a class def
                    op.state.push(&Any);
                    break;
                case STORE_NAME:
                    // Stores __module__, __doc__, __qualname__, as well as class/function defs sometimes
                    op.state.pop();
                    break;
                case DELETE_NAME:
                    break;
                case LOAD_CLASSDEREF:
                    op.state.push(&Any);
                    break;
                case LOAD_GLOBAL: {
                    auto name = PyTuple_GetItem(mCode->co_names, op.oparg);

                    PyObject* v = PyObject_GetItem(globals, name);
                    if (v == nullptr) {
                        PyErr_Clear();
                        v = PyObject_GetItem(builtins, name);
                        if (v == nullptr) {
                            PyErr_Clear();
                            // Neither. Maybe it'll appear at runtime!!
                            op.state.push(&Any);
                        }
                        else {
                            // Builtin source
                            auto globalSource = op.addBuiltinSource(op.oparg, utf8_names[op.oparg], v);
                            auto builtinType = Py_TYPE(v);
                            AbstractValue* avk = avkToAbstractValue(GetAbstractType(builtinType));
                            auto value = AbstractValueWithSources(
                                    avk,
                                    globalSource
                            );
                            op.state.push(value);
                        }
                    } else {
                        // global source
                        auto globalSource = op.addGlobalSource(op.oparg, PyUnicode_AsUTF8(name), v);
                        auto value = AbstractValueWithSources(
                                &Any,
                                globalSource
                        );
                        op.state.push(value);
                    }
                    break;
                }
                case STORE_GLOBAL:
                    op.state.pop();
                    break;
                case LOAD_ATTR:
                    if (PGC_READY()){
                        PGC_PROBE(1);
                        PGC_UPDATE_STACK(1);
                    }
                    op.state.pop();
                    op.state.push(&Any);
                    break;
                case STORE_ATTR:
                    op.state.pop();
                    op.state.pop();
                    break;
                case DELETE_ATTR:
                    op.state.pop();
                    break;
                case BUILD_LIST:
                    for (int i = 0; i < op.oparg; i++) {
                        op.state.pop();
                    }
                    op.state.push(&List);
                    break;
                case BUILD_TUPLE: {
                    for (int i = 0; i < op.oparg; i++) {
                        op.state.pop();
                    }
                    op.state.push(&Tuple);
                    break;
                }
                case BUILD_MAP:
                    for (int i = 0; i < op.oparg; i++) {
                        op.state.pop();
                        op.state.pop();
                    }
                    op.state.push(&Dict);
                    break;
                case COMPARE_OP: {
                    if (PGC_READY()){
                        PGC_PROBE(2);
                        PGC_UPDATE_STACK(2);
                    }
                    op.state.pop();
                    op.state.pop();
                    op.state.push(&Any);
                }
                break;
                case IMPORT_NAME:
                    op.state.pop();
                    op.state.pop();
                    op.state.push(&Any);
                    break;
                case IMPORT_FROM:
                case LOAD_CLOSURE:
                    op.state.push(&Any);
                    break;
                case CALL_FUNCTION: {
                    if (PGC_READY()){
                        PGC_PROBE(op.oparg + 1);
                        PGC_UPDATE_STACK(op.oparg+1);
                    }
                    int argCnt = op.oparg & 0xff;
                    int kwArgCnt = (op.oparg >> 8) & 0xff;

                    for (int i = 0; i < argCnt; i++) {
                        op.state.pop();
                    }
                    for (int i = 0; i < kwArgCnt; i++) {
                        op.state.pop();
                        op.state.pop();
                    }

                    // pop the function...
                    auto func = op.state.pop();
                    auto source = AbstractValueWithSources(
                        avkToAbstractValue(knownFunctionReturnType(func)),
                        newSource(new LocalSource()));
                    op.state.push(source);
                    break;
                }
                case CALL_FUNCTION_KW: {
                    // Pop the names tuple
                    auto names = op.state.pop();
                    assert(names.Value->kind() == AVK_Tuple);

                    for (int i = 0; i < op.oparg; i++) {
                        op.state.pop();
                    }

                    // pop the function
                    op.state.pop();

                    op.state.push(&Any);
                    break;
                }
                case CALL_FUNCTION_EX:
                    if (op.oparg & 0x01) {
                        // kwargs
                        op.state.pop();
                    }

                    // call args (iterable)
                    op.state.pop();
                    // function
                    op.state.pop();

                    op.state.push(&Any);
                    break;
                case MAKE_FUNCTION: {
                    op.state.pop(); // qual name
                    op.state.pop(); // code

                    if (op.oparg & 0x08) {
                        // closure object
                        op.state.pop();
                    }
                    if (op.oparg & 0x04) {
                        // annotations
                        op.state.pop();
                    }
                    if (op.oparg & 0x02) {
                        // kw defaults
                        op.state.pop();
                    }
                    if (op.oparg & 0x01) {
                        // defaults
                        op.state.pop();
                    }

                    op.state.push(&Function);
                    break;
                }
                case BUILD_SLICE:
                    for (int i = 0; i < op.oparg; i++) {
                        op.state.pop();
                    }
                    op.state.push(&Slice);
                    break;
                case UNARY_POSITIVE:
                case UNARY_NEGATIVE:
                case UNARY_INVERT:
                case UNARY_NOT: {
                    auto in = op.state.pop();
                    auto out = in.Value->unary(in.Sources, op.opcode);
                    op.state.push(out);
                    break;
                }
                case UNPACK_EX:
                    op.state.pop();
                    for (int i = 0; i < op.oparg >> 8; i++) {
                        op.state.push(&Any);
                    }
                    op.state.push(&List);
                    for (int i = 0; i < (op.oparg & 0xff); i++) {
                        op.state.push(&Any);
                    }
                    break;
                case UNPACK_SEQUENCE:
                    if (PGC_READY()){
                        PGC_PROBE(1);
                        PGC_UPDATE_STACK(1);
                    }
                    op.state.pop();
                    for (int i = 0; i < op.oparg; i++) {
                        op.state.push(&Any);
                    }
                    break;
                case RAISE_VARARGS:
                    for (int i = 0; i < op.oparg; i++) {
                        op.state.pop();
                    }
                    goto next;
                case STORE_SUBSCR:
                    if (PGC_READY()){
                        PGC_PROBE(3);
                        PGC_UPDATE_STACK(3);
                    }
                    op.state.pop();
                    op.state.pop();
                    op.state.pop();
                    break;
                case DELETE_SUBSCR:
                    op.state.pop();
                    op.state.pop();
                    break;
                case BUILD_SET:
                    for (int i = 0; i < op.oparg; i++) {
                        op.state.pop();
                    }
                    op.state.push(&Set);
                    break;
                case STORE_DEREF:
                    // There is no tracking of cell variables.
                    op.state.pop();
                    break;
                case LOAD_DEREF:
                    // There is no tracking of cell variables.
                    op.state.push(&Any);
                    break;
                case DELETE_DEREF:
                    // Since cell variables are not tracked, no need to worry
                    // about their deletion.
                    break;
                case GET_ITER: {
                    auto iteratorType = op.state.pop();
                    // TODO : Allow guarded/PGC sources to be optimized.
                    auto source = AbstractValueWithSources(
                            &Iterable,
                            newSource(new IteratorSource(iteratorType.Value->needsGuard() ? AVK_Any: iteratorType.Value->kind())));
                    op.state.push(source);
                }
                    break;
                case FOR_ITER: {
                    // For branches out with the value consumed
                    auto leaveState = op.state;
                    leaveState.pop(); // Iterator
                    if (updateStartState(leaveState, (size_t) op.oparg + op.index + SIZEOF_CODEUNIT)) {
                        queue.push_back((size_t) op.oparg + op.index + SIZEOF_CODEUNIT);
                    }

                    // When we compile this we don't actually leave the value on the stack,
                    // but the sequence of opcodes assumes that happens.  to keep our stack
                    // properly balanced we match what's really going on.
                    op.state.push(&Any);
                    break;
                }
                case POP_BLOCK:
                    op.state.mStack = mByteCodeSequence[m_blockStarts[op.index]].state.mStack;
                    op.state.push(&Any);
                    op.state.push(&Any);
                    op.state.push(&Any);
                case POP_EXCEPT:
                    skipEffect = true;
                    break;
                case LOAD_BUILD_CLASS:
                    // TODO: if we know this is __builtins__.__build_class__ we can push a special value
                    // to optimize the call.f
                    op.state.push(&Any);
                    break;
                case SET_ADD:
                    op.state.pop();
                    break;
                case LIST_APPEND:
                    // pop the value being stored off, leave list on stack
                    op.state.pop();
                    op.state.pop();
                    op.state.push(&List);
                    break;
                case MAP_ADD:
                    // pop the value and key being stored off, leave list on stack
                    op.state.pop();
                    op.state.pop();
                    op.state.pop();
                    op.state.push(&Dict);
                    break;
                case FORMAT_VALUE:
                    if ((op.oparg & FVS_MASK) == FVS_HAVE_SPEC) {
                        // format spec
                        op.state.pop();
                    }
                    op.state.pop();
                    op.state.push(&String);
                    break;
                case BUILD_STRING:
                    for (auto i = 0; i < op.oparg; i++) {
                        op.state.pop();
                    }
                    op.state.push(&String);
                    break;
                case SETUP_ASYNC_WITH:
                case SETUP_WITH: {
                    auto finallyState = op.state;
                    finallyState.push(&Any);
                    if (updateStartState(finallyState, (size_t) op.oparg + op.index + SIZEOF_CODEUNIT)) {
                        jump = 1;
                        queue.push_back((size_t) op.oparg + op.index + SIZEOF_CODEUNIT);
                    }
                    op.state.push(&Any);
                    goto next;
                }
                case SETUP_FINALLY: {
                    auto ehState = op.state;
                    // Except is entered with the exception object, traceback, and exception
                    // type.  TODO: We could type these stronger then they currently are typed
                    ehState.push(&Any);
                    ehState.push(&Any);
                    ehState.push(&Any);
                    ehState.push(&Any);
                    ehState.push(&Any);
                    ehState.push(&Any);
                    if (updateStartState(ehState, (size_t) op.oparg + op.index + SIZEOF_CODEUNIT)) {
                        queue.push_back((size_t)op.oparg + op.index + SIZEOF_CODEUNIT);
                    }
                    break;
                }
                case BUILD_CONST_KEY_MAP:
                    op.state.pop(); //keys
                    for (auto i = 0; i < op.oparg; i++) {
                        op.state.pop(); // values
                    }
                    op.state.push(&Dict);
                    break;
                case LOAD_METHOD: {
                    auto method = AbstractValueWithSources(
                            &Method,
                            newSource(new MethodSource(utf8_names[op.oparg])));
                    op.state.push(method);
                    break;
                }
                case CALL_METHOD:
                {
                    /* LOAD_METHOD could push a NULL value, but (as above)
                     it doesn't, so instead it just considers the same scenario.

                     This is a method call.  Stack layout:

                         ... | method | self | arg1 | ... | argN
                                                            ^- TOP()
                                               ^- (-oparg)
                                        ^- (-oparg-1)
                               ^- (-oparg-2)

                      `self` and `method` will be POPed by call_function.
                      We'll be passing `oparg + 1` to call_function, to
                      make it accept the `self` as a first argument.
                    */
                    if (PGC_READY()){
                        PGC_PROBE(1 + op.oparg);
                        PGC_UPDATE_STACK(1 + op.oparg);
                    }
                    auto method = op.state.pop();
                    auto self = op.state.pop();
                    for (int i = 0 ; i < op.oparg; i++)
                        op.state.pop();
                    if (method.hasValue() && method.Value->kind() == AVK_Method && self.Value->known()){
                        auto meth_source = dynamic_cast<MethodSource*>(method.Sources);
                        op.state.push(avkToAbstractValue(avkToAbstractValue(self.Value->kind())->resolveMethod(meth_source->name())));
                    } else {
                        op.state.push(&Any); // push result.
                    }
                    break;
                }
                case IS_OP:
                case CONTAINS_OP:
                    op.state.pop();
                    op.state.pop();
                    op.state.push(&Bool);
                    break;
                case WITH_EXCEPT_START: {
                    // TODO: Implement WITH_EXCEPT_START
                    /* At the top of the stack are 7 values:
                       - (TOP, SECOND, THIRD) = exc_info()
                       - (FOURTH, FIFTH, SIXTH) = previous exception for EXCEPT_HANDLER
                       - SEVENTH: the context.__exit__ bound method
                       We call SEVENTH(TOP, SECOND, THIRD).
                       Then we push again the TOP exception and the __exit__
                       return value.
                    */
                    return IncompatibleOpcode_WithExcept; // not implemented
//                    auto top = op.lastState.pop(); // exc
//                    auto second = op.lastState.pop(); // val
//                    auto third = op.lastState.pop(); // tb
//                    auto seventh = op.lastState[op.lastState.stackSize() - 7]; // exit_func
//                    // TODO : Vectorcall (exit_func, stack+1, 3, ..)
//                    op.lastState.push(&Any); // res
//                    break;
                }
                case LIST_EXTEND:
                {
                    op.state.pop();
                    op.state.pop();
                    op.state.push(&List);
                    break;
                }
                case DICT_UPDATE:
                case SET_UPDATE:
                case DICT_MERGE:
                case PRINT_EXPR:
                {
                    op.state.pop(); // value
                    break;
                }
                case LIST_TO_TUPLE:
                {
                    op.state.pop(); // list
                    op.state.push(&Tuple); // tuple
                    break;
                }
                case LOAD_ASSERTION_ERROR:
                {
                    op.state.push(&Any);
                    break;
                }
                case IMPORT_STAR:
                    op.state.pop();
                    break;
                case DELETE_GLOBAL:
                case SETUP_ANNOTATIONS:
                    break;  // No stack effect
                default:
                    PyErr_Format(PyExc_ValueError,
                                 "Unknown unsupported opcode: %d", op.opcode);
                    return IncompatibleOpcode_Unknown;
                    break;
            }
#ifdef DEBUG
            assert(skipEffect || 
                static_cast<size_t>(PyCompile_OpcodeStackEffectWithJump(op.opcode, op.oparg, jump)) == (op.state.stackSize() - curStackLen));
#endif
            updateStartState(op.state, op.index + SIZEOF_CODEUNIT);
            op.state.pgcProbeSize = pgcSize;
            op.state.requiresPgcProbe = pgcRequired;
        }

    next:;
    } while (!queue.empty());

    return Success;
}

bool AbstractInterpreter::updateStartState(InterpreterState& newState, BytecodeOperation op) {
    if (op.hasState) {
        return mergeStates(newState, op.state);
    }
    else {
        op.state = newState;
        op.hasState = true;
        return true;
    }
}

bool AbstractInterpreter::updateStartState(InterpreterState& newState, size_t op_idx){
    return updateStartState(newState, mByteCodeSequence[op_idx]);
}

// TODO: Move this method.
bool AbstractInterpreter::mergeStates(InterpreterState& newState, InterpreterState& mergeTo) {
    bool changed = false;
    if (mergeTo.mLocals != newState.mLocals) {
        // need to merge locals...
        if(mergeTo.localCount() != newState.localCount()){
            throw StackImbalanceException();
        }
        for (size_t i = 0; i < newState.localCount(); i++) {
            auto oldType = mergeTo.getLocal(i);
            auto newType = oldType.mergeWith(newState.getLocal(i));
            if (newType != oldType) {
                mergeTo.replaceLocal(i, newType);
                changed = true;
            }
        }
    }

    if (mergeTo.stackSize() == 0) {
        // first time we assigned, or empty stack...
        mergeTo.mStack = newState.mStack;
        changed |= newState.stackSize() != 0;
    }
    else {
        size_t max = mergeTo.stackSize();;
        if (newState.stackSize() < mergeTo.stackSize())
            max = newState.stackSize();

        // need to merge the stacks...
        for (size_t i = 0; i < max; i++) {
            auto newType = mergeTo[i].mergeWith(newState[i]);
            if (mergeTo[i] != newType) {
                mergeTo[i] = newType;
                changed = true;
            }
        }
    }
    return changed;
}

AbstractValue* AbstractInterpreter::toAbstract(PyObject*obj) {
    if (obj == Py_None) {
        return &None;
    }
    else if (PyLong_CheckExact(obj)) {
        int value;
        if (Py_SIZE(obj) < 4)
            if (IS_SMALL_INT(PyLong_AsLongLongAndOverflow(obj, &value)))
                return &InternInteger;
        return &Integer;
    }
    else if (PyUnicode_Check(obj)) {
        return &String;
    }
    else if (PyList_CheckExact(obj)) {
        return &List;
    }
    else if (PyDict_CheckExact(obj)) {
        return &Dict;
    }
    else if (PyTuple_CheckExact(obj)) {
        return &Tuple;
    }
    else if (PyBool_Check(obj)) {
        return &Bool;
    }
    else if (PyFloat_CheckExact(obj)) {
        return &Float;
    }
    else if (PyBytes_CheckExact(obj)) {
        return &Bytes;
    }
    else if (PySet_Check(obj)) {
        return &Set;
    }
    else if (PyComplex_CheckExact(obj)) {
        return &Complex;
    }
    else if (PyFunction_Check(obj)) {
        return &Function;
    }
    return &Any;
}

// Returns information about the specified local variable at a specific
// byte code index.
AbstractLocalInfo AbstractInterpreter::getLocalInfo(size_t byteCodeIndex, size_t localIndex) {
    return mByteCodeSequence[byteCodeIndex].state.getLocal(localIndex);
}

// Returns information about the stack at the specific byte code index.
InterpreterStack& AbstractInterpreter::getStackInfo(size_t byteCodeIndex) {
    return mByteCodeSequence[byteCodeIndex].state.mStack;
}

uint8_t AbstractInterpreter::pgcProbeSize(size_t byteCodeIndex) {
    return mByteCodeSequence[byteCodeIndex].state.pgcProbeSize;
}

bool AbstractInterpreter::pgcProbeRequired(size_t byteCodeIndex, PgcStatus status) {
    if (status == PgcStatus::Uncompiled)
        return mByteCodeSequence[byteCodeIndex].state.requiresPgcProbe;
    return false;
}

AbstractValue* AbstractInterpreter::getReturnInfo() {
    return mReturnValue;
}

 // Checks to see if we have a non-zero error code on the stack, and if so,
 // branches to the current error handler.  Consumes the error code in the process
void AbstractInterpreter::intErrorCheck(const char* reason, size_t curByte) {
    auto noErr = m_comp->emit_define_label();
    m_comp->emit_branch(BranchFalse, noErr);
    branchRaise(reason, curByte);
    m_comp->emit_mark_label(noErr);
}

// Checks to see if we have a null value as the last value on our stack
// indicating an error, and if so, branches to our current error handler.
void AbstractInterpreter::errorCheck(const char *reason, size_t curByte) {
    auto noErr = m_comp->emit_define_label();
    m_comp->emit_dup();
    m_comp->emit_store_local(mErrorCheckLocal);
    m_comp->emit_null();
    m_comp->emit_branch(BranchNotEqual, noErr);

    branchRaise(reason, curByte);
    m_comp->emit_mark_label(noErr);
    m_comp->emit_load_local(mErrorCheckLocal);
}

Label AbstractInterpreter::getOffsetLabel(size_t jumpTo) {
    return mByteCodeSequence[jumpTo].label;
}

void AbstractInterpreter::ensureRaiseAndFreeLocals(size_t localCount) {
    while (m_raiseAndFreeLocals.size() <= localCount) {
        m_raiseAndFreeLocals.push_back(m_comp->emit_define_local());
    }
}

vector<Label>& AbstractInterpreter::getRaiseAndFreeLabels(size_t blockId) {
    while (m_raiseAndFree.size() <= blockId) {
        m_raiseAndFree.emplace_back();
    }

    return m_raiseAndFree[blockId];
}

void AbstractInterpreter::ensureLabels(vector<Label>& labels, size_t count) {
    for (auto i = labels.size(); i < count; i++) {
        labels.push_back(m_comp->emit_define_label());
    }
}

void AbstractInterpreter::branchRaise(const char *reason, size_t curByte) {
    auto ehBlock = currentHandler();
    auto& entryStack = ehBlock->EntryStack;

#ifdef DEBUG
    if (reason != nullptr) {
        m_comp->emit_debug_msg(reason);
    }
#endif
    if (!canSkipLastiUpdate(curByte)) {
        m_comp->emit_lasti_update(curByte);
    }

    m_comp->emit_eh_trace();

    if (mTracingEnabled)
        m_comp->emit_trace_exception();

    // number of stack entries we need to clear...
    auto count = static_cast<ssize_t>(m_stack.size() - entryStack.size());

    auto cur = m_stack.rbegin();
    for (; cur != m_stack.rend() && count >= 0; cur++) {
        if (*cur == STACK_KIND_VALUE) {
            count--;
            m_comp->emit_pop();
        }
        else {
            break;
        }
    }

    if (!ehBlock->IsRootHandler())
        incExcVars(6);

    if (!count || count < 0) {
        // No values on the stack, we can just branch directly to the raise label
        m_comp->emit_branch(BranchAlways, ehBlock->ErrorTarget);
        return;
    }

    vector<Label>& labels = getRaiseAndFreeLabels(ehBlock->RaiseAndFreeId);
    ensureLabels(labels, count);
    ensureRaiseAndFreeLocals(count);

    // continue walking our stack iterator
    for (auto i = 0; i < count; cur++, i++) {
        if (*cur == STACK_KIND_VALUE) {
            // pop off the stack value...
            m_comp->emit_pop();

            // and store null into our local that needs to be freed
            m_comp->emit_null();
            m_comp->emit_store_local(m_raiseAndFreeLocals[i]);
        }
        else {
            m_comp->emit_store_local(m_raiseAndFreeLocals[i]);
        }
    }
    m_comp->emit_branch(BranchAlways, ehBlock->ErrorTarget);
}

void AbstractInterpreter::buildTuple(size_t argCnt) {
    m_comp->emit_new_tuple(argCnt);
    if (argCnt != 0) {
        errorCheck("tuple build failed");
        m_comp->emit_tuple_store(argCnt);
        decStack(argCnt);
    }
}

void AbstractInterpreter::buildList(size_t argCnt) {
    m_comp->emit_new_list(argCnt);
    errorCheck("build list failed");
    if (argCnt != 0) {
        m_comp->emit_list_store(argCnt);
    }
    decStack(argCnt);
}

void AbstractInterpreter::extendListRecursively(Local list, size_t argCnt) {
    if (argCnt == 0) {
        return;
    }

    auto valueTmp = m_comp->emit_define_local();
    m_comp->emit_store_local(valueTmp);
    decStack();

    extendListRecursively(list, --argCnt);

    m_comp->emit_load_local(valueTmp); // arg 2
    m_comp->emit_load_local(list);  // arg 1

    m_comp->emit_list_extend();
    intErrorCheck("list extend failed");

    m_comp->emit_free_local(valueTmp);
}

void AbstractInterpreter::extendList(size_t argCnt) {
    assert(argCnt > 0);
    auto listTmp = m_comp->emit_spill();
    decStack();
    extendListRecursively(listTmp, argCnt);
    m_comp->emit_load_and_free_local(listTmp);
    incStack(1, STACK_KIND_OBJECT);
}

void AbstractInterpreter::buildSet(size_t argCnt) {
    m_comp->emit_new_set();
    errorCheck("build set failed");

    if (argCnt != 0) {
        auto setTmp = m_comp->emit_define_local();
        m_comp->emit_store_local(setTmp);
        auto* tmps = new Local[argCnt];
        auto* frees = new Label[argCnt];
        for (size_t i = 0; i < argCnt; i++) {
            tmps[argCnt - (i + 1)] = m_comp->emit_spill();
            decStack();
        }

        // load all the values into the set...
        auto err = m_comp->emit_define_label();
        for (size_t i = 0; i < argCnt; i++) {
            m_comp->emit_load_local(setTmp);
            m_comp->emit_load_local(tmps[i]);

            m_comp->emit_set_add();
            frees[i] = m_comp->emit_define_label();
            m_comp->emit_branch(BranchFalse, frees[i]);
        }

        auto noErr = m_comp->emit_define_label();
        m_comp->emit_branch(BranchAlways, noErr);

        m_comp->emit_mark_label(err);
        m_comp->emit_load_local(setTmp);
        m_comp->emit_pop_top();

        // In the event of an error we need to free any
        // args that weren't processed.  We'll always process
        // the 1st value and dec ref it in the set add helper.
        // tmps[0] = 'a', tmps[1] = 'b', tmps[2] = 'c'
        // We'll process tmps[0], and if that fails, then we need
        // to free tmps[1] and tmps[2] which correspond with frees[0]
        // and frees[1]
        for (size_t i = 1; i < argCnt; i++) {
            m_comp->emit_mark_label(frees[i - 1]);
            m_comp->emit_load_local(tmps[i]);
            m_comp->emit_pop_top();
        }

        // And if the last one failed, then all of the values have been
        // decref'd
        m_comp->emit_mark_label(frees[argCnt - 1]);
        branchRaise("build set failed");

        m_comp->emit_mark_label(noErr);
        delete[] frees;
        delete[] tmps;

        m_comp->emit_load_local(setTmp);
        m_comp->emit_free_local(setTmp);
    }
    incStack();
}

void AbstractInterpreter::buildMap(size_t  argCnt) {
    m_comp->emit_new_dict(argCnt);
    errorCheck("build map failed");

    if (argCnt > 0) {
        auto map = m_comp->emit_spill();
        for (size_t curArg = 0; curArg < argCnt; curArg++) {
            m_comp->emit_load_local(map);

            m_comp->emit_dict_store();

            decStack(2);
            intErrorCheck("dict store failed");
        }
        m_comp->emit_load_and_free_local(map);
    }
}

void AbstractInterpreter::makeFunction(size_t oparg) {
    m_comp->emit_new_function();
    decStack(2);
    errorCheck("new function failed");

    if (oparg & 0x0f) {
        auto func = m_comp->emit_spill();
        if (oparg & 0x08) {
            // closure
            auto tmp = m_comp->emit_spill();
            m_comp->emit_load_local(func);
            m_comp->emit_load_and_free_local(tmp);
            m_comp->emit_set_closure();
            decStack();
        }
        if (oparg & 0x04) {
            // annoations
            auto tmp = m_comp->emit_spill();
            m_comp->emit_load_local(func);
            m_comp->emit_load_and_free_local(tmp);
            m_comp->emit_set_annotations();
            decStack();
        }
        if (oparg & 0x02) {
            // kw defaults
            auto tmp = m_comp->emit_spill();
            m_comp->emit_load_local(func);
            m_comp->emit_load_and_free_local(tmp);
            m_comp->emit_set_kw_defaults();
            decStack();
        }
        if (oparg & 0x01) {
            // defaults
            auto tmp = m_comp->emit_spill();
            m_comp->emit_load_local(func);
            m_comp->emit_load_and_free_local(tmp);
            m_comp->emit_set_defaults();
            decStack();
        }
        m_comp->emit_load_and_free_local(func);
    }

    incStack();
}

void AbstractInterpreter::decStack(size_t size) {
    m_stack.dec(size);
}

void AbstractInterpreter::incStack(size_t size, StackEntryKind kind) {
    m_stack.inc(size, kind);
}

// Checks to see if -1 is the current value on the stack, and if so, falls into
// the logic for raising an exception.  If not execution continues forward progress.
// Used for checking if an API reports an error (typically true/false/-1)
void AbstractInterpreter::raiseOnNegativeOne(size_t curByte) {
    m_comp->emit_dup();
    m_comp->emit_int(-1);

    auto noErr = m_comp->emit_define_label();
    m_comp->emit_branch(BranchNotEqual, noErr);
    // we need to issue a leave to clear the stack as we may have
    // values on the stack...

    m_comp->emit_pop();
    branchRaise("last operation failed", curByte);
    m_comp->emit_mark_label(noErr);
}

void AbstractInterpreter::emitRaise(ExceptionHandler * handler) {
    m_comp->emit_load_local(handler->ExVars.PrevTraceback);
    m_comp->emit_load_local(handler->ExVars.PrevExcVal);
    m_comp->emit_load_local(handler->ExVars.PrevExc);
    m_comp->emit_load_local(handler->ExVars.FinallyTb);
    m_comp->emit_load_local(handler->ExVars.FinallyValue);
    m_comp->emit_load_local(handler->ExVars.FinallyExc);
}

void AbstractInterpreter::decExcVars(size_t count){
    m_comp->emit_dec_local(mExcVarsOnStack, count);
}

void AbstractInterpreter::incExcVars(size_t count) {
    m_comp->emit_inc_local(mExcVarsOnStack, count);
}

void AbstractInterpreter::popExcVars(){
    auto nothing_to_pop = m_comp->emit_define_label();
    auto loop = m_comp->emit_define_label();

    m_comp->emit_mark_label(loop);
    m_comp->emit_load_local(mExcVarsOnStack);
    m_comp->emit_branch(BranchFalse, nothing_to_pop);
    m_comp->emit_pop();
    m_comp->emit_dec_local(mExcVarsOnStack, 1);
    m_comp->emit_branch(BranchAlways, loop);

    m_comp->emit_mark_label(nothing_to_pop);
}

AbstactInterpreterCompileResult AbstractInterpreter::compileWorker(PgcStatus pgc_status) {
    Label ok;

    m_comp->emit_lasti_init();
    m_comp->emit_push_frame();

    if (OPT_ENABLED(nativeLocals))
        m_comp->emit_load_frame_locals();

    auto rootHandlerLabel = m_comp->emit_define_label();

    mExcVarsOnStack = m_comp->emit_define_local(LK_Int);
    m_comp->emit_int(0);
    m_comp->emit_store_local(mExcVarsOnStack);

    m_comp->emit_init_instr_counter();

    if (mTracingEnabled){
        // push initial trace on entry to frame
        m_comp->emit_trace_frame_entry();

        mTracingInstrLowerBound = m_comp->emit_define_local(LK_Int);
        m_comp->emit_int(0);
        m_comp->emit_store_local(mTracingInstrLowerBound);

        mTracingInstrUpperBound = m_comp->emit_define_local(LK_Int);
        m_comp->emit_int(-1);
        m_comp->emit_store_local(mTracingInstrUpperBound);

        mTracingLastInstr = m_comp->emit_define_local(LK_Int);
        m_comp->emit_int(-1);
        m_comp->emit_store_local(mTracingLastInstr);
    }
    if (mProfilingEnabled) { m_comp->emit_profile_frame_entry(); }

    // Push a catch-all error handler onto the handler list
    auto rootHandler = m_exceptionHandler.SetRootHandler(rootHandlerLabel, ExceptionVars(m_comp));

    // Push root block to stack, has no end offset
    m_blockStack.push_back(BlockInfo(-1, NOP, rootHandler));

    // Loop through all opcodes in this frame
    size_t curByte = 0;
    for (auto & op : mByteCodeSequence) {
        curByte += SIZEOF_CODEUNIT;

        // opcodeIndex is the opcode position (matches the dis.dis() output)
        auto opcodeIndex = curByte;

        // Get the opcode identifier (see opcode.h)
        auto byte = GET_OPCODE(curByte);

        // Get an additional oparg, see dis help for information on what each means
        size_t oparg = GET_OPARG(curByte);

    processOpCode:
        m_comp->emit_mark_label(mByteCodeSequence[curByte].label);
        m_comp->mark_sequence_point(curByte);

        // See if current index is part of offset stack, used for jump operations
        auto curStackDepth = m_offsetStack.find(curByte);
        if (curStackDepth != m_offsetStack.end()) {
            // Recover stack from jump
            m_stack = curStackDepth->second;
        }
        if (m_exceptionHandler.IsHandlerAtOffset(curByte)){
            ExceptionHandler* handler = m_exceptionHandler.HandlerAtOffset(curByte);
            m_comp->emit_mark_label(handler->ErrorTarget);
            emitRaise(handler);
        }

        if (!canSkipLastiUpdate(curByte)) {
            if (mTracingEnabled){
                m_comp->emit_trace_line(mTracingInstrLowerBound, mTracingInstrUpperBound, mTracingLastInstr);
            }
        }

        auto stackInfo = getStackInfo(curByte);
        auto next_byte = (curByte + SIZEOF_CODEUNIT) < mSize ? GET_OPCODE(curByte + SIZEOF_CODEUNIT) : -1;
        auto nextStackInfo = getStackInfo(next_byte);

        size_t curStackSize = m_stack.size();
        bool skipEffect = false;

        if (g_pyjionSettings.pgc && pgcProbeRequired(curByte, pgc_status)){
            m_comp->emit_pgc_probe(curByte, pgcProbeSize(curByte));
        }

        if (OPT_ENABLED(tripleBinaryFunctions) && stackInfo.size() >= 2 && nextStackInfo.size() >= 1 && canBeOptimized(byte, next_byte, stackInfo.top().Value->kind(), stackInfo.second().Value->kind(), nextStackInfo.top().Value->kind())){
            m_comp->emit_triple_binary_op(byte, next_byte);
            decStack(3);
            errorCheck("binary math op failed", curByte);
            incStack();
            curByte += SIZEOF_CODEUNIT;
            continue;
        }
        if (OPT_ENABLED(subscrSlice) && byte == BUILD_SLICE && next_byte == BINARY_SUBSCR && stackInfo.size() >= (1 + oparg)){
            bool optimized ;
            if (oparg == 3) {
                optimized = m_comp->emit_binary_subscr_slice(stackInfo.fourth(), stackInfo.third(), stackInfo.second(), stackInfo.top());
            } else {
                optimized = m_comp->emit_binary_subscr_slice(stackInfo.third(), stackInfo.second(), stackInfo.top());
            }
            if (optimized) {
                decStack(oparg + 1);
                errorCheck("subscr slice failed", curByte);
                incStack();
                curByte += SIZEOF_CODEUNIT;
                continue;
            } // Else, use normal compilation path.
        }

        switch (byte) {
            case NOP: break;
            case ROT_TWO:
            {
                m_comp->emit_rot_two();
                break;
            }
            case ROT_THREE:
            {
                m_comp->emit_rot_three();
                break;
            }
            case ROT_FOUR:
            {
                m_comp->emit_rot_four();
                break;
            }
            case POP_TOP:
                m_comp->emit_pop_top();
                decStack();
                break;
            case DUP_TOP:
                m_comp->emit_dup_top();
                m_stack.dup_top(); // implicit incStack(1)
                break;
            case DUP_TOP_TWO:
                incStack(2);
                m_comp->emit_dup_top_two();
                break;
            case COMPARE_OP: {
                if ((OPT_ENABLED(internRichCompare) || OPT_ENABLED(compare)) && stackInfo.size() >= 2){
                    m_comp->emit_compare_known_object(static_cast<int>(oparg), stackInfo.second(), stackInfo.top());
                } else {
                    m_comp->emit_compare_object(static_cast<int>(oparg));
                }
                decStack(2);
                errorCheck("failed to compare", curByte);
                incStack(1);
                break; }
            case LOAD_BUILD_CLASS:
                m_comp->emit_load_build_class();
                errorCheck("load build class failed", curByte);
                incStack();
                break;
            case SETUP_ANNOTATIONS:
                m_comp->emit_set_annotations();
                intErrorCheck("failed to setup annotations", curByte);
                break;
            case JUMP_ABSOLUTE:
                jumpAbsolute(oparg, opcodeIndex); break;
            case JUMP_FORWARD:
                jumpAbsolute(oparg + curByte + SIZEOF_CODEUNIT, opcodeIndex); break;
            case JUMP_IF_FALSE_OR_POP:
            case JUMP_IF_TRUE_OR_POP:
                jumpIfOrPop(byte != JUMP_IF_FALSE_OR_POP, opcodeIndex, oparg);
                skipEffect = true;
                break;
            case JUMP_IF_NOT_EXC_MATCH:
                jumpIfNotExact(opcodeIndex, oparg);
                break;
            case POP_JUMP_IF_TRUE:
            case POP_JUMP_IF_FALSE:
                popJumpIf(byte != POP_JUMP_IF_FALSE, opcodeIndex, oparg);
                break;
            case LOAD_NAME:
                if (OPT_ENABLED(hashedNames)){
                    m_comp->emit_load_name_hashed(PyTuple_GetItem(mCode->co_names, oparg), (long)nameHashes[oparg]);
                } else {
                    m_comp->emit_load_name(PyTuple_GetItem(mCode->co_names, oparg));
                }
                errorCheck("load name failed", curByte);
                incStack();
                break;
            case STORE_ATTR:
                m_comp->emit_store_attr(PyTuple_GetItem(mCode->co_names, oparg));
                decStack(2);
                intErrorCheck("store attr failed", curByte);
                break;
            case DELETE_ATTR:
                m_comp->emit_delete_attr(PyTuple_GetItem(mCode->co_names, oparg));
                decStack();
                intErrorCheck("delete attr failed", curByte);
                break;
            case LOAD_ATTR:
                if (OPT_ENABLED(loadAttr) && stackInfo.size() > 0){
                    m_comp->emit_load_attr(PyTuple_GetItem(mCode->co_names, oparg), stackInfo.top());
                } else {
                    m_comp->emit_load_attr(PyTuple_GetItem(mCode->co_names, oparg));
                }
                decStack();
                errorCheck("load attr failed", curByte);
                incStack();
                break;
            case STORE_GLOBAL:
                m_comp->emit_store_global(PyTuple_GetItem(mCode->co_names, oparg));
                decStack();
                intErrorCheck("store global failed", curByte);
                break;
            case DELETE_GLOBAL:
                m_comp->emit_delete_global(PyTuple_GetItem(mCode->co_names, oparg));
                intErrorCheck("delete global failed", curByte);
                break;
            case LOAD_GLOBAL:
                if (OPT_ENABLED(hashedNames)){
                    m_comp->emit_load_global_hashed(PyTuple_GetItem(mCode->co_names, oparg), nameHashes[oparg]);
                } else {
                    m_comp->emit_load_global(PyTuple_GetItem(mCode->co_names, oparg));
                }
                errorCheck("load global failed", curByte);
                incStack();
                break;
            case LOAD_CONST:
                loadConst(oparg, opcodeIndex); break;
            case STORE_NAME:
                m_comp->emit_store_name(PyTuple_GetItem(mCode->co_names, oparg));
                decStack();
                intErrorCheck("store name failed", curByte);
                break;
            case DELETE_NAME:
                m_comp->emit_delete_name(PyTuple_GetItem(mCode->co_names, oparg));
                intErrorCheck("delete name failed", curByte);
                break;
            case DELETE_FAST:
                loadFastWorker(oparg, true, curByte);
                m_comp->emit_pop_top();
                m_comp->emit_delete_fast(oparg);
                m_assignmentState[oparg] = false;
                break;
            case STORE_FAST:
                storeFast(oparg, opcodeIndex); break;
            case LOAD_FAST:
                loadFast(oparg, opcodeIndex); break;
            case UNPACK_SEQUENCE:
                m_comp->emit_unpack_sequence(oparg, stackInfo.top());
                decStack();
                incStack(oparg);
                intErrorCheck("failed to unpack");
                break;
            case UNPACK_EX: {
                size_t rightSize = oparg >> 8, leftSize = oparg & 0xff;
                m_comp->emit_unpack_sequence_ex(leftSize, rightSize, stackInfo.top());
                decStack();
                incStack(leftSize + rightSize + 1);
                intErrorCheck("failed to unpack");
                break;
            }
            case CALL_FUNCTION_KW: {
                // names is a tuple on the stack, should have come from a LOAD_CONST
                auto names = m_comp->emit_spill();
                decStack();    // names
                buildTuple(oparg);
                m_comp->emit_load_and_free_local(names);

                m_comp->emit_kwcall_with_tuple();
                decStack();// function & names

                errorCheck("kwcall failed", curByte);
                incStack();
                break;
            }
            case CALL_FUNCTION_EX:
                if (oparg & 0x01) {
                    // kwargs, then args, then function
                    m_comp->emit_call_kwargs();
                    decStack(3);
                }else{
                    m_comp->emit_call_args();
                    decStack(2);
                }

                errorCheck("call failed", curByte);
                incStack();
                break;
            case CALL_FUNCTION:
            {
                if (OPT_ENABLED(functionCalls) &&
                    stackInfo.size() >= (oparg + 1) &&
                    stackInfo.nth(oparg + 1).hasSource() &&
                    stackInfo.nth(oparg + 1).hasValue() &&
                    !mTracingEnabled)
                {
                    m_comp->emit_call_function_inline(oparg, stackInfo.nth(oparg + 1));
                    decStack(oparg + 1); // target + args(oparg)
                    errorCheck("inline function call failed", curByte);
                } else {
                    if (!m_comp->emit_call_function(oparg)) {
                        buildTuple(oparg);
                        incStack();
                        m_comp->emit_call_with_tuple();
                        decStack(2);// target + args
                        errorCheck("call n-function failed", curByte);
                    } else {
                        decStack(oparg + 1); // target + args(oparg)
                        errorCheck("call function failed", curByte);
                    }
                }
                incStack();
                break;
            }
            case BUILD_TUPLE:
                buildTuple(oparg);
                incStack();
                break;
            case BUILD_LIST:
                buildList(oparg);
                incStack();
                break;
            case BUILD_MAP:
                buildMap(oparg);
                incStack();
                break;
            case STORE_SUBSCR:
                if (OPT_ENABLED(knownStoreSubscr) && stackInfo.size() >= 3){
                    m_comp->emit_store_subscr(stackInfo.third(), stackInfo.second(), stackInfo.top());
                } else {
                    m_comp->emit_store_subscr();
                }
                decStack(3);
                intErrorCheck("store subscr failed", curByte);
                break;
            case DELETE_SUBSCR:
                decStack(2);
                m_comp->emit_delete_subscr();
                intErrorCheck("delete subscr failed", curByte);
                break;
            case BUILD_SLICE:
                assert(oparg == 2 || 3);
                if (oparg != 3) {
                    m_comp->emit_null();
                }
                m_comp->emit_build_slice();
                decStack(oparg);
                errorCheck("slice failed", curByte);
                incStack();
                break;
            case BUILD_SET:
                buildSet(oparg);
                break;
            case UNARY_POSITIVE:
                unaryPositive(opcodeIndex); break;
            case UNARY_NEGATIVE:
                unaryNegative(opcodeIndex); break;
            case UNARY_NOT:
                unaryNot(opcodeIndex); break;
            case UNARY_INVERT:
                m_comp->emit_unary_invert();
                decStack(1);
                errorCheck("unary invert failed", curByte);
                incStack();
                break;
            case BINARY_SUBSCR:
                if (stackInfo.size() >= 2) {
                    m_comp->emit_binary_subscr(byte, stackInfo.second(), stackInfo.top());
                    decStack(2);
                    errorCheck("optimized binary subscr failed", curByte);
                }
                else {
                    m_comp->emit_binary_object(byte);
                    decStack(2);
                    errorCheck("binary subscr failed", curByte);
                }
                incStack();
                break;
            case BINARY_ADD:
            case BINARY_TRUE_DIVIDE:
            case BINARY_FLOOR_DIVIDE:
            case BINARY_POWER:
            case BINARY_MODULO:
            case BINARY_MATRIX_MULTIPLY:
            case BINARY_LSHIFT:
            case BINARY_RSHIFT:
            case BINARY_AND:
            case BINARY_XOR:
            case BINARY_OR:
            case BINARY_MULTIPLY:
            case BINARY_SUBTRACT:
            case INPLACE_POWER:
            case INPLACE_MULTIPLY:
            case INPLACE_MATRIX_MULTIPLY:
            case INPLACE_TRUE_DIVIDE:
            case INPLACE_FLOOR_DIVIDE:
            case INPLACE_MODULO:
            case INPLACE_ADD:
            case INPLACE_SUBTRACT:
            case INPLACE_LSHIFT:
            case INPLACE_RSHIFT:
            case INPLACE_AND:
            case INPLACE_XOR:
            case INPLACE_OR:
                if (stackInfo.size() >= 2) {
                    m_comp->emit_binary_object(byte, stackInfo.second(), stackInfo.top());
                    decStack(2);
                    errorCheck("optimized binary op failed", curByte);
                }
                else {
                    m_comp->emit_binary_object(byte);
                    decStack(2);
                    errorCheck("binary op failed", curByte);
                }
                incStack();
                break;
            case RETURN_VALUE:
                returnValue(opcodeIndex); break;
            case EXTENDED_ARG:
            {
                curByte += SIZEOF_CODEUNIT;
                oparg = (oparg << 8) | GET_OPARG(curByte);
                byte = GET_OPCODE(curByte);

                goto processOpCode;
            }
            case MAKE_FUNCTION:
                makeFunction(oparg);
                break;
            case LOAD_DEREF:
                m_comp->emit_load_deref(oparg);
                errorCheck("load deref failed", curByte);
                incStack();
                break;
            case STORE_DEREF:
                m_comp->emit_store_deref(oparg);
                decStack();
                break;
            case DELETE_DEREF: m_comp->emit_delete_deref(oparg); break;
            case LOAD_CLOSURE:
                m_comp->emit_load_closure(oparg);
                errorCheck("load closure failed", curByte);
                incStack();
                break;
            case GET_ITER: {
                m_comp->emit_getiter();
                decStack();
                errorCheck("get iter failed", curByte);
                incStack();
                break;
            }
            case FOR_ITER:
            {
                auto postIterStack = ValueStack(m_stack);
                postIterStack.dec(1); // pop iter when stopiter happens
                size_t jumpTo = curByte + oparg + SIZEOF_CODEUNIT;
                if (OPT_ENABLED(inlineIterators) && !stackInfo.empty()){
                    auto iterator = stackInfo.top();
                    forIter(
                            jumpTo,
                            &iterator
                    );
                } else {
                    forIter(
                            jumpTo
                    );
                }
                m_offsetStack[jumpTo] = postIterStack;
                skipEffect = true; // has jump effect
                break;
            }
            case SET_ADD:
                // Calls set.update(TOS1[-i], TOS). Used to build sets.
                m_comp->lift_n_to_second(oparg);
                m_comp->emit_set_add();
                decStack(2); // set, value
                errorCheck("set update failed", curByte);
                incStack(1); // set
                m_comp->sink_top_to_n(oparg - 1);
                break;
            case MAP_ADD:
                // Calls dict.__setitem__(TOS1[-i], TOS1, TOS). Used to implement dict comprehensions.
                m_comp->lift_n_to_third(oparg + 1);
                m_comp->emit_map_add();
                decStack(3);
                errorCheck("map add failed", curByte);
                incStack();
                m_comp->sink_top_to_n(oparg - 1);
                break;
            case LIST_APPEND: {
                // Calls list.append(TOS1[-i], TOS).
                m_comp->lift_n_to_second(oparg);
                m_comp->emit_list_append();
                decStack(2); // list, value
                errorCheck("list append failed", curByte);
                incStack(1);
                m_comp->sink_top_to_n(oparg - 1);
                break;
            }
            case DICT_MERGE: {
                // Calls dict.update(TOS1[-i], TOS). Used to merge dicts.
                m_comp->lift_n_to_second(oparg);
                m_comp->emit_dict_merge();
                decStack(2); // list, value
                errorCheck("dict merge failed", curByte);
                incStack(1);
                m_comp->sink_top_to_n(oparg - 1);
                break;
            }
            case PRINT_EXPR:
                m_comp->emit_print_expr();
                decStack();
                intErrorCheck("print expr failed", curByte);
                break;
            case LOAD_CLASSDEREF:
                m_comp->emit_load_classderef(oparg);
                errorCheck("load classderef failed", curByte);
                incStack();
                break;
            case RAISE_VARARGS:
                // do raise (exception, cause)
                // We can be invoked with no args (bare raise), raise exception, or raise w/ exception and cause
                switch (oparg) { // NOLINT(hicpp-multiway-paths-covered)
                    case 0: m_comp->emit_null(); // NOLINT(bugprone-branch-clone)
                    case 1: m_comp->emit_null();
                    case 2:
                        decStack(oparg);
                        // raise exc
                        m_comp->emit_raise_varargs();
                        // returns 1 if we're doing a re-raise in which case we don't need
                        // to update the traceback.  Otherwise returns 0.
                        auto curHandler = currentHandler();
                        if (oparg == 0) {
                                // The stack actually ended up being empty - either because we didn't
                                // have any values, or the values were all non-objects that we could
                                // spill eagerly.
                                // TODO : Validate this logic.
                                m_comp->emit_branch(BranchAlways, curHandler->ErrorTarget);
                        }
                        else {
                            // if we have args we'll always return 0...
                            m_comp->emit_pop();
                            branchRaise("hit native error", curByte);
                        }
                        break;
                }
                break;
            case SETUP_FINALLY:
            {
                auto current = m_blockStack.back();
                auto jumpTo = oparg + curByte + SIZEOF_CODEUNIT;
                auto handlerLabel = m_comp->emit_define_label();

                auto newHandler = m_exceptionHandler.AddSetupFinallyHandler(
                        handlerLabel,
                        m_stack,
                        current.CurrentHandler,
                        ExceptionVars(m_comp, true),
                        jumpTo);

                auto newBlock = BlockInfo(
                        jumpTo,
                        SETUP_FINALLY,
                        newHandler);

                m_blockStack.push_back(newBlock);

                ValueStack newStack = ValueStack(m_stack);
                newStack.inc(6, StackEntryKind::STACK_KIND_VALUE);
                // This stack only gets used if an error occurs within the try:
                m_offsetStack[jumpTo] = newStack;
                skipEffect = true;
            }
            break;
            case RERAISE:{
                m_comp->emit_restore_err();
                decExcVars(3);
                unwindHandlers();
                skipEffect = true;
                break;
            }
            case POP_EXCEPT:
                popExcept();
                m_comp->pop_top();
                m_comp->pop_top();
                m_comp->pop_top();
                decStack(3);
                decExcVars(3);
                skipEffect = true;
                break;
            case POP_BLOCK:
                m_blockStack.pop_back();
                break;
            case SETUP_WITH:
                return {nullptr, IncompatibleOpcode_With};
            case YIELD_FROM:
            case YIELD_VALUE:
                return {nullptr, IncompatibleOpcode_Yield};

            case IMPORT_NAME:
                m_comp->emit_import_name(PyTuple_GetItem(mCode->co_names, oparg));
                decStack(2);
                errorCheck("import name failed", curByte);
                incStack();
                break;
            case IMPORT_FROM:
                m_comp->emit_import_from(PyTuple_GetItem(mCode->co_names, oparg));
                errorCheck("import from failed", curByte);
                incStack();
                break;
            case IMPORT_STAR:
                m_comp->emit_import_star();
                decStack(1);
                intErrorCheck("import star failed", curByte);
                break;
            case FORMAT_VALUE:
            {
                Local fmtSpec;
                if ((oparg & FVS_MASK) == FVS_HAVE_SPEC) {
                    // format spec
                    fmtSpec = m_comp->emit_spill();
                    decStack();
                }

                int whichConversion = oparg & FVC_MASK;

                decStack();
                if (whichConversion) {
                    // Save the original value so we can decref it...
                    m_comp->emit_dup();
                    auto tmp = m_comp->emit_spill();

                    // Convert it
                    switch (whichConversion) { // NOLINT(hicpp-multiway-paths-covered)
                        case FVC_STR:   m_comp->emit_pyobject_str();   break;
                        case FVC_REPR:  m_comp->emit_pyobject_repr();  break;
                        case FVC_ASCII: m_comp->emit_pyobject_ascii(); break;
                    }

                    // Decref the original value
                    m_comp->emit_load_and_free_local(tmp);
                    m_comp->emit_pop_top();

                    // Custom error handling in case we have a spilled spec
                    // we need to free as well.
                    auto noErr = m_comp->emit_define_label();
                    m_comp->emit_dup();
                    m_comp->emit_store_local(mErrorCheckLocal);
                    m_comp->emit_null();
                    m_comp->emit_branch(BranchNotEqual, noErr);

                    if ((oparg & FVS_MASK) == FVS_HAVE_SPEC) {
                        m_comp->emit_load_local(fmtSpec);
                        m_comp->emit_pop_top();
                    }

                    branchRaise("conversion failed", curByte);
                    m_comp->emit_mark_label(noErr);
                    m_comp->emit_load_local(mErrorCheckLocal);
                }

                if ((oparg & FVS_MASK) == FVS_HAVE_SPEC) {
                    // format spec
                    m_comp->emit_load_and_free_local(fmtSpec);
                    m_comp->emit_pyobject_format();

                    errorCheck("format object", curByte);
                }
                else if (!whichConversion) {
                    // TODO: This could also be avoided if we knew we had a string on the stack

                    // If we did a conversion we know we have a string...
                    // Otherwise we need to convert
                    m_comp->emit_format_value();
                }

                incStack();
                break;
            }
            case BUILD_STRING:
            {
                buildTuple(oparg);
                m_comp->emit_long_long(oparg);
                incStack(2);
                m_comp->emit_unicode_joinarray();
                decStack(2);
                errorCheck("build string (fstring) failed", curByte);
                incStack();
                break;
            }
            case BUILD_CONST_KEY_MAP:
            {
                /*
                 * The version of BUILD_MAP specialized for constant keys.
                 * Pops the top element on the stack which contains a tuple of keys,
                 * then starting from TOS1, pops count values to form values
                 * in the built dictionary.
                 */
                // spill TOP into keys and then build a tuple for stack
                buildTuple(oparg + 1);
                incStack();
                m_comp->emit_dict_build_from_map();
                decStack(1);
                errorCheck("dict map failed", curByte);
                incStack(1);
                break;
            }
            case LIST_EXTEND:
            {
                assert(oparg == 1); // always 1 in 3.9
                m_comp->lift_n_to_top(oparg);
                m_comp->emit_list_extend();
                decStack(2);
                errorCheck("list extend failed", curByte);
                incStack(1);
                // Takes list, value from stack and pushes list back onto stack
                break;
            }
            case DICT_UPDATE:
            {
                // Calls dict.update(TOS1[-i], TOS). Used to build dicts.
                assert(oparg == 1); // always 1 in 3.9
                m_comp->lift_n_to_top(oparg);
                m_comp->emit_dict_update();
                decStack(2); // dict, item
                errorCheck("dict update failed", curByte);
                incStack(1);
                break;
            }
            case SET_UPDATE:
            {
                // Calls set.update(TOS1[-i], TOS). Used to build sets.
                assert(oparg == 1); // always 1 in 3.9
                m_comp->lift_n_to_top(oparg);
                m_comp->emit_set_extend();
                decStack(2); // set, iterable
                errorCheck("set update failed", curByte);
                incStack(1); // set
                break;
            }
            case LIST_TO_TUPLE:
            {
                m_comp->emit_list_to_tuple();
                decStack(1); // list
                errorCheck("list to tuple failed", curByte);
                incStack(1); // tuple
                break;
            }
            case IS_OP:
            {
                m_comp->emit_is(oparg);
                decStack(2);
                errorCheck("is check failed", curByte);
                incStack(1);
                break;
            }
            case CONTAINS_OP:
            {
                // Oparg is 0 if "in", and 1 if "not in"
                if (oparg == 0)
                    m_comp->emit_in();
                else
                    m_comp->emit_not_in();
                decStack(2); // in/not in takes two
                incStack(); // pushes result
                break;
            }
            case LOAD_METHOD:
            {
                if (OPT_ENABLED(builtinMethods) && !stackInfo.empty() && stackInfo.top().hasValue() && stackInfo.top().Value->known() && !stackInfo.top().Value->needsGuard()){
                    m_comp->emit_builtin_method(PyTuple_GetItem(mCode->co_names, oparg), stackInfo.top().Value);
                } else {
                    m_comp->emit_dup(); // dup self as needs to remain on stack
                    m_comp->emit_load_method(PyTuple_GetItem(mCode->co_names, oparg));
                }
                incStack(1);
                break;
            }
            case CALL_METHOD:
            {
                if (!m_comp->emit_method_call(oparg)) {
                    buildTuple(oparg);
                    m_comp->emit_method_call_n();
                    decStack(2); // + method + name + nargs
                } else {
                    decStack(2 + oparg); // + method + name + nargs
                }
                errorCheck("failed to call method", curByte);
                incStack(); //result
                break;
            }
            case LOAD_ASSERTION_ERROR :
            {
                m_comp->emit_load_assertion_error();
                incStack();
                break;
            }
            default:
                return {nullptr, IncompatibleOpcode_Unknown};
        }
#ifdef DEBUG
        assert(skipEffect ||
            static_cast<size_t>(PyCompile_OpcodeStackEffect(byte, oparg)) == (m_stack.size() - curStackSize));
#endif
    }

    popExcVars();

    // label branch for error handling when we have no EH handlers, (return NULL).
    m_comp->emit_branch(BranchAlways, rootHandlerLabel);
    m_comp->emit_mark_label(rootHandlerLabel);

    m_comp->emit_null();

    auto finalRet = m_comp->emit_define_label();
    m_comp->emit_branch(BranchAlways, finalRet);

    // Return value from local
    m_comp->emit_mark_label(m_retLabel);
    m_comp->emit_load_local(m_retValue);

    // Final return position, pop frame and return
    m_comp->emit_mark_label(finalRet);

    if (mTracingEnabled) {
        m_comp->emit_trace_frame_exit();
    }
    if (mProfilingEnabled) {
        m_comp->emit_profile_frame_exit();
    }

    m_comp->emit_pop_frame();

    m_comp->emit_ret();
    auto code = m_comp->emit_compile();
    if (code != nullptr)
        return {code, Success};
    else
        return {nullptr, CompilationJitFailure};
}

void AbstractInterpreter::testBoolAndBranch(Local value, bool isTrue, Label target) {
    m_comp->emit_load_local(value);
    m_comp->emit_ptr(isTrue ? Py_False : Py_True);
    m_comp->emit_branch(BranchEqual, target);
}

void AbstractInterpreter::unaryPositive(size_t opcodeIndex) {
    m_comp->emit_unary_positive();
    decStack();
    errorCheck("unary positive failed", opcodeIndex);
    incStack();
}

void AbstractInterpreter::unaryNegative(size_t opcodeIndex) {
    m_comp->emit_unary_negative();
    decStack();
    errorCheck("unary negative failed", opcodeIndex);
    incStack();
}

void AbstractInterpreter::unaryNot(size_t opcodeIndex) {
    m_comp->emit_unary_not();
    decStack(1);
    errorCheck("unary not failed", opcodeIndex);
    incStack();
}

AbstactInterpreterCompileResult AbstractInterpreter::compile(PyObject* builtins, PyObject* globals, PyjionCodeProfile* profile, PgcStatus pgc_status) {
    AbstractInterpreterResult interpreted = interpret(builtins, globals, profile, pgc_status);
    if (interpreted != Success) {
        return {nullptr, interpreted};
    }
    try {
        return compileWorker(pgc_status);
    } catch (const exception& e){
#ifdef DEBUG
        printf("Error whilst compiling : %s\n", e.what());
#endif
        return {nullptr, CompilationException};
    }
}

bool AbstractInterpreter::canSkipLastiUpdate(size_t opcodeIndex) {
    switch (GET_OPCODE(opcodeIndex)) {
        case DUP_TOP:
        case DUP_TOP_TWO:
        case NOP:
        case ROT_TWO:
        case ROT_THREE:
        case ROT_FOUR:
        case POP_BLOCK:
        case POP_JUMP_IF_FALSE:
        case POP_JUMP_IF_TRUE:
        case JUMP_IF_FALSE_OR_POP:
        case JUMP_IF_TRUE_OR_POP:
        case CONTAINS_OP:
        case IS_OP:
        case LOAD_ASSERTION_ERROR:
        case END_ASYNC_FOR:
        case POP_TOP:
        case STORE_FAST:
        case LOAD_FAST:
        case LOAD_CONST:
        case JUMP_FORWARD:
        case JUMP_ABSOLUTE:
            return true;
    }

    return false;
}

void AbstractInterpreter::storeFast(size_t local, size_t opcodeIndex) {
    m_comp->emit_store_fast(local);
    decStack();
    m_assignmentState[local] = true;
}

void AbstractInterpreter::loadConst(ssize_t constIndex, size_t opcodeIndex) {
    auto constValue = PyTuple_GetItem(mCode->co_consts, constIndex);
    m_comp->emit_ptr(constValue);
    m_comp->emit_dup();
    m_comp->emit_incref();
    incStack();
}

void AbstractInterpreter::unwindHandlers(){
    // for each exception handler we need to load the exception
    // information onto the stack, and then branch to the correct
    // handler.  When we take an error we'll branch down to this
    // little stub and then back up to the correct handler.
    if (!m_exceptionHandler.Empty()) {
        // TODO: Unify the first handler with this loop
        for (auto handler: m_exceptionHandler.GetHandlers()) {
            //emitRaiseAndFree(handler);

            if (handler->HasErrorTarget()) {
                m_comp->emit_prepare_exception(
                        handler->ExVars.PrevExc,
                        handler->ExVars.PrevExcVal,
                        handler->ExVars.PrevTraceback
                );
                if (handler->IsTryFinally()) {
                    auto tmpEx = m_comp->emit_spill();

                    auto root = handler->GetRootOf();
                    auto& vars = handler->ExVars;

                    m_comp->emit_store_local(vars.FinallyValue);
                    m_comp->emit_store_local(vars.FinallyTb);

                    m_comp->emit_load_and_free_local(tmpEx);
                }
                m_comp->emit_branch(BranchAlways, handler->ErrorTarget);
            }
        }
    }
}

void AbstractInterpreter::returnValue(size_t opcodeIndex) {
    m_comp->emit_store_local(m_retValue);
    m_comp->emit_branch(BranchAlways, m_retLabel);
    decStack();
}

void AbstractInterpreter::forIter(size_t loopIndex, AbstractValueWithSources* iterator) {
    // dup the iter so that it stays on the stack for the next iteration
    m_comp->emit_dup(); // ..., iter -> iter, iter, ...

    // emits NULL on error, 0xff on StopIter and ptr on next
    if (iterator == nullptr)
        m_comp->emit_for_next(); // ..., iter, iter -> "next", iter, ...
    else
        m_comp->emit_for_next(*iterator);

    /* Start error branch */
    errorCheck("failed to fetch iter");
    /* End error branch */

    incStack(1);

    auto next = m_comp->emit_define_label();

    /* Start next iter branch */
    m_comp->emit_dup();
    m_comp->emit_ptr((void*)0xff);
    m_comp->emit_branch(BranchNotEqual, next);
    /* End next iter branch */

    /* Start stop iter branch */
    m_comp->emit_pop(); // Pop the 0xff StopIter value
    m_comp->emit_pop_top(); // POP and DECREF iter
    m_comp->emit_branch(BranchAlways, getOffsetLabel(loopIndex)); // Goto: post-stack
    /* End stop iter error branch */

    m_comp->emit_mark_label(next);
}

void AbstractInterpreter::forIter(size_t loopIndex) {
    forIter(loopIndex, nullptr);
}

void AbstractInterpreter::loadFast(size_t local, size_t opcodeIndex) {
    bool checkUnbound = m_assignmentState.find(local) == m_assignmentState.end() || !m_assignmentState.find(local)->second;
    loadFastWorker(local, checkUnbound, opcodeIndex);
    incStack();
}

void AbstractInterpreter::loadFastWorker(size_t local, bool checkUnbound, int curByte) {
    m_comp->emit_load_fast(local);

    // Check if arg is unbound, raises UnboundLocalError
    if (checkUnbound) {
        Label success = m_comp->emit_define_label();

        m_comp->emit_dup();
        m_comp->emit_store_local(mErrorCheckLocal);
        m_comp->emit_branch(BranchTrue, success);

        m_comp->emit_ptr(PyTuple_GetItem(mCode->co_varnames, local));

        m_comp->emit_unbound_local_check();

        branchRaise("unbound local", curByte);

        m_comp->emit_mark_label(success);
        m_comp->emit_load_local(mErrorCheckLocal);
    }

    m_comp->emit_dup();
    m_comp->emit_incref();
}

void AbstractInterpreter::jumpIfOrPop(bool isTrue, size_t opcodeIndex, size_t jumpTo) {
    if (jumpTo <= opcodeIndex){
        m_comp->emit_pending_calls();
    }
    auto target = getOffsetLabel(jumpTo);
    m_offsetStack[jumpTo] = ValueStack(m_stack);
    decStack();

    auto tmp = m_comp->emit_spill();
    auto noJump = m_comp->emit_define_label();
    auto willJump = m_comp->emit_define_label();

    // fast checks for true/false.
    testBoolAndBranch(tmp, isTrue, noJump);
    testBoolAndBranch(tmp, !isTrue, willJump);

    // Use PyObject_IsTrue
    m_comp->emit_load_local(tmp);
    m_comp->emit_is_true();

    raiseOnNegativeOne(opcodeIndex);

    m_comp->emit_branch(isTrue ? BranchFalse : BranchTrue, noJump);

    // Jumping, load the value back and jump
    m_comp->emit_mark_label(willJump);
    m_comp->emit_load_local(tmp);    // load the value back onto the stack
    m_comp->emit_branch(BranchAlways, target);

    // not jumping, load the value and dec ref it
    m_comp->emit_mark_label(noJump);
    m_comp->emit_load_local(tmp);
    m_comp->emit_pop_top();

    m_comp->emit_free_local(tmp);
}

void AbstractInterpreter::popJumpIf(bool isTrue, size_t opcodeIndex, size_t jumpTo) {
    if (jumpTo <= opcodeIndex){
        m_comp->emit_pending_calls();
    }
    auto target = getOffsetLabel(jumpTo);

    auto noJump = m_comp->emit_define_label();
    auto willJump = m_comp->emit_define_label();

    // fast checks for true/false...
    m_comp->emit_dup();
    m_comp->emit_ptr(isTrue ? Py_False : Py_True);
    m_comp->emit_branch(BranchEqual, noJump);

    m_comp->emit_dup();
    m_comp->emit_ptr(isTrue ? Py_True : Py_False);
    m_comp->emit_branch(BranchEqual, willJump);

    // Use PyObject_IsTrue
    m_comp->emit_dup();
    m_comp->emit_is_true();

    raiseOnNegativeOne(opcodeIndex);

    m_comp->emit_branch(isTrue ? BranchFalse : BranchTrue, noJump);

    // Branching, pop the value and branch
    m_comp->emit_mark_label(willJump);
    m_comp->emit_pop_top();
    m_comp->emit_branch(BranchAlways, target);

    // Not branching, just pop the value and fall through
    m_comp->emit_mark_label(noJump);
    m_comp->emit_pop_top();

    decStack();
    m_offsetStack[jumpTo] = ValueStack(m_stack);
}

void AbstractInterpreter::jumpAbsolute(size_t index, size_t from) {
    if (index <= from){
        m_comp->emit_pending_calls();
    }
    m_offsetStack[index] = ValueStack(m_stack);
    m_comp->emit_branch(BranchAlways, getOffsetLabel(index));
}

void AbstractInterpreter::jumpIfNotExact(size_t opcodeIndex, size_t jumpTo) {
    if (jumpTo <= opcodeIndex){
        m_comp->emit_pending_calls();
    }
    auto target = getOffsetLabel(jumpTo);
    m_comp->emit_compare_exceptions();
    decStack(2);
    errorCheck("failed to compare exceptions", opcodeIndex);
    m_comp->emit_ptr(Py_False);
    m_comp->emit_branch(BranchEqual, target);

    m_offsetStack[jumpTo] = ValueStack(m_stack);
}

// Unwinds exception handling starting at the current handler.  Emits the unwind for all
// of the current handlers until we reach one which will actually handle the current
// exception.
void AbstractInterpreter::unwindEh(ExceptionHandler* fromHandler, ExceptionHandler* toHandler) {
    auto cur = fromHandler;
    do {
        auto& exVars = cur->ExVars;

        if (exVars.PrevExc.is_valid()) {
            m_comp->emit_unwind_eh(exVars.PrevExc, exVars.PrevExcVal, exVars.PrevTraceback);
        }
        if (cur->IsRootHandler())
            break;
        cur = cur->BackHandler;
    } while (cur != nullptr && !cur->IsRootHandler() && cur != toHandler && !cur->IsTryExceptOrFinally());
}

inline ExceptionHandler* AbstractInterpreter::currentHandler() {
    return m_blockStack.back().CurrentHandler;
}

void AbstractInterpreter::popExcept() {
    // we made it to the end of an EH block w/o throwing,
    // clear the exception.
    auto block = m_blockStack.back();
    assert (block.CurrentHandler);
    unwindEh(block.CurrentHandler, block.CurrentHandler->BackHandler);
}

void AbstractInterpreter::enableTracing() {
    mTracingEnabled = true;
}

void AbstractInterpreter::disableTracing() {
    mTracingEnabled = false;
}

void AbstractInterpreter::enableProfiling() {
    mProfilingEnabled = true;
}

void AbstractInterpreter::disableProfiling() {
    mProfilingEnabled = false;
}