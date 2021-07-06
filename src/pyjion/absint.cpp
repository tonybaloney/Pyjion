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

#include "absint.h"
#include "pyjit.h"

#define PGC_READY() g_pyjionSettings.pgc && profile != nullptr

#define PGC_PROBE(count) pgcRequired = true; pgcSize = count;

#define PGC_UPDATE_STACK(count) \
    if (pgc_status == PgcStatus::CompiledWithProbes) {                      \
        for (int pos = 0; pos < (count) ; pos++) \
            lastState.push_n(pos,                                           \
                             lastState.fromPgc(                             \
                                pos,                                        \
                                profile->getType(curByte, pos),             \
                                profile->getValue(curByte, pos)));          \
        mStartStates[curByte] = lastState; \
    }

#define CAN_UNBOX() OPT_ENABLED(unboxing) && graph->isValid()
#define POP_VALUE() \
    lastState.pop(curByte, stackPosition); stackPosition++;
#define PUSH_INTERMEDIATE(ty) \
    lastState.push(AbstractValueWithSources((ty), newSource(new IntermediateSource(curByte))));
#define PUSH_INTERMEDIATE_TO(ty, to) \
    (to).push(AbstractValueWithSources((ty), newSource(new IntermediateSource(curByte))));

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
    for (auto source : m_sources) {
        delete source;
    }
}

AbstractInterpreterResult AbstractInterpreter::preprocess() {
    if (mCode->co_flags & (CO_COROUTINE | CO_ITERABLE_COROUTINE | CO_ASYNC_GENERATOR)) {
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

    py_oparg oparg;
    vector<bool> ehKind;
    vector<AbsIntBlockInfo> blockStarts;
    for (py_opindex curByte = 0; curByte < mSize; curByte += SIZEOF_CODEUNIT) {
        py_opindex opcodeIndex = curByte;
        py_opcode byte = GET_OPCODE(curByte);
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
            case YIELD_VALUE:
                m_yieldOffsets[opcodeIndex] = m_comp->emit_define_label();
                break;
            case YIELD_FROM:
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
        for (Py_ssize_t i = 0; i < PyTuple_Size(mCode->co_names); i++) {
            nameHashes[i] = PyObject_Hash(PyTuple_GetItem(mCode->co_names, i));
        }
    }
    return Success;
}

void AbstractInterpreter::setLocalType(size_t index, PyObject* val) {
    auto& lastState = mStartStates[0];
    if (val != nullptr) {
        auto localInfo = AbstractLocalInfo(new ArgumentValue(Py_TYPE(val), val));
        localInfo.ValueInfo.Sources = newSource(new LocalSource(index));
        lastState.replaceLocal(index, localInfo);
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
    deque<py_opindex> queue;
    queue.push_back(0);
    vector<const char*> utf8_names ;
    for (Py_ssize_t i = 0; i < PyTuple_Size(mCode->co_names); i++)
        utf8_names.push_back(PyUnicode_AsUTF8(PyTuple_GetItem(mCode->co_names, i)));

    do {
        py_oparg oparg;
        py_opindex cur = queue.front();
        queue.pop_front();
        for (py_opindex curByte = cur; curByte < mSize; curByte += SIZEOF_CODEUNIT) {
            // get our starting state when we entered this opcode
            InterpreterState lastState = mStartStates.find(curByte)->second;

            py_opindex opcodeIndex = curByte;
            py_opcode opcode = GET_OPCODE(curByte);
            bool pgcRequired = false;
            short pgcSize = 0;
            oparg = GET_OPARG(curByte);
        processOpCode:

            size_t curStackLen = lastState.stackSize();
            int jump = 0;
            bool skipEffect = false;
            size_t stackPosition = 0;

            switch (opcode) {
                case EXTENDED_ARG: {
                    curByte += SIZEOF_CODEUNIT;
                    oparg = (oparg << 8) | GET_OPARG(curByte);
                    opcode = GET_OPCODE(curByte);
                    updateStartState(lastState, curByte);
                    goto processOpCode;
                }
                case NOP:
                    break;
                case ROT_TWO: {
                    auto top = POP_VALUE();
                    auto second = POP_VALUE();

                    auto sources = AbstractSource::combine(top.Sources, second.Sources);
                    m_opcodeSources[opcodeIndex] = sources;
                    top.Sources = newSource(new IntermediateSource(curByte));
                    second.Sources = newSource(new IntermediateSource(curByte));
                    lastState.push(top);
                    lastState.push(second);
                    break;
                }
                case ROT_THREE: {
                    auto top = POP_VALUE();
                    auto second = POP_VALUE();
                    auto third = POP_VALUE();

                    auto sources = AbstractSource::combine(
                            top.Sources,
                            AbstractSource::combine(second.Sources, third.Sources));
                    m_opcodeSources[opcodeIndex] = sources;
                    top.Sources = newSource(new IntermediateSource(curByte));
                    second.Sources = newSource(new IntermediateSource(curByte));
                    third.Sources = newSource(new IntermediateSource(curByte));
                    lastState.push(top);
                    lastState.push(third);
                    lastState.push(second);
                    break;
                }
                case ROT_FOUR: {
                    auto top = POP_VALUE();
                    auto second = POP_VALUE();
                    auto third = POP_VALUE();
                    auto fourth = POP_VALUE();

                    auto sources = AbstractSource::combine(
                            top.Sources,
                            AbstractSource::combine(second.Sources,
                                                    AbstractSource::combine(third.Sources, fourth.Sources)));
                    m_opcodeSources[opcodeIndex] = sources;
                    top.Sources = newSource(new IntermediateSource(curByte));
                    second.Sources = newSource(new IntermediateSource(curByte));
                    third.Sources = newSource(new IntermediateSource(curByte));
                    fourth.Sources = newSource(new IntermediateSource(curByte));
                    lastState.push(top);
                    lastState.push(fourth);
                    lastState.push(third);
                    lastState.push(second);
                    break;
                }
                case POP_TOP:
                    POP_VALUE();
                    break;
                case DUP_TOP: {
                    auto top = POP_VALUE();
                    top.Sources = newSource(new IntermediateSource(curByte));
                    lastState.push(top);
                    top.Sources = newSource(new IntermediateSource(curByte));
                    lastState.push(top);
                    break;
                }
                case DUP_TOP_TWO: {
                    auto top = lastState[lastState.stackSize() - 1];
                    auto second = lastState[lastState.stackSize() - 2];
                    top.Sources = newSource(new IntermediateSource(curByte));
                    second.Sources = newSource(new IntermediateSource(curByte));
                    lastState.push(second);
                    lastState.push(top);
                    break;
                }
                case RERAISE: {
                    POP_VALUE();
                    POP_VALUE();
                    POP_VALUE();
                    break;
                }
                case LOAD_CONST: {
                    PyObject* item = PyTuple_GetItem(mCode->co_consts, oparg);
                    auto constSource = addConstSource(opcodeIndex, oparg, item);
                    auto value = AbstractValueWithSources(
                            toAbstract(item),
                            constSource
                    );
                    lastState.push(value);
                    break;
                }
                case LOAD_FAST: {
                    auto localSource = addLocalSource(opcodeIndex, oparg);
                    auto local = lastState.getLocal(oparg);
                    local.ValueInfo.Sources = localSource;
                    lastState.push(local.ValueInfo);
                    break;
                }
                case STORE_FAST: {
                    auto valueInfo = POP_VALUE();
                    m_opcodeSources[opcodeIndex] = valueInfo.Sources;
                    lastState.replaceLocal(oparg, AbstractLocalInfo(valueInfo, valueInfo.Value == &Undefined));
                }
                    break;
                case DELETE_FAST:
                    // We need to box any previous stores so we can delete them...  Otherwise
                    // we won't know if we should raise an unbound local error
                    lastState.replaceLocal(oparg, AbstractLocalInfo(&Undefined, true));
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
                    two = POP_VALUE();
                    one = POP_VALUE();

                    auto out = one.Value->binary(one.Sources, opcode, two);
                    PUSH_INTERMEDIATE(out)
                }
                break;
                case POP_JUMP_IF_FALSE: {
                    auto value = POP_VALUE();

                    // merge our current state into the branched to location...
                    if (updateStartState(lastState, oparg)) {
                        queue.push_back(oparg);
                    }

                    if (value.Value->isAlwaysFalse()) {
                        // We're always jumping, we don't need to process the following opcodes...
                        goto next;
                    }

                    // we'll continue processing after the jump with our new state...
                    break;
                }
                case POP_JUMP_IF_TRUE: {
                    auto value = POP_VALUE();

                    // merge our current state into the branched to location...
                    if (updateStartState(lastState, oparg)) {
                        queue.push_back(oparg);
                    }

                    if (value.Value->isAlwaysTrue()) {
                        // We're always jumping, we don't need to process the following opcodes...
                        goto next;
                    }

                    // we'll continue processing after the jump with our new state...
                    break;
                }
                case JUMP_IF_TRUE_OR_POP: {
                    auto curState = lastState;
                    auto top = POP_VALUE();
                    top.Sources = newSource(new IntermediateSource(curByte));
                    lastState.push(top);
                    if (updateStartState(lastState, oparg)) {
                        queue.push_back(oparg);
                    }
                    auto value = POP_VALUE();
                    if (value.Value->isAlwaysTrue()) {
                        // we always jump, no need to analyze the following instructions...
                        goto next;
                    }
                }
                    break;
                case JUMP_IF_FALSE_OR_POP: {
                    auto curState = lastState;
                    auto top = POP_VALUE();
                    top.Sources = newSource(new IntermediateSource(curByte));
                    lastState.push(top);
                    if (updateStartState(lastState, oparg)) {
                        queue.push_back(oparg);
                    }
                    auto value = POP_VALUE();
                    if (value.Value->isAlwaysFalse()) {
                        // we always jump, no need to analyze the following instructions...
                        goto next;
                    }
                }
                    break;
                case JUMP_IF_NOT_EXC_MATCH:
                    POP_VALUE();
                    POP_VALUE();

                    if (updateStartState(lastState, oparg)) {
                        queue.push_back(oparg);
                    }
                    goto next;
                case JUMP_ABSOLUTE:
                    if (updateStartState(lastState, oparg)) {
                        queue.push_back(oparg);
                    }
                    // Done processing this basic block, we'll need to see a branch
                    // to the following opcodes before we'll process them.
                    goto next;
                case JUMP_FORWARD:
                    if (updateStartState(lastState, (size_t) oparg + curByte + SIZEOF_CODEUNIT)) {
                        queue.push_back((size_t) oparg + curByte + SIZEOF_CODEUNIT);
                    }
                    // Done processing this basic block, we'll need to see a branch
                    // to the following opcodes before we'll process them.
                    goto next;
                case RETURN_VALUE: {
                    auto retValue = POP_VALUE();
                    mReturnValue = mReturnValue->mergeWith(retValue.Value);
                    }
                    goto next;
                case LOAD_NAME: {
                    // Used to load __name__ for a class def
                    PUSH_INTERMEDIATE(&Any);
                    break;
                }
                case STORE_NAME:
                    // Stores __module__, __doc__, __qualname__, as well as class/function defs sometimes
                    POP_VALUE();
                    break;
                case DELETE_NAME:
                    break;
                case LOAD_CLASSDEREF: {
                    PUSH_INTERMEDIATE(&Any);
                    break;
                }
                case LOAD_GLOBAL: {
                    auto name = PyTuple_GetItem(mCode->co_names, oparg);

                    PyObject* v = PyObject_GetItem(globals, name);
                    if (v == nullptr) {
                        PyErr_Clear();
                        v = PyObject_GetItem(builtins, name);
                        if (v == nullptr) {
                            PyErr_Clear();
                            // Neither. Maybe it'll appear at runtime!!
                            PUSH_INTERMEDIATE(&Any);
                        }
                        else {
                            // Builtin source
                            auto globalSource = addBuiltinSource(opcodeIndex, oparg, utf8_names[oparg], v);
                            auto builtinType = Py_TYPE(v);
                            AbstractValue* avk = avkToAbstractValue(GetAbstractType(builtinType));
                            auto value = AbstractValueWithSources(
                                    avk,
                                    globalSource
                            );
                            lastState.push(value);
                        }
                    } else {
                        // global source
                        auto globalSource = addGlobalSource(opcodeIndex, oparg, PyUnicode_AsUTF8(name), v);
                        auto value = AbstractValueWithSources(
                                &Any,
                                globalSource
                        );
                        lastState.push(value);
                    }
                    break;
                }
                case STORE_GLOBAL:
                    POP_VALUE();
                    break;
                case LOAD_ATTR: {
                    if (PGC_READY()) {
                        PGC_PROBE(1);
                        PGC_UPDATE_STACK(1);
                    }
                    POP_VALUE();
                    PUSH_INTERMEDIATE(&Any);
                    break;
                }
                case STORE_ATTR:
                    POP_VALUE();
                    POP_VALUE();
                    break;
                case DELETE_ATTR:
                    POP_VALUE();
                    break;
                case BUILD_LIST: {
                    for (int i = 0; i < oparg; i++) {
                        POP_VALUE();
                    }
                    PUSH_INTERMEDIATE(&List);
                    break;
                }
                case BUILD_TUPLE: {
                    for (int i = 0; i < oparg; i++) {
                        POP_VALUE();
                    }
                    PUSH_INTERMEDIATE(&Tuple);
                    break;
                }
                case BUILD_MAP: {
                    for (int i = 0; i < oparg; i++) {
                        POP_VALUE();
                        POP_VALUE();
                    }
                    PUSH_INTERMEDIATE(&Dict);
                    break;
                }
                case COMPARE_OP: {
                    if (PGC_READY()){
                        PGC_PROBE(2);
                        PGC_UPDATE_STACK(2);
                    }
                    POP_VALUE();
                    POP_VALUE();
                    PUSH_INTERMEDIATE(&Bool);
                }
                break;
                case IMPORT_NAME: {
                    POP_VALUE();
                    POP_VALUE();
                    PUSH_INTERMEDIATE(&Any);
                    break;
                }
                case IMPORT_FROM:
                case LOAD_CLOSURE: {
                    PUSH_INTERMEDIATE(&Any);
                    break;
                }
                case CALL_FUNCTION: {
                    if (PGC_READY()){
                        PGC_PROBE(oparg + 1);
                        PGC_UPDATE_STACK(oparg+1);
                    }
                    int argCnt = oparg & 0xff;
                    int kwArgCnt = (oparg >> 8) & 0xff;
                    for (int i = 0; i < argCnt; i++) {
                        POP_VALUE();
                    }
                    for (int i = 0; i < kwArgCnt; i++) {
                        POP_VALUE();
                        POP_VALUE();
                    }

                    // pop the function...
                    auto func = POP_VALUE();
                    auto source = AbstractValueWithSources(
                        avkToAbstractValue(knownFunctionReturnType(func)),
                        newSource(new LocalSource(curByte)));
                    lastState.push(source);
                    break;
                }
                case CALL_FUNCTION_KW: {
                    int na = oparg;

                    // Pop the names tuple
                    auto names = POP_VALUE();
                    assert(names.Value->kind() == AVK_Tuple);

                    for (int i = 0; i < na; i++) {
                        POP_VALUE();
                    }

                    // pop the function
                    POP_VALUE();

                    PUSH_INTERMEDIATE(&Any);
                    break;
                }
                case CALL_FUNCTION_EX: {
                    if (oparg & 0x01) {
                        // kwargs
                        POP_VALUE();
                    }

                    // call args (iterable)
                    POP_VALUE();
                    // function
                    POP_VALUE();

                    PUSH_INTERMEDIATE(&Any);
                    break;
                }
                case MAKE_FUNCTION: {
                    POP_VALUE(); // qual name
                    POP_VALUE(); // code

                    if (oparg & 0x08) {
                        // closure object
                        POP_VALUE();
                    }
                    if (oparg & 0x04) {
                        // annotations
                        POP_VALUE();
                    }
                    if (oparg & 0x02) {
                        // kw defaults
                        POP_VALUE();
                    }
                    if (oparg & 0x01) {
                        // defaults
                        POP_VALUE();
                    }

                    PUSH_INTERMEDIATE(&Function);
                    break;
                }
                case BUILD_SLICE: {
                    for (int i = 0; i < oparg; i++) {
                        POP_VALUE();
                    }
                    PUSH_INTERMEDIATE(&Slice);
                    break;
                }
                case UNARY_POSITIVE:
                case UNARY_NEGATIVE:
                case UNARY_INVERT:
                case UNARY_NOT: {
                    auto in = POP_VALUE();
                    auto out = in.Value->unary(in.Sources, opcode);
                    PUSH_INTERMEDIATE(out);
                    break;
                }
                case UNPACK_EX: {
                    POP_VALUE();
                    for (int i = 0; i < oparg >> 8; i++) {
                        PUSH_INTERMEDIATE(&Any);
                    }
                    PUSH_INTERMEDIATE(&List);
                    for (int i = 0; i < (oparg & 0xff); i++) {
                        PUSH_INTERMEDIATE(&Any);
                    }
                    break;
                }
                case UNPACK_SEQUENCE: {
                    if (PGC_READY()) {
                        PGC_PROBE(1);
                        PGC_UPDATE_STACK(1);
                    }
                    POP_VALUE();
                    for (int i = 0; i < oparg; i++) {
                        PUSH_INTERMEDIATE(&Any);
                    }
                    break;
                }
                case RAISE_VARARGS:
                    for (int i = 0; i < oparg; i++) {
                        POP_VALUE();
                    }
                    goto next;
                case STORE_SUBSCR:
                    if (PGC_READY()){
                        PGC_PROBE(3);
                        PGC_UPDATE_STACK(3);
                    }
                    POP_VALUE();
                    POP_VALUE();
                    POP_VALUE();
                    break;
                case DELETE_SUBSCR:
                    POP_VALUE();
                    POP_VALUE();
                    break;
                case BUILD_SET: {
                    for (int i = 0; i < oparg; i++) {
                        POP_VALUE();
                    }
                    PUSH_INTERMEDIATE(&Set);
                    break;
                }
                case STORE_DEREF:
                    // There is no tracking of cell variables.
                    POP_VALUE();
                    break;
                case LOAD_DEREF: {
                    // There is no tracking of cell variables.
                    PUSH_INTERMEDIATE(&Any);
                    break;
                }
                case DELETE_DEREF:
                    // Since cell variables are not tracked, no need to worry
                    // about their deletion.
                    break;
                case GET_ITER: {
                    auto iteratorType = POP_VALUE();
                    // TODO : Allow guarded/PGC sources to be optimized.
                    auto source = AbstractValueWithSources(
                            &Iterable,
                            newSource(new IteratorSource(iteratorType.Value->needsGuard() ? AVK_Any: iteratorType.Value->kind(), curByte)));
                    lastState.push(source);
                }
                    break;
                case FOR_ITER: {
                    // For branches out with the value consumed
                    auto leaveState = lastState;
                    auto iterator = leaveState.pop(curByte, 0); // Iterator
                    if (updateStartState(leaveState, (size_t) oparg + curByte + SIZEOF_CODEUNIT)) {
                        queue.push_back((size_t) oparg + curByte + SIZEOF_CODEUNIT);
                    }

                    // When we compile this we don't actually leave the value on the stack,
                    // but the sequence of opcodes assumes that happens.  to keep our stack
                    // properly balanced we match what's really going on.
                    PUSH_INTERMEDIATE(&Any);
                    break;
                }
                case POP_BLOCK: {
                    lastState.mStack = mStartStates[m_blockStarts[opcodeIndex]].mStack;
                    PUSH_INTERMEDIATE(&Any);
                    PUSH_INTERMEDIATE(&Any);
                    PUSH_INTERMEDIATE(&Any);
                }
                case POP_EXCEPT:
                    skipEffect = true;
                    break;
                case LOAD_BUILD_CLASS: {
                    // TODO: if we know this is __builtins__.__build_class__ we can push a special value
                    // to optimize the call.f
                    PUSH_INTERMEDIATE(&Any);
                    break;
                }
                case SET_ADD:
                    POP_VALUE();
                    break;
                case LIST_APPEND: {
                    // pop the value being stored off, leave list on stack
                    POP_VALUE();
                    POP_VALUE();
                    PUSH_INTERMEDIATE(&List);
                    break;
                }
                case MAP_ADD: {
                    // pop the value and key being stored off, leave list on stack
                    POP_VALUE();
                    POP_VALUE();
                    POP_VALUE();
                    PUSH_INTERMEDIATE(&Dict);
                }
                    break;
                case FORMAT_VALUE: {
                    if ((oparg & FVS_MASK) == FVS_HAVE_SPEC) {
                        // format spec
                        POP_VALUE();
                    }
                    POP_VALUE();
                    PUSH_INTERMEDIATE(&String);
                    break;
                }
                case BUILD_STRING: {
                    for (auto i = 0; i < oparg; i++) {
                        POP_VALUE();
                    }
                    PUSH_INTERMEDIATE(&String);
                    break;
                }
                case SETUP_ASYNC_WITH:
                case SETUP_WITH: {
                    auto finallyState = lastState;
                    PUSH_INTERMEDIATE_TO(&Any, finallyState);
                    if (updateStartState(finallyState, (size_t) oparg + curByte + SIZEOF_CODEUNIT)) {
                        jump = 1;
                        queue.push_back((size_t) oparg + curByte + SIZEOF_CODEUNIT);
                    }
                    PUSH_INTERMEDIATE(&Any);
                    goto next;
                }
                case SETUP_FINALLY: {
                    auto ehState = lastState;
                    // Except is entered with the exception object, traceback, and exception
                    // type.  TODO: We could type these stronger then they currently are typed
                    PUSH_INTERMEDIATE_TO(&Any, ehState);
                    PUSH_INTERMEDIATE_TO(&Any, ehState);
                    PUSH_INTERMEDIATE_TO(&Any, ehState);
                    PUSH_INTERMEDIATE_TO(&Any, ehState);
                    PUSH_INTERMEDIATE_TO(&Any, ehState);
                    PUSH_INTERMEDIATE_TO(&Any, ehState);
                    if (updateStartState(ehState, (size_t) oparg + curByte + SIZEOF_CODEUNIT)) {
                        queue.push_back((size_t)oparg + curByte + SIZEOF_CODEUNIT);
                    }
                    break;
                }
                case BUILD_CONST_KEY_MAP:
                    POP_VALUE(); //keys
                    for (auto i = 0; i < oparg; i++) {
                        POP_VALUE(); // values
                    }
                    PUSH_INTERMEDIATE(&Dict);
                    break;
                case LOAD_METHOD: {
                    auto object = POP_VALUE();
                    auto method = AbstractValueWithSources(
                            &Method,
                            newSource(new MethodSource(utf8_names[oparg], curByte)));
                    object.Sources = newSource(new IntermediateSource(curByte));
                    lastState.push(object);
                    lastState.push(method);
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
                        PGC_PROBE(1 + oparg);
                        PGC_UPDATE_STACK(1 + oparg);
                    }
                    for (int i = 0 ; i < oparg; i++) {
                        POP_VALUE();
                    }
                    auto method = POP_VALUE();
                    auto self = POP_VALUE();

                    if (method.hasValue() && method.Value->kind() == AVK_Method && self.Value->known()){
                        auto meth_source = dynamic_cast<MethodSource*>(method.Sources);
                        lastState.push(AbstractValueWithSources(avkToAbstractValue(avkToAbstractValue(self.Value->kind())->resolveMethod(meth_source->name())),
                                                                newSource(new IntermediateSource(curByte))));
                    } else {
                        PUSH_INTERMEDIATE(&Any);
                    }
                    break;
                }
                case IS_OP:
                case CONTAINS_OP:
                    POP_VALUE();
                    POP_VALUE();
                    PUSH_INTERMEDIATE(&Bool);
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
                }
                case LIST_EXTEND:
                {
                    POP_VALUE();
                    POP_VALUE();
                    PUSH_INTERMEDIATE(&List);
                    break;
                }
                case DICT_UPDATE:
                case SET_UPDATE:
                case DICT_MERGE:
                case PRINT_EXPR:
                {
                    POP_VALUE(); // value
                    break;
                }
                case LIST_TO_TUPLE:
                {
                    POP_VALUE(); // list
                    PUSH_INTERMEDIATE(&Tuple);
                    break;
                }
                case LOAD_ASSERTION_ERROR:
                {
                    PUSH_INTERMEDIATE(&Any);
                    break;
                }
                case IMPORT_STAR:
                    POP_VALUE();
                    break;
                case DELETE_GLOBAL:
                case SETUP_ANNOTATIONS:
                    break;  // No stack effect
                case YIELD_VALUE:
                    POP_VALUE();
                    PUSH_INTERMEDIATE(&Any);
                    break;  // No stack effect
                default:
                    PyErr_Format(PyExc_ValueError,
                                 "Unknown unsupported opcode: %d", opcode);
                    return IncompatibleOpcode_Unknown;
                    break;
            }
#ifdef DEBUG
            assert(skipEffect || 
                static_cast<size_t>(PyCompile_OpcodeStackEffectWithJump(opcode, oparg, jump)) == (lastState.stackSize() - curStackLen));
#endif
            updateStartState(lastState, curByte + SIZEOF_CODEUNIT);
            mStartStates[curByte].pgcProbeSize = pgcSize;
            mStartStates[curByte].requiresPgcProbe = pgcRequired;
        }

    next:;
    } while (!queue.empty());

    return Success;
}

bool AbstractInterpreter::updateStartState(InterpreterState& newState, py_opindex index) {
    auto initialState = mStartStates.find(index);
    if (initialState != mStartStates.end()) {
        return mergeStates(newState, initialState->second);
    }
    else {
        mStartStates[index] = newState;
        return true;
    }
}

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
        int ovf;
        long value = PyLong_AsLongAndOverflow(obj, &ovf);
        if (ovf || value > 2147483647 || value < -2147483647 )
            return &BigInteger;
        if (Py_SIZE(obj) < 4 && IS_SMALL_INT(value))
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
AbstractLocalInfo AbstractInterpreter::getLocalInfo(py_opindex byteCodeIndex, size_t localIndex) {
    return mStartStates[byteCodeIndex].getLocal(localIndex);
}

// Returns information about the stack at the specific byte code index.
InterpreterStack& AbstractInterpreter::getStackInfo(py_opindex byteCodeIndex) {
    return mStartStates[byteCodeIndex].mStack;
}

short AbstractInterpreter::pgcProbeSize(py_opindex byteCodeIndex) {
    return mStartStates[byteCodeIndex].pgcProbeSize;
}

bool AbstractInterpreter::pgcProbeRequired(py_opindex byteCodeIndex, PgcStatus status) {
    if (status == PgcStatus::Uncompiled)
        return mStartStates[byteCodeIndex].requiresPgcProbe;
    return false;
}

AbstractValue* AbstractInterpreter::getReturnInfo() {
    return mReturnValue;
}

AbstractSource* AbstractInterpreter::addLocalSource(py_opindex opcodeIndex, py_oparg localIndex) {
    auto store = m_opcodeSources.find(opcodeIndex);
    if (store == m_opcodeSources.end()) {
        return m_opcodeSources[opcodeIndex] = newSource(new LocalSource(opcodeIndex));
    }

    return store->second;
}

AbstractSource* AbstractInterpreter::addGlobalSource(py_opindex opcodeIndex, py_oparg constIndex, const char * name, PyObject* value) {
    auto store = m_opcodeSources.find(opcodeIndex);
    if (store == m_opcodeSources.end()) {
        return m_opcodeSources[opcodeIndex] = newSource(new GlobalSource(name, value, opcodeIndex));
    }

    return store->second;
}

AbstractSource* AbstractInterpreter::addBuiltinSource(py_opindex opcodeIndex, py_oparg constIndex, const char * name, PyObject* value) {
    auto store = m_opcodeSources.find(opcodeIndex);
    if (store == m_opcodeSources.end()) {
        return m_opcodeSources[opcodeIndex] = newSource(new BuiltinSource(name, value, opcodeIndex));
    }

    return store->second;
}

AbstractSource* AbstractInterpreter::addConstSource(py_opindex opcodeIndex, py_oparg constIndex, PyObject* value) {
    auto store = m_opcodeSources.find(opcodeIndex);
    if (store == m_opcodeSources.end()) {
        return m_opcodeSources[opcodeIndex] = newSource(new ConstSource(value, opcodeIndex));
    }

    return store->second;
}

// Checks to see if we have a non-zero error code on the stack, and if so,
// branches to the current error handler.  Consumes the error code in the process
void AbstractInterpreter::intErrorCheck(const char* reason, py_opindex curByte) {
    auto noErr = m_comp->emit_define_label();
    m_comp->emit_branch(BranchFalse, noErr);
    branchRaise(reason, curByte);
    m_comp->emit_mark_label(noErr);
}

// Checks to see if we have a null value as the last value on our stack
// indicating an error, and if so, branches to our current error handler.
void AbstractInterpreter::errorCheck(const char *reason, py_opindex curByte) {
    auto noErr = m_comp->emit_define_label();
    m_comp->emit_dup();
    m_comp->emit_store_local(mErrorCheckLocal);
    m_comp->emit_null();
    m_comp->emit_branch(BranchNotEqual, noErr);

    branchRaise(reason, curByte);
    m_comp->emit_mark_label(noErr);
    m_comp->emit_load_local(mErrorCheckLocal);
}

void AbstractInterpreter::invalidFloatErrorCheck(const char *reason, py_opindex curByte, py_opcode opcode) {
    auto noErr = m_comp->emit_define_label();
    Local errorCheckLocal = m_comp->emit_define_local(LK_Float);
    m_comp->emit_store_local(errorCheckLocal);
    m_comp->emit_load_local(errorCheckLocal);
    m_comp->emit_infinity();
    m_comp->emit_branch(BranchNotEqual, noErr);
    m_comp->emit_pyerr_setstring(PyExc_ZeroDivisionError, "division by zero/operation infinite");
    branchRaise(reason, curByte);

    m_comp->emit_mark_label(noErr);
    m_comp->emit_load_and_free_local(errorCheckLocal);
}

void AbstractInterpreter::invalidIntErrorCheck(const char *reason, py_opindex curByte, py_opcode opcode) {
    auto noErr = m_comp->emit_define_label();
    Local errorCheckLocal = m_comp->emit_define_local(LK_Int);
    m_comp->emit_store_local(errorCheckLocal);
    m_comp->emit_load_local(errorCheckLocal);
    m_comp->emit_infinity_long();
    m_comp->emit_branch(BranchNotEqual, noErr);
    m_comp->emit_pyerr_setstring(PyExc_ZeroDivisionError, "division by zero/operation infinite");
    branchRaise(reason, curByte);
    m_comp->emit_mark_label(noErr);
    m_comp->emit_load_and_free_local(errorCheckLocal);
}

Label AbstractInterpreter::getOffsetLabel(py_opindex jumpTo) {
    auto jumpToLabelIter = m_offsetLabels.find(jumpTo);
    Label jumpToLabel;
    if (jumpToLabelIter == m_offsetLabels.end()) {
        m_offsetLabels[jumpTo] = jumpToLabel = m_comp->emit_define_label();
    }
    else {
        jumpToLabel = jumpToLabelIter->second;
    }
    return jumpToLabel;
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

void AbstractInterpreter::branchRaise(const char *reason, py_opindex curByte, bool force) {
    auto ehBlock = currentHandler();
    auto& entryStack = ehBlock->EntryStack;

#ifdef DEBUG
    if (reason != nullptr) {
        m_comp->emit_debug_msg(reason);
    }
#endif

    m_comp->emit_eh_trace();

    if (mTracingEnabled)
        m_comp->emit_trace_exception();

    // number of stack entries we need to clear...
    auto count = static_cast<ssize_t>(m_stack.size() - entryStack.size());

    auto cur = m_stack.rbegin();
    for (; cur != m_stack.rend() && count >= 0; cur++) {
        if (*cur != STACK_KIND_OBJECT || force) {
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
        if (*cur != STACK_KIND_OBJECT || force) {
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

void AbstractInterpreter::buildTuple(py_oparg argCnt) {
    m_comp->emit_new_tuple(argCnt);
    if (argCnt != 0) {
        errorCheck("tuple build failed");
        m_comp->emit_tuple_store(argCnt);
        decStack(argCnt);
    }
}

void AbstractInterpreter::buildList(py_oparg argCnt) {
    m_comp->emit_new_list(argCnt);
    errorCheck("build list failed");
    if (argCnt != 0) {
        m_comp->emit_list_store(argCnt);
    }
    decStack(argCnt);
}

void AbstractInterpreter::extendListRecursively(Local list, py_oparg argCnt) {
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

void AbstractInterpreter::extendList(py_oparg argCnt) {
    assert(argCnt > 0);
    auto listTmp = m_comp->emit_spill();
    decStack();
    extendListRecursively(listTmp, argCnt);
    m_comp->emit_load_and_free_local(listTmp);
    incStack(1);
}

void AbstractInterpreter::buildSet(py_oparg argCnt) {
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

void AbstractInterpreter::buildMap(py_oparg argCnt) {
    m_comp->emit_new_dict(argCnt);
    errorCheck("build map failed");

    if (argCnt > 0) {
        auto map = m_comp->emit_spill();
        for (py_oparg curArg = 0; curArg < argCnt; curArg++) {
            m_comp->emit_load_local(map);

            m_comp->emit_dict_store();

            decStack(2);
            intErrorCheck("dict store failed");
        }
        m_comp->emit_load_and_free_local(map);
    }
}

void AbstractInterpreter::makeFunction(py_oparg oparg) {
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

void AbstractInterpreter::incStack(size_t size, LocalKind kind) {
    switch (kind){
        case LK_Int:
            m_stack.inc(size, STACK_KIND_VALUE_INT);
            break;
        case LK_Float:
            m_stack.inc(size, STACK_KIND_VALUE_FLOAT);
            break;
        case LK_Bool:
            m_stack.inc(size, STACK_KIND_VALUE_INT);
            break;
    }
}

// Checks to see if -1 is the current value on the stack, and if so, falls into
// the logic for raising an exception.  If not execution continues forward progress.
// Used for checking if an API reports an error (typically true/false/-1)
void AbstractInterpreter::raiseOnNegativeOne(py_opindex curByte) {
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

void AbstractInterpreter::dumpEscapedLocalsToFrame(const unordered_map<py_oparg, AbstractValueKind>& locals, py_opindex at){
    for (auto &loc: locals){
        m_comp->emit_load_local(m_fastNativeLocals[loc.first]);
        m_comp->emit_box(loc.second);
        m_comp->emit_store_fast(loc.first);
    }
}

void AbstractInterpreter::loadEscapedLocalsFromFrame(const unordered_map<py_oparg, AbstractValueKind>& locals, py_opindex at){
    Local failFlag = m_comp->emit_define_local();
    for (auto &loc: locals){
        m_comp->emit_load_fast(loc.first);
        m_comp->emit_unbox(loc.second, false, failFlag);
        m_comp->emit_store_local(m_fastNativeLocals[loc.first]);
    }
}

void AbstractInterpreter::escapeEdges(const vector<Edge>& edges, py_opindex curByte) {
    // Check if edges need boxing/unboxing
    // If none of the edges need escaping, skip
    bool needsEscapes = false;
    for (auto & edge : edges){
        if (edge.escaped == Unbox || edge.escaped == Box)
            needsEscapes = true;
    }
    if (!needsEscapes)
        return;

    // Escape edges
    Local escapeSuccess = m_comp->emit_define_local(LK_Int);
    Label noError = m_comp->emit_define_label();
    m_comp->emit_escape_edges(edges, escapeSuccess);
    m_comp->emit_load_and_free_local(escapeSuccess);
    m_comp->emit_branch(BranchFalse, noError);
    branchRaise("failed unboxing operation", curByte, true);
    m_comp->emit_mark_label(noError);
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

void AbstractInterpreter::emitPgcProbes(py_opindex curByte, size_t stackSize) {
    vector<Local> stack;
    stack.resize(stackSize);
    Local hasProbedFlag = m_comp->emit_define_local(LK_Bool);
    auto hasProbed = m_comp->emit_define_label();

    m_comp->emit_load_local(hasProbedFlag);
    m_comp->emit_branch(BranchTrue, hasProbed);
    
    for (size_t i = 0; i < stackSize; i++){
        stack[i] = m_comp->emit_define_local(stackEntryKindAsLocalKind(m_stack.peek(i)));
        m_comp->emit_store_local(stack[i]);
        if (m_stack.peek(i) == STACK_KIND_OBJECT) {
            m_comp->emit_pgc_profile_capture(stack[i], curByte, i);
        }
    }
    m_comp->emit_int(1);
    m_comp->emit_store_local(hasProbedFlag);
    // Recover the stack in the right order
    for (size_t i = stackSize; i > 0; --i){
        m_comp->emit_load_and_free_local(stack[i-1]);
    }

    m_comp->emit_mark_label(hasProbed);
}

bool canReturnInfinity(py_opcode opcode){
    switch(opcode){
        case BINARY_TRUE_DIVIDE:
        case BINARY_FLOOR_DIVIDE:
        case INPLACE_TRUE_DIVIDE:
        case INPLACE_FLOOR_DIVIDE:
        case BINARY_MODULO:
        case INPLACE_MODULO:
            return true;
    }
    return false;
}

void AbstractInterpreter::yieldJumps(){
    for (auto &pair: m_yieldOffsets){
        m_comp->emit_lasti();
        m_comp->emit_int(pair.first);
        m_comp->emit_branch(BranchEqual, pair.second);
    }
}

void AbstractInterpreter::yieldValue(py_opindex index, size_t stackSize, InstructionGraph* graph) {
    m_comp->emit_lasti_update(index);
    dumpEscapedLocalsToFrame(graph->getUnboxedFastLocals(), index);

    // incref and send top of stack
    m_comp->emit_dup();
    m_comp->emit_incref();
    m_comp->emit_store_local(m_retValue);
    // Stack has submitted result back. Store any other variables
    for (size_t i = (stackSize - 1); i > 0 ; --i) {
        m_comp->emit_store_in_frame_value_stack(i-1);
    }
    m_comp->emit_set_stacktop(stackSize-1);
    m_comp->emit_branch(BranchAlways, m_retLabel);
    // ^ Exit Frame || 🔽 Enter frame from next()
    m_comp->emit_mark_label(m_yieldOffsets[index]);
    loadEscapedLocalsFromFrame(graph->getUnboxedFastLocals(), index);
    for (size_t i = stackSize; i > 0 ; --i) {
        m_comp->emit_load_from_frame_value_stack(i);
    }
    m_comp->emit_shrink_stacktop_local(stackSize);
}

AbstactInterpreterCompileResult AbstractInterpreter::compileWorker(PgcStatus pgc_status, InstructionGraph* graph) {
    Label ok;
    m_comp->emit_lasti_init();
    m_comp->emit_push_frame();
    m_comp->emit_init_stacktop_local();

    if (mCode->co_flags & CO_GENERATOR){
        yieldJumps();
    }

    auto rootHandlerLabel = m_comp->emit_define_label();

    mExcVarsOnStack = m_comp->emit_define_local(LK_Int);
    m_comp->emit_int(0);
    m_comp->emit_store_local(mExcVarsOnStack);

    m_comp->emit_init_instr_counter();

    if (graph->isValid()) {
        for (auto &fastLocal : graph->getUnboxedFastLocals()) {
            m_fastNativeLocals[fastLocal.first] = m_comp->emit_define_local(fastLocal.second);
            m_fastNativeLocalKinds[fastLocal.first] = avkAsStackEntryKind(fastLocal.second);
        }
    }

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
    for (py_opindex curByte = 0; curByte < mSize; curByte += SIZEOF_CODEUNIT) {
        assert(curByte % SIZEOF_CODEUNIT == 0);
        auto op = graph->operator[](curByte);

        // opcodeIndex is the opcode position (matches the dis.dis() output)
        py_opindex opcodeIndex = curByte;

        // Get the opcode identifier (see opcode.h)
        py_opcode byte = op.opcode;

        // Get an additional oparg, see dis help for information on what each means
        py_oparg oparg = op.oparg;

        markOffsetLabel(curByte);
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
            m_comp->emit_lasti_update(curByte);
            if (mTracingEnabled){
                m_comp->emit_trace_line(mTracingInstrLowerBound, mTracingInstrUpperBound, mTracingLastInstr);
            }
        }
        auto stackInfo = getStackInfo(curByte);

        size_t curStackSize = m_stack.size();
        bool skipEffect = false;

        auto edges = graph->getEdges(curByte);
        if (g_pyjionSettings.pgc && pgcProbeRequired(curByte, pgc_status) && !(CAN_UNBOX() && op.escape)){
            emitPgcProbes(curByte, pgcProbeSize(curByte));
        }

        if (CAN_UNBOX()) {
            escapeEdges(edges, curByte);
        }

        switch (byte) {
            case NOP:
            case EXTENDED_ARG:
                break;
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
                if (stackInfo.size() >= 2){
                    if (CAN_UNBOX() && op.escape) {
                        m_comp->emit_compare_unboxed(oparg, stackInfo.second(), stackInfo.top());
                        decStack(2);
                        incStack(1, STACK_KIND_VALUE_INT);
                    } else if (OPT_ENABLED(internRichCompare)){
                        m_comp->emit_compare_known_object(oparg, stackInfo.second(), stackInfo.top());
                        decStack(2);
                        errorCheck("failed to compare", curByte);
                        incStack(1);
                    } else {
                        m_comp->emit_compare_object(oparg);
                        decStack(2);
                        errorCheck("failed to compare", curByte);
                        incStack(1);
                    }
                } else {
                    m_comp->emit_compare_object(oparg);
                    decStack(2);
                    errorCheck("failed to compare", curByte);
                    incStack(1);
                }

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
                if (CAN_UNBOX() && op.escape) {
                    unboxedPopJumpIf(byte != POP_JUMP_IF_FALSE, opcodeIndex, oparg);
                } else {
                    popJumpIf(byte != POP_JUMP_IF_FALSE, opcodeIndex, oparg);
                }
                break;
            case LOAD_NAME:
                if (OPT_ENABLED(hashedNames)){
                    m_comp->emit_load_name_hashed(PyTuple_GetItem(mCode->co_names, oparg), nameHashes[oparg]);
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
                if (OPT_ENABLED(loadAttr) && !stackInfo.empty()){
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
                if (CAN_UNBOX() && op.escape){
                    loadUnboxedConst(oparg, opcodeIndex);
                } else {
                    loadConst(oparg, opcodeIndex);
                }
                break;
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
                if (CAN_UNBOX() && op.escape){
                    // TODO : Decide if we need to store some junk value in the local?
                } else {
                    loadFastWorker(oparg, true, curByte);
                    m_comp->emit_pop_top();
                    m_comp->emit_delete_fast(oparg);
                }
                m_assignmentState[oparg] = false;
                break;
            case STORE_FAST:
                if (CAN_UNBOX() && op.escape){
                    storeFastUnboxed(oparg);
                } else {
                    m_comp->emit_store_fast(oparg);
                    decStack();
                }
                m_assignmentState[oparg] = true;
                break;
            case LOAD_FAST:
                if (CAN_UNBOX() && op.escape){
                    loadFastUnboxed(oparg, opcodeIndex);
                } else {
                    loadFast(oparg, opcodeIndex);
                }
                break;
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
                m_comp->emit_unary_positive();
                decStack();
                errorCheck("unary positive failed", opcodeIndex);
                incStack();
                break;
            case UNARY_NEGATIVE:
                m_comp->emit_unary_negative();
                decStack();
                errorCheck("unary negative failed", opcodeIndex);
                incStack();
                break;
            case UNARY_NOT:
                m_comp->emit_unary_not();
                decStack(1);
                errorCheck("unary not failed", opcodeIndex);
                incStack();
                break;
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
                    if (CAN_UNBOX() && op.escape) {
                        auto retKind = m_comp->emit_unboxed_binary_object(byte, stackInfo.second(), stackInfo.top());
                        decStack(2);
                        if (canReturnInfinity(byte)) {
                            switch (retKind) {
                                case LK_Float:
                                    invalidFloatErrorCheck("unboxed binary op failed", curByte, byte);
                                    break;
                                case LK_Int:
                                    invalidIntErrorCheck("unboxed binary op failed", curByte, byte);
                                    break;
                            }
                        }
                        incStack(1, retKind);
                    } else {
                        m_comp->emit_binary_object(byte, stackInfo.second(), stackInfo.top());
                        decStack(2);
                        errorCheck("optimized binary op failed", curByte);
                        incStack();
                    }
                }
                else {
                    m_comp->emit_binary_object(byte);
                    decStack(2);
                    errorCheck("binary op failed", curByte);
                    incStack();
                }
                break;
            case RETURN_VALUE:
                returnValue(opcodeIndex); break;
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
                py_opindex jumpTo = curByte + oparg + SIZEOF_CODEUNIT;
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
                py_opindex jumpTo = oparg + curByte + SIZEOF_CODEUNIT;
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
                newStack.inc(6, STACK_KIND_OBJECT);
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
            case YIELD_VALUE: {
                yieldValue(op.index, curStackSize, graph);
                skipEffect = true;
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

void AbstractInterpreter::updateIntermediateSources(){
    for (auto & s : m_sources){
        if (s->isIntermediate()){
            auto interSource = reinterpret_cast<IntermediateSource*>(s);
            if (interSource->markForSingleUse()){
                m_unboxableProducers[interSource->producer()] = true;
            }
        }
    }
}

InstructionGraph* AbstractInterpreter::buildInstructionGraph() {
    unordered_map<py_opindex, const InterpreterStack*> stacks;
    for (const auto &state: mStartStates){
        stacks[state.first] = &state.second.mStack;
    }
    auto* graph = new InstructionGraph(mCode, stacks);
    updateIntermediateSources();
    return graph;
}

AbstactInterpreterCompileResult AbstractInterpreter::compile(PyObject* builtins, PyObject* globals, PyjionCodeProfile* profile, PgcStatus pgc_status) {
    AbstractInterpreterResult interpreted = interpret(builtins, globals, profile, pgc_status);
    if (interpreted != Success) {
        return {nullptr, interpreted};
    }
    try {
        auto instructionGraph = buildInstructionGraph();
        auto result = compileWorker(pgc_status, instructionGraph);
        if (g_pyjionSettings.graph) {
            result.instructionGraph = instructionGraph->makeGraph(PyUnicode_AsUTF8(mCode->co_name));

#ifdef DUMP_INSTRUCTION_GRAPHS
            printf("%s", PyUnicode_AsUTF8(result.instructionGraph));
#endif
        }

        delete instructionGraph;
        return result;
    } catch (const exception& e){
#ifdef DEBUG
        printf("Error whilst compiling : %s\n", e.what());
#endif
        return {nullptr, CompilationException};
    }
}

bool AbstractInterpreter::canSkipLastiUpdate(py_opindex opcodeIndex) {
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

void AbstractInterpreter::loadConst(py_oparg constIndex, py_opindex opcodeIndex) {
    auto constValue = PyTuple_GetItem(mCode->co_consts, constIndex);
    m_comp->emit_ptr(constValue);
    m_comp->emit_dup();
    m_comp->emit_incref();
    incStack();
}

void AbstractInterpreter::loadUnboxedConst(py_oparg constIndex, py_opindex opcodeIndex) {
    auto constValue = PyTuple_GetItem(mCode->co_consts, constIndex);
    auto abstractT = GetAbstractType(constValue->ob_type);
    switch(abstractT){
        case AVK_Float:
            m_comp->emit_float(PyFloat_AS_DOUBLE(constValue));
            incStack(1, STACK_KIND_VALUE_FLOAT);
            break;
        case AVK_Integer:
            m_comp->emit_long_long(PyLong_AsLongLong(constValue));
            incStack(1, STACK_KIND_VALUE_INT);
            break;
        case AVK_Bool:
            if (constValue == Py_True)
                m_comp->emit_int(1);
            else
                m_comp->emit_int(0);
            incStack(1, STACK_KIND_VALUE_INT);
            break;
    }
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

void AbstractInterpreter::returnValue(py_opindex opcodeIndex) {
    m_comp->emit_store_local(m_retValue);
    m_comp->emit_branch(BranchAlways, m_retLabel);
    decStack();
}

void AbstractInterpreter::forIter(py_opindex loopIndex, AbstractValueWithSources* iterator) {
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
    m_comp->emit_pyerr_clear();
    m_comp->emit_branch(BranchAlways, getOffsetLabel(loopIndex)); // Goto: post-stack
    /* End stop iter error branch */

    m_comp->emit_mark_label(next);
}

void AbstractInterpreter::forIter(py_opindex loopIndex) {
    forIter(loopIndex, nullptr);
}

void AbstractInterpreter::loadFast(py_oparg local, py_opindex opcodeIndex) {
    bool checkUnbound = m_assignmentState.find(local) == m_assignmentState.end() || !m_assignmentState.find(local)->second;
    loadFastWorker(local, checkUnbound, opcodeIndex);
    incStack();
}

void AbstractInterpreter::loadFastUnboxed(py_oparg local, py_opindex opcodeIndex) {
    bool checkUnbound = m_assignmentState.find(local) == m_assignmentState.end() || !m_assignmentState.find(local)->second;
    assert(!checkUnbound);
    m_comp->emit_load_local(m_fastNativeLocals[local]);
    incStack(1, m_fastNativeLocalKinds[local]);
}
void AbstractInterpreter::storeFastUnboxed(py_oparg local) {
    m_comp->emit_store_local(m_fastNativeLocals[local]);
    decStack();
}

void AbstractInterpreter::loadFastWorker(py_oparg local, bool checkUnbound, py_opindex curByte) {
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

void AbstractInterpreter::jumpIfOrPop(bool isTrue, py_opindex opcodeIndex, py_oparg jumpTo) {
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

void AbstractInterpreter::popJumpIf(bool isTrue, py_opindex opcodeIndex, py_oparg jumpTo) {
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

void AbstractInterpreter::unboxedPopJumpIf(bool isTrue, py_opindex opcodeIndex, py_oparg jumpTo) {
    if (jumpTo <= opcodeIndex){
        m_comp->emit_pending_calls();
    }
    auto target = getOffsetLabel(jumpTo);

    m_comp->emit_branch(isTrue ? BranchTrue : BranchFalse, target);

    decStack();
    m_offsetStack[jumpTo] = ValueStack(m_stack);
}

void AbstractInterpreter::jumpAbsolute(py_opindex index, py_opindex from) {
    if (index <= from){
        m_comp->emit_pending_calls();
    }
    m_offsetStack[index] = ValueStack(m_stack);
    m_comp->emit_branch(BranchAlways, getOffsetLabel(index));
}

void AbstractInterpreter::jumpIfNotExact(py_opindex opcodeIndex, py_oparg jumpTo) {
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

// We want to maintain a mapping between locations in the Python byte code
// and generated locations in the code.  So for each Python byte code index
// we define a label in the generated code.  If we ever branch to a specific
// opcode then we'll branch to the generated label.
void AbstractInterpreter::markOffsetLabel(py_opindex index) {
    auto existingLabel = m_offsetLabels.find(index);
    if (existingLabel != m_offsetLabels.end()) {
        m_comp->emit_mark_label(existingLabel->second);
    }
    else {
        auto label = m_comp->emit_define_label();
        m_offsetLabels[index] = label;
        m_comp->emit_mark_label(label);
    }
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