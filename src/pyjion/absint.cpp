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
#include <set>
#include <unordered_map>
#include <algorithm>

#include "absint.h"
#include "pyjit.h"
#include "pycomp.h"
#include "attrtable.h"

#define PGC_READY() g_pyjionSettings.pgc&& profile != nullptr

#define PGC_PROBE(count) \
    pgcRequired = true;  \
    pgcSize = count;

#define PGC_UPDATE_STACK(count)                                        \
    if (pgc_status == PgcStatus::CompiledWithProbes) {                 \
        for (int pos = 0; pos < (count); pos++)                        \
            lastState.push_n(pos,                                      \
                             lastState.fromPgc(                        \
                                     pos,                              \
                                     profile->getType(curByte, pos),   \
                                     profile->getKind(curByte, pos))); \
        mStartStates[curByte] = lastState;                             \
    }

#define CAN_UNBOX() OPT_ENABLED(Unboxing) && graph->isValid()
#define POP_VALUE()                        \
    lastState.pop(curByte, stackPosition); \
    stackPosition++;
#define PUSH_INTERMEDIATE(ty) \
    lastState.push(AbstractValueWithSources((ty), newSource(new IntermediateSource(curByte))));
#define PUSH_INTERMEDIATE_TO(ty, to) \
    (to).push(AbstractValueWithSources((ty), newSource(new IntermediateSource(curByte))));
#define FLAG_OPT_USAGE(opt) (optimizationsMade = optimizationsMade | (opt))

#define CUR_HANDLER m_blockStack.back().CurrentHandler

AbstractInterpreter::AbstractInterpreter(PyCodeObject* code) : mCode(code) {
    mGlobalsVersion = 0;
    mBuiltinsVersion = 0;
    mByteCode = (_Py_CODEUNIT*) PyBytes_AS_STRING(code->co_code);
    mSize = PyBytes_Size(code->co_code);
    mTracingEnabled = false;
    mProfilingEnabled = false;
    m_comp = nullptr;
    initStartingState();
}

AbstractInterpreter::~AbstractInterpreter() {
    // clean up any dynamically allocated objects...
    for (auto source : m_sources) {
        delete source;
    }
}

AbstractInterpreterPreprocessResult AbstractInterpreter::preprocess() {
    if (mCode->co_flags & (CO_COROUTINE | CO_ITERABLE_COROUTINE | CO_ASYNC_GENERATOR)) {
        // Don't compile co-routines or generators.  We can't rely on
        // detecting yields because they could be optimized out.
        return {IncompatibleCompilerFlags};
    }

    for (int i = 0; i < mCode->co_argcount; i++) {
        // all parameters are initially definitely assigned
        m_assignmentState[i] = true;
    }
    if (mSize >= g_pyjionSettings.codeObjectSizeLimit) {
        return {IncompatibleSize};
    }

    py_oparg oparg;
    vector<bool> ehKind;
    AbstractBlockList blockStarts;
    for (py_opindex curByte = 0; curByte < mSize; curByte += SIZEOF_CODEUNIT) {
        py_opindex opcodeIndex = curByte;
        auto byte = GET_OPCODE(curByte);
        oparg = GET_OPARG(curByte);
    processOpCode:
        while (!blockStarts.empty() &&
               opcodeIndex >= blockStarts[blockStarts.size() - 1].BlockEnd) {
            auto blockStart = blockStarts.back();
            blockStarts.pop_back();
            m_blockStarts[opcodeIndex] = blockStart.BlockStart;
        }

        switch (byte) {// NOLINT(hicpp-multiway-paths-covered)
            case EXTENDED_ARG: {
                curByte += SIZEOF_CODEUNIT;
                oparg = (oparg << 8) | GET_OPARG(curByte);
                byte = GET_OPCODE(curByte);
                goto processOpCode;
            }

            // Opcodes that push basic blocks
            case SETUP_FINALLY:
                if (!g_pyjionSettings.exceptionHandling)
                    return {IncompatibleOpcode_WithExcept};
            case SETUP_WITH:
            case SETUP_ASYNC_WITH:
            case FOR_ITER:
                blockStarts.emplace_back(opcodeIndex, jumpsTo(byte, oparg, curByte));
                ehKind.emplace_back(true);
                break;
            // Opcodes that pop basic blocks
            case POP_EXCEPT:
            case POP_BLOCK: {
                if (!blockStarts.empty()) {
                    auto blockStart = blockStarts.back();
                    blockStarts.pop_back();
                    m_blockStarts[opcodeIndex] = blockStart.BlockStart;
                }
                break;
            }
            case YIELD_FROM:
                return {IncompatibleOpcode_Yield};
            case DELETE_FAST:
                if (oparg < mCode->co_argcount) {
                    // this local is deleted, so we need to check for assignment
                    m_assignmentState[oparg] = false;
                }
                break;

            case LOAD_GLOBAL: {
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
#ifdef DEBUG_VERBOSE
                    printf("Skipping function because it contains frame globals.");
#endif
                    return {IncompatibleFrameGlobal};
                }
            } break;
        }
    }
    if (OPT_ENABLED(HashedNames)) {
        for (Py_ssize_t i = 0; i < PyTuple_Size(mCode->co_names); i++) {
            nameHashes[i] = PyObject_Hash(PyTuple_GetItem(mCode->co_names, i));
        }
    }
    return {Success};
}

void AbstractInterpreter::setLocalType(size_t index, PyObject* val) {
    auto& lastState = mStartStates[0];
    if (val != nullptr) {
        auto localInfo = AbstractLocalInfo(new ArgumentValue(Py_TYPE(val), val, GetAbstractType(Py_TYPE(val), val)));
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
AbstractInterpreter::interpret(PyObject* builtins, PyObject* globals, PyjionCodeProfile* profile, PgcStatus pgc_status) {
    auto preprocessResult = preprocess();
    if (preprocessResult.result != Success) {
        return preprocessResult.result;
    }
    // walk all the blocks in the code one by one, analyzing them, and enqueing any
    // new blocks that we encounter from branches.
    deque<py_opindex> queue;
    queue.emplace_back(0);
    vector<const char*> utf8_names;

    // Save the version tags of globals and builtins for cached LOAD_GLOBAL
    mGlobalsVersion = ((PyDictObject*) globals)->ma_version_tag;
    mBuiltinsVersion = ((PyDictObject*) builtins)->ma_version_tag;

    for (Py_ssize_t i = 0; i < PyTuple_Size(mCode->co_names); i++)
        utf8_names.emplace_back(PyUnicode_AsUTF8(PyTuple_GetItem(mCode->co_names, i)));
    // Keep a list of SETUP_FINALLY instructions and their offsets, to push the exception values onto the stack
    unordered_map<py_opindex, py_opindex> tryExceptMarkers;
    do {
        py_oparg oparg;
        py_opindex cur = queue.front();
        queue.pop_front();
        for (py_opindex curByte = cur; curByte < mSize; curByte += SIZEOF_CODEUNIT) {
            // get our starting state when we entered this opcode
            InterpreterState lastState = mStartStates.find(curByte)->second;

            py_opindex opcodeIndex = curByte;
            auto opcode = GET_OPCODE(curByte);
            bool pgcRequired = false;
            short pgcSize = 0;
            oparg = GET_OPARG(curByte);
        processOpCode:
            size_t curStackLen = lastState.stackSize();
            int jump = 0;
            bool skipEffect = false;
            size_t stackPosition = 0;
            if (tryExceptMarkers.find(curByte) != tryExceptMarkers.end()) {
                lastState.push(AbstractValueWithSources(&Any, newSource(new IntermediateSource(tryExceptMarkers[curByte]))));
                lastState.push(AbstractValueWithSources(&Any, newSource(new IntermediateSource(tryExceptMarkers[curByte]))));
                lastState.push(AbstractValueWithSources(&Any, newSource(new IntermediateSource(tryExceptMarkers[curByte]))));
                lastState.push(AbstractValueWithSources(&Any, newSource(new IntermediateSource(tryExceptMarkers[curByte]))));
                lastState.push(AbstractValueWithSources(&Any, newSource(new IntermediateSource(tryExceptMarkers[curByte]))));
                lastState.push(AbstractValueWithSources(&Any, newSource(new IntermediateSource(tryExceptMarkers[curByte]))));
                skipEffect = true;
            }

            switch (opcode) {
                case EXTENDED_ARG: {
                    curByte += SIZEOF_CODEUNIT;
                    opcodeIndex += SIZEOF_CODEUNIT; // TODO : Merge these variables, they are duplicative.
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
                    auto top = POP_VALUE();
                    auto second = POP_VALUE();
                    top.Sources = newSource(new IntermediateSource(curByte));
                    second.Sources = newSource(new IntermediateSource(curByte));
                    lastState.push(second);
                    lastState.push(top);
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
                            constSource);
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
                    if (PGC_READY() && g_pyjionSettings.optimizationLevel >= 2) {
                        PGC_PROBE(1);
                        PGC_UPDATE_STACK(1);
                    }
                    auto valueInfo = POP_VALUE();
                    m_opcodeSources[opcodeIndex] = valueInfo.Sources;
                    lastState.replaceLocal(oparg, AbstractLocalInfo(valueInfo, valueInfo.Value == &Undefined));
                } break;
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
                    if (PGC_READY()) {
                        PGC_PROBE(2);
                        PGC_UPDATE_STACK(2);
                    }
                    two = POP_VALUE();
                    one = POP_VALUE();

                    auto out = one.Value->binary(one.Sources, opcode, two);
                    PUSH_INTERMEDIATE(out)
                } break;
                case POP_JUMP_IF_FALSE: {
                    auto value = POP_VALUE();

                    // merge our current state into the branched to location...
                    if (updateStartState(lastState, jumpsTo(opcode, oparg, opcodeIndex))) {
                        queue.emplace_back(jumpsTo(opcode, oparg, opcodeIndex));
                    }

                    // we'll continue processing after the jump with our new state...
                    break;
                }
                case POP_JUMP_IF_TRUE: {
                    auto value = POP_VALUE();

                    // merge our current state into the branched to location...
                    if (updateStartState(lastState, jumpsTo(opcode, oparg, opcodeIndex))) {
                        queue.emplace_back(jumpsTo(opcode, oparg, opcodeIndex));
                    }

                    // we'll continue processing after the jump with our new state...
                    break;
                }
                case JUMP_IF_TRUE_OR_POP: {
                    auto curState = lastState;
                    auto top = POP_VALUE();
                    top.Sources = newSource(new IntermediateSource(curByte));
                    lastState.push(top);
                    if (updateStartState(lastState, jumpsTo(opcode, oparg, opcodeIndex))) {
                        queue.emplace_back(jumpsTo(opcode, oparg, opcodeIndex));
                    }
                    auto value = POP_VALUE();
                } break;
                case JUMP_IF_FALSE_OR_POP: {
                    auto curState = lastState;
                    auto top = POP_VALUE();
                    top.Sources = newSource(new IntermediateSource(curByte));
                    lastState.push(top);
                    if (updateStartState(lastState, jumpsTo(opcode, oparg, opcodeIndex))) {
                        queue.emplace_back(jumpsTo(opcode, oparg, opcodeIndex));
                    }
                    auto value = POP_VALUE();
                } break;
                case JUMP_IF_NOT_EXC_MATCH:
                    POP_VALUE();
                    POP_VALUE();

                    if (updateStartState(lastState, jumpsTo(opcode, oparg, opcodeIndex))) {
                        queue.emplace_back(jumpsTo(opcode, oparg, opcodeIndex));
                    }
                    goto next_block;
                case JUMP_ABSOLUTE:
                    if (updateStartState(lastState, jumpsTo(opcode, oparg, opcodeIndex))) {
                        queue.emplace_back(jumpsTo(opcode, oparg, opcodeIndex));
                    }
                    // Done processing this basic block, we'll need to see a branch
                    // to the following opcodes before we'll process them.
                    goto next_block;
                case JUMP_FORWARD:
                    if (updateStartState(lastState, jumpsTo(opcode, oparg, opcodeIndex))) {
                        queue.emplace_back(jumpsTo(opcode, oparg, opcodeIndex));
                    }
                    // Done processing this basic block, we'll need to see a branch
                    // to the following opcodes before we'll process them.
                    goto next_block;
                case RETURN_VALUE: {
                    auto retValue = POP_VALUE();
                }
                    goto next_block;
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
                            lastResolvedGlobal[oparg] = nullptr;
                        } else {
                            // Builtin source
                            auto globalSource = addBuiltinSource(opcodeIndex, oparg, utf8_names[oparg], v);
                            auto builtinType = Py_TYPE(v);
                            AbstractValue* avk = avkToAbstractValue(GetAbstractType(builtinType));
                            auto value = AbstractValueWithSources(
                                    avk,
                                    globalSource);
                            lastState.push(value);
                            lastResolvedGlobal[oparg] = v;
                        }
                    } else {
                        // global source
                        auto globalSource = addGlobalSource(opcodeIndex, oparg, PyUnicode_AsUTF8(name), v);
                        AbstractValue* avk = new GlobalValue(Py_TYPE(v), v, GetAbstractType(Py_TYPE(v), v));
                        auto value = AbstractValueWithSources(
                                avk,
                                globalSource);
                        lastState.push(value);
                        lastResolvedGlobal[oparg] = v;
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
                    auto obj = POP_VALUE();
                    if (OPT_ENABLED(AttrTypeTable)){
                        if (obj.hasValue() && obj.Value->known()) {
                            auto avk = g_attrTable->getAttr(obj.Value->pythonType(), utf8_names[oparg]);
                            if (avk == AVK_Any){
                                PUSH_INTERMEDIATE(&Any);
                            } else {
                                auto val = new PgcValue(GetPyType(avk), avk);
                                PUSH_INTERMEDIATE(val);
                            }
                        } else {
                            PUSH_INTERMEDIATE(&Any);
                        }
                    } else {
                        PUSH_INTERMEDIATE(&Any);
                    }
                    break;
                }
                case STORE_ATTR: {
                    auto name = PyTuple_GetItem(mCode->co_names, oparg);
                    auto obj = POP_VALUE();
                    auto value = POP_VALUE();
                    if (OPT_ENABLED(AttrTypeTable)){
                        if (obj.hasValue() && obj.Value->known() && value.hasValue() && value.Value->known()) {
                            if (g_attrTable->captureStoreAttr(obj.Value->pythonType(), utf8_names[oparg], value.Value->kind()) != 0){
#ifdef DEBUG_VERBOSE
                                printf("!Switching value of %s.%s to %u at %s:%d\n", obj.Value->pythonType()->tp_name, utf8_names[oparg], value.Value->kind(), PyUnicode_AsUTF8(mCode->co_name), curByte);
#endif
                            };
                        }
                    }
                }
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
                    if (PGC_READY()) {
                        PGC_PROBE(2);
                        PGC_UPDATE_STACK(2);
                    }
                    auto two = POP_VALUE();
                    auto one = POP_VALUE();
                    auto out = one.Value->compare(one.Sources, opcode, two);
                    PUSH_INTERMEDIATE(out)
                } break;
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
                    if (PGC_READY()) {
                        PGC_PROBE(oparg + 1);
                        PGC_UPDATE_STACK(oparg + 1);
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
                    POP_VALUE();// qual name
                    POP_VALUE();// code

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
                    auto container = POP_VALUE();
                    for (int i = 0; i < oparg; i++) {
                        PUSH_INTERMEDIATE(container.Value->item(container.Sources));
                    }
                    break;
                }
                case RAISE_VARARGS:
                    for (int i = 0; i < oparg; i++) {
                        POP_VALUE();
                    }
                    break;
                case STORE_SUBSCR:
                    if (PGC_READY()) {
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
                    auto in = POP_VALUE();
                    auto out = in.Value->iter(in.Sources);
                    PUSH_INTERMEDIATE(out);
                    break;
                } break;
                case FOR_ITER: {
                    // For branches out with the value consumed
                    auto leaveState = lastState;
                    auto iterator = leaveState.pop(curByte, 0);// Iterator
                    if (updateStartState(leaveState, jumpsTo(opcode, oparg, opcodeIndex))) {
                        queue.emplace_back(jumpsTo(opcode, oparg, opcodeIndex));
                    }

                    iterator.Sources = newSource(new IntermediateSource(curByte));
                    lastState.push(iterator);
                    auto out = iterator.Value->next(iterator.Sources);
                    PUSH_INTERMEDIATE(out);
                    skipEffect = true;
                    break;
                }
                case POP_BLOCK:
                    lastState.mStack = mStartStates[m_blockStarts[opcodeIndex]].mStack;
                    goto next_block;
                case POP_EXCEPT:
                    POP_VALUE();
                    POP_VALUE();
                    POP_VALUE();
                    lastState.mStack = mStartStates[m_blockStarts[opcodeIndex]].mStack;
                    goto next_block;
                case LOAD_BUILD_CLASS: {
                    PUSH_INTERMEDIATE(&Function);
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
                } break;
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
                    if (updateStartState(finallyState, jumpsTo(opcode, oparg, opcodeIndex))) {
                        jump = 1;
                        queue.emplace_back(jumpsTo(opcode, oparg, opcodeIndex));
                    }
                    PUSH_INTERMEDIATE(&Any);
                    break;
                }
                case SETUP_FINALLY: {
                    // Capture where the except block starts.
                    tryExceptMarkers[jumpsTo(opcode, oparg, opcodeIndex)] = curByte;
                    auto ehState = lastState;
                    if (updateStartState(ehState, jumpsTo(opcode, oparg, opcodeIndex))) {
                        queue.emplace_back(jumpsTo(opcode, oparg, opcodeIndex));
                    }
                    break;
                }
                case BUILD_CONST_KEY_MAP:
                    POP_VALUE();//keys
                    for (auto i = 0; i < oparg; i++) {
                        POP_VALUE();// values
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
                case CALL_METHOD: {
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
                    if (PGC_READY()) {
                        PGC_PROBE(1 + oparg);
                        PGC_UPDATE_STACK(1 + oparg);
                    }
                    for (int i = 0; i < oparg; i++) {
                        POP_VALUE();
                    }
                    auto method = POP_VALUE();
                    auto self = POP_VALUE();

                    if (method.hasValue() && method.Value->kind() == AVK_Method && self.Value->known()) {
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
                    return IncompatibleOpcode_WithExcept;// not implemented
                }
                case LIST_EXTEND: {
                    POP_VALUE();
                    POP_VALUE();
                    PUSH_INTERMEDIATE(&List);
                    break;
                }
                case DICT_UPDATE:
                case SET_UPDATE:
                case DICT_MERGE:
                case PRINT_EXPR: {
                    POP_VALUE();// value
                    break;
                }
                case LIST_TO_TUPLE: {
                    POP_VALUE();// list
                    PUSH_INTERMEDIATE(&Tuple);
                    break;
                }
                case LOAD_ASSERTION_ERROR: {
                    PUSH_INTERMEDIATE(&Any);
                    break;
                }
                case IMPORT_STAR:
                    POP_VALUE();
                    break;
                case DELETE_GLOBAL:
                case SETUP_ANNOTATIONS:
                    break;// No stack effect
                case YIELD_VALUE:
                    POP_VALUE();
                    PUSH_INTERMEDIATE(&Any);
                    break;// No stack effect
                case GEN_START:
                    skipEffect = true;
                    break;
                default:
                    return IncompatibleOpcode_Unknown;
                    break;
            }
            if (!skipEffect) {
                if (static_cast<size_t>(PyCompile_OpcodeStackEffectWithJump(opcode, oparg, jump)) != (lastState.stackSize() - curStackLen)) {
#ifdef DEBUG_VERBOSE
                    printf("Opcode %s at %d should have stack effect %d, but was %lu\n", opcodeName(opcode), curByte, PyCompile_OpcodeStackEffectWithJump(opcode, oparg, jump), (lastState.stackSize() - curStackLen));
#endif
                    return CompilationStackEffectFault;
                }
            }
            updateStartState(lastState, curByte + SIZEOF_CODEUNIT);
            mStartStates[curByte].pgcProbeSize = pgcSize;
            mStartStates[curByte].requiresPgcProbe = pgcRequired;
        }

    next_block:;
    } while (!queue.empty());

    return Success;
}

bool AbstractInterpreter::updateStartState(InterpreterState& newState, py_opindex index) {
    auto initialState = mStartStates.find(index);
    if (initialState != mStartStates.end()) {
        return mergeStates(newState, initialState->second);
    } else {
        mStartStates[index] = newState;
        return true;
    }
}

bool AbstractInterpreter::mergeStates(InterpreterState& newState, InterpreterState& mergeTo) {
    bool changed = false;
    if (mergeTo.mLocals != newState.mLocals) {
        // need to merge locals...
        if (mergeTo.localCount() != newState.localCount()) {
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
    } else {
        size_t max = mergeTo.stackSize();
        ;
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

AbstractValue* AbstractInterpreter::toAbstract(PyObject* obj) {
    if (obj == Py_None) {
        return &None;
    } else if (PyLong_CheckExact(obj)) {
        if (IntegerValue::isBig(obj))
            return &BigInteger;
        else
            return &Integer;
    } else if (PyUnicode_Check(obj)) {
        return &String;
    } else if (PyList_CheckExact(obj)) {
        return &List;
    } else if (PyDict_CheckExact(obj)) {
        return &Dict;
    } else if (PyTuple_CheckExact(obj)) {
        if (PyTuple_GET_SIZE(obj) == 0) {
            return &Tuple;
        }
        auto t = Py_TYPE(PyTuple_GET_ITEM(obj, 0));
        for (int i = 1; i < PyTuple_GET_SIZE(obj); i++) {
            if (Py_TYPE(PyTuple_GET_ITEM(obj, i)) != t)
                return &Tuple;
        }
        auto abstractType = GetAbstractType(Py_TYPE(PyTuple_GET_ITEM(obj, 0)), PyTuple_GET_ITEM(obj, 0));
        switch (abstractType) {
            case AVK_String:
                return &TupleOfString;
            case AVK_BigInteger:
                return &TupleOfBigInteger;
            case AVK_Integer:
                return &TupleOfInteger;
            case AVK_Float:
                return &TupleOfFloat;
            default:
                return &Tuple;
        }
    } else if (PyBool_Check(obj)) {
        return &Bool;
    } else if (PyFloat_CheckExact(obj)) {
        return &Float;
    } else if (PyBytes_CheckExact(obj)) {
        return &Bytes;
    } else if (PySet_Check(obj)) {
        return &Set;
    } else if (PyComplex_CheckExact(obj)) {
        return &Complex;
    } else if (PyFunction_Check(obj)) {
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

AbstractSource* AbstractInterpreter::addLocalSource(py_opindex opcodeIndex, py_oparg localIndex) {
    auto store = m_opcodeSources.find(opcodeIndex);
    if (store == m_opcodeSources.end()) {
        return m_opcodeSources[opcodeIndex] = newSource(new LocalSource(opcodeIndex));
    }

    return store->second;
}

AbstractSource* AbstractInterpreter::addGlobalSource(py_opindex opcodeIndex, py_oparg constIndex, const char* name, PyObject* value) {
    auto store = m_opcodeSources.find(opcodeIndex);
    if (store == m_opcodeSources.end()) {
        return m_opcodeSources[opcodeIndex] = newSource(new GlobalSource(name, value, opcodeIndex));
    }

    return store->second;
}

AbstractSource* AbstractInterpreter::addBuiltinSource(py_opindex opcodeIndex, py_oparg constIndex, const char* name, PyObject* value) {
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
void AbstractInterpreter::intErrorCheck(ExceptionHandler* handler, const char* reason, const char* context, py_opindex curByte) {
    auto noErr = m_comp->emit_define_label();
    m_comp->emit_branch(BranchFalse, noErr);
    branchRaise(handler, reason, context, curByte);
    m_comp->emit_mark_label(noErr);
}

// Checks to see if we have a null value as the last value on our stack
// indicating an error, and if so, branches to our current error handler.
void AbstractInterpreter::errorCheck(ExceptionHandler* handler, const char* reason, const char* context, py_opindex curByte) {
    auto noErr = m_comp->emit_define_label();
    m_comp->emit_dup();
    m_comp->emit_store_local(mErrorCheckLocal);
    m_comp->emit_null();
    m_comp->emit_branch(BranchNotEqual, noErr);

    branchRaise(handler, reason, context, curByte);
    m_comp->emit_mark_label(noErr);
    m_comp->emit_load_local(mErrorCheckLocal);
}

void AbstractInterpreter::invalidFloatErrorCheck(ExceptionHandler* handler, const char* reason, py_opindex curByte, py_opcode opcode) {
    auto noErr = m_comp->emit_define_label();
    Local errorCheckLocal = m_comp->emit_define_local(LK_Float);
    m_comp->emit_store_local(errorCheckLocal);
    m_comp->emit_load_local(errorCheckLocal);
    m_comp->emit_infinity();
    m_comp->emit_branch(BranchNotEqual, noErr);
    m_comp->emit_pyerr_setstring(PyExc_ZeroDivisionError, "division by zero/operation infinite");
    branchRaise(handler, reason, "", curByte);

    m_comp->emit_mark_label(noErr);
    m_comp->emit_load_and_free_local(errorCheckLocal);
}

void AbstractInterpreter::invalidIntErrorCheck(ExceptionHandler* handler, const char* reason, py_opindex curByte, py_opcode opcode, void* exc, const char* message) {
    auto noErr = m_comp->emit_define_label();
    Local errorCheckLocal = m_comp->emit_define_local(LK_Int);
    m_comp->emit_store_local(errorCheckLocal);
    m_comp->emit_load_local(errorCheckLocal);
    m_comp->emit_infinity_long();
    m_comp->emit_branch(BranchNotEqual, noErr);
    m_comp->emit_pyerr_setstring(exc, message);
    branchRaise(handler, reason, "", curByte);
    m_comp->emit_mark_label(noErr);
    m_comp->emit_load_and_free_local(errorCheckLocal);
}

Label OffsetMap::get(py_opindex jumpTo) {
    auto jumpToLabelIter = m_offsetLabels.find(jumpTo);
    Label jumpToLabel;
    if (jumpToLabelIter == m_offsetLabels.end()) {
        m_offsetLabels[jumpTo] = jumpToLabel = m_comp->emit_define_label();
    } else {
        jumpToLabel = jumpToLabelIter->second;
    }
    return jumpToLabel;
}


void AbstractInterpreter::branchRaise(ExceptionHandler* handler, const char* reason, const char* context, py_opindex curByte, bool force, bool trace) {
    auto& entryStack = handler->EntryStack;

#ifdef DEBUG_VERBOSE
    if (reason != nullptr) {
        m_comp->emit_debug_fault(reason, context, curByte);
    }
#endif

    if (trace)
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
        } else {
            break;
        }
    }

    if (!count || count < 0) {
        // No values on the stack, we can just branch directly to the raise label
        m_comp->emit_branch(BranchAlways, handler->ErrorTarget);
        return;
    }

    while (m_raiseAndFree.size() <= handler->RaiseAndFreeId) {
        m_raiseAndFree.emplace_back();
    }

    vector<Label>& labels = m_raiseAndFree[handler->RaiseAndFreeId];

    for (auto i = labels.size(); i < count; i++) {
        labels.emplace_back(m_comp->emit_define_label());
    }
    while (m_raiseAndFreeLocals.size() <= count) {
        m_raiseAndFreeLocals.emplace_back(m_comp->emit_define_local());
    }

    // continue walking our stack iterator
    for (auto i = 0; i < count; cur++, i++) {
        if (*cur != STACK_KIND_OBJECT || force) {
            // pop off the stack value...
            m_comp->emit_pop();

            // and store null into our local that needs to be freed
            m_comp->emit_null();
            m_comp->emit_store_local(m_raiseAndFreeLocals[i]);
        } else {
            m_comp->emit_store_local(m_raiseAndFreeLocals[i]);
        }
    }
    m_comp->emit_branch(BranchAlways, handler->ErrorTarget);
}

void AbstractInterpreter::buildTuple(ExceptionHandler* handler, py_oparg argCnt) {
    m_comp->emit_new_tuple(argCnt);
    if (argCnt != 0) {
        errorCheck(handler, "tuple build failed");
        m_comp->emit_tuple_store(argCnt);
        decStack(argCnt);
    }
}

void AbstractInterpreter::decStack(size_t size) {
    m_stack.dec(size);
}

void AbstractInterpreter::incStack(size_t size, StackEntryKind kind) {
    m_stack.inc(size, kind);
}

void AbstractInterpreter::incStack(size_t size, LocalKind kind) {
    switch (kind) {
        case LK_Int:
            m_stack.inc(size, STACK_KIND_VALUE_INT);
            break;
        case LK_Float:
            m_stack.inc(size, STACK_KIND_VALUE_FLOAT);
            break;
        case LK_Bool:
            m_stack.inc(size, STACK_KIND_VALUE_INT);
            break;
        default:
            m_stack.inc(size, STACK_KIND_OBJECT);
    }
}

// Checks to see if -1 is the current value on the stack, and if so, falls into
// the logic for raising an exception.  If not execution continues forward progress.
// Used for checking if an API reports an error (typically true/false/-1)
void AbstractInterpreter::raiseOnNegativeOne(ExceptionHandler* handler, py_opindex curByte) {
    m_comp->emit_dup();
    m_comp->emit_int(-1);

    auto noErr = m_comp->emit_define_label();
    m_comp->emit_branch(BranchNotEqual, noErr);
    // we need to issue a leave to clear the stack as we may have
    // values on the stack...

    m_comp->emit_pop();
    branchRaise(handler, "last operation failed", "", curByte);
    m_comp->emit_mark_label(noErr);
}

void AbstractInterpreter::escapeEdges(ExceptionHandler* handler, const vector<Edge>& edges, py_opindex curByte) {
    // Check if edges need boxing/unboxing
    // If none of the edges need escaping, skip
    bool needsEscapes = false;
    for (auto& edge : edges) {
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
    branchRaise(handler, "failed unboxing operation", "", curByte, true);
    m_comp->emit_mark_label(noError);
}

void AbstractInterpreter::emitPgcProbes(py_opindex curByte, size_t stackSize, const vector<Edge>& edges) {
    vector<Local> stack = vector<Local>(stackSize);
    assert(edges.size() >= stackSize);
    Local hasProbedFlag = m_comp->emit_define_local(LK_Bool);
    auto hasProbed = m_comp->emit_define_label();

    m_comp->emit_load_local(hasProbedFlag);
    m_comp->emit_branch(BranchTrue, hasProbed);

    for (size_t i = 0; i < stackSize; i++) {
        stack[i] = m_comp->emit_define_local(stackEntryKindAsLocalKind(m_stack.peek(i)));
        m_comp->emit_store_local(stack[i]);
        if (m_stack.peek(i) == STACK_KIND_OBJECT) {
            if (edges[i].escaped == NoEscape || edges[i].escaped == Unbox)
                m_comp->emit_pgc_profile_capture(stack[i], curByte, i);
        }
    }
    m_comp->emit_int(1);
    m_comp->emit_store_local(hasProbedFlag);
    // Recover the stack in the right order
    for (size_t i = stackSize; i > 0; --i) {
        m_comp->emit_load_and_free_local(stack[i - 1]);
    }

    m_comp->emit_mark_label(hasProbed);
}

bool canReturnInfinity(py_opcode opcode) {
    switch (opcode) {// NOLINT(hicpp-multiway-paths-covered)
        case BINARY_TRUE_DIVIDE:
        case BINARY_FLOOR_DIVIDE:
        case INPLACE_TRUE_DIVIDE:
        case INPLACE_FLOOR_DIVIDE:
        case BINARY_MODULO:
        case INPLACE_MODULO:
        case BINARY_POWER:
        case INPLACE_POWER:
            return true;
    }
    return false;
}

AbstactInterpreterCompileWorkerResult AbstractInterpreter::compileWorker(PgcStatus pgc_status, InstructionGraph* graph, IPythonCompiler* comp) {// NOLINT(readability-function-cognitive-complexity)
    Label ok;
    OptimizationFlags optimizationsMade = OptimizationFlags();
    OffsetMap offsets = OffsetMap(comp);
    m_comp = comp;
    Label m_retLabel = m_comp->emit_define_label();
    Local m_retValue = m_comp->emit_define_local();
    mErrorCheckLocal = m_comp->emit_define_local();
    // Tracks the state of the stack when we perform a branch.  We copy the existing state to the map and
    // reload it when we begin processing at the stack.
    unordered_map<py_opindex, ValueStack> m_offsetStack;
    // m_blockStack is like Python's f_blockstack which lives on the frame object, except we only maintain
    // it at compile time.  Blocks are pushed onto the stack when we enter a loop, the start of a try block,
    // or into a finally or exception handler.  Blocks are popped as we leave those protected regions.
    // When we pop a block associated with a try body we transform it into the correct block for the handler
    BlockStack m_blockStack;
    m_raiseAndFreeLocals.clear();
    m_raiseAndFree.clear();
    m_stack.clear();
    offsetLabels yieldOffsets;
    m_comp->emit_lasti_init();
    auto rootHandlerLabel = m_comp->emit_define_label();

    if (m_comp->emit_push_frame()) {
        FLAG_OPT_USAGE(InlineFramePushPop);
    }
    // Almost certainly will be used, tracking its usage is inefficient
    if (OPT_ENABLED(InlineDecref)) {
        FLAG_OPT_USAGE(InlineDecref);
    }

    if (graph->isValid()) {
        for (auto& fastLocal : graph->getUnboxedFastLocals()) {
            m_fastNativeLocals[fastLocal.first] = m_comp->emit_define_local(fastLocal.second);
            m_fastNativeLocalKinds[fastLocal.first] = avkAsStackEntryKind(fastLocal.second);
        }
    }

    if (mCode->co_flags & CO_GENERATOR) {
        for (py_opindex curByte = 0; curByte < mSize; curByte += SIZEOF_CODEUNIT) {
            assert(curByte % SIZEOF_CODEUNIT == 0);
            auto op = graph->operator[](curByte);
            if (op.opcode == YIELD_VALUE){
                yieldOffsets[op.index] = m_comp->emit_define_label();
                m_comp->emit_lasti();
                m_comp->emit_int(op.index / 2);
                m_comp->emit_branch(BranchEqual, yieldOffsets[op.index]);
            }
        }
    }

    m_comp->emit_init_instr_counter();

    if (mTracingEnabled) {
        // push initial trace on entry to frame
        m_comp->emit_trace_frame_entry();

        mTracingLastInstr = m_comp->emit_define_local(LK_Int);
        m_comp->emit_int(-1);
        m_comp->emit_store_local(mTracingLastInstr);
    }
    if (mProfilingEnabled) { m_comp->emit_profile_frame_entry(); }

    ExceptionHandlerManager m_exceptionHandler;

    // Push a catch-all error handler onto the handler list
    auto rootHandler = m_exceptionHandler.SetRootHandler(rootHandlerLabel);

    // Push root block to stack, has no end offset
    m_blockStack.emplace_back(BlockInfo(-1, NOP, rootHandler));

    // Loop through all opcodes in this frame
    for (py_opindex curByte = 0; curByte < mSize; curByte += SIZEOF_CODEUNIT) {
        assert(curByte % SIZEOF_CODEUNIT == 0);
        auto op = graph->operator[](curByte);

        // opcodeIndex is the opcode position (matches the dis.dis() output)
        py_opindex opcodeIndex = op.index;

        // Get the opcode identifier (see opcode.h)
        py_opcode byte = op.opcode;

        // Get an additional oparg, see dis help for information on what each means
        py_oparg oparg = op.oparg;

        offsets.mark(curByte);
        m_comp->mark_sequence_point(curByte);

        // See if current index is part of offset stack, used for jump operations
        auto curStackDepth = m_offsetStack.find(curByte);
        if (curStackDepth != m_offsetStack.end()) {
            // Recover stack from jump
            m_stack = ValueStack(curStackDepth->second);
        }
        if (m_exceptionHandler.IsHandlerAtOffset(curByte)) {
            ExceptionHandler* handler = m_exceptionHandler.HandlerAtOffset(curByte);
            auto newBlock = BlockInfo(
                    0,
                    EXCEPT_HANDLER,
                    handler->BackHandler,
                    EhfInExceptHandler);
            m_blockStack.emplace_back(newBlock);
            m_comp->emit_mark_label(handler->ErrorTarget);
            m_comp->emit_pop_block();
            m_comp->emit_pop();
            m_comp->emit_push_block(EXCEPT_HANDLER, -1, 0);
            m_comp->emit_fetch_err();
        }

        if (!canSkipLastiUpdate(op.opcode)) {
            m_comp->emit_lasti_update(op.index);
            if (mTracingEnabled) {
                m_comp->emit_trace_line(mTracingLastInstr);
            }
        }
        auto stackInfo = getStackInfo(curByte);

        size_t curStackSize = m_stack.size();
        bool skipEffect = false;

        auto edges = graph->getEdges(curByte);
        if (g_pyjionSettings.pgc && pgcProbeRequired(curByte, pgc_status) && !(CAN_UNBOX() && op.escape)) {
            emitPgcProbes(curByte, pgcProbeSize(curByte), edges);
        }

        if (CAN_UNBOX()) {
            escapeEdges(CUR_HANDLER, edges, curByte);
        }

        switch (byte) {
            case NOP:
            case EXTENDED_ARG:
                // EXTENDED_ARG is precalculated in the graph loop
                break;
            case ROT_TWO: {
                m_comp->emit_rot_two();
                break;
            }
            case ROT_THREE: {
                m_comp->emit_rot_three();
                break;
            }
            case ROT_FOUR: {
                m_comp->emit_rot_four();
                break;
            }
            case POP_TOP:
                m_comp->emit_pop_top();
                decStack();
                break;
            case DUP_TOP:
                m_comp->emit_dup_top();
                m_stack.dup_top();// implicit incStack(1)
                break;
            case DUP_TOP_TWO:
                incStack(2);
                m_comp->emit_dup_top_two();
                break;
            case COMPARE_OP: {
                if (stackInfo.size() >= 2) {
                    if (CAN_UNBOX() && op.escape) {
                        m_comp->emit_compare_unboxed(oparg, stackInfo.second(), stackInfo.top());
                        decStack(2);
                        incStack(1, STACK_KIND_VALUE_INT);
                    } else if (OPT_ENABLED(InternRichCompare)) {
                        FLAG_OPT_USAGE(InternRichCompare);
                        m_comp->emit_compare_known_object(oparg, stackInfo.second(), stackInfo.top());
                        decStack(2);
                        errorCheck(CUR_HANDLER, "optimized compare failed", nullptr, op.index);
                        incStack(1);
                    } else {
                        m_comp->emit_compare_object(oparg);
                        decStack(2);
                        errorCheck(CUR_HANDLER, "compare failed", nullptr, op.index);
                        incStack(1);
                    }
                } else {
                    m_comp->emit_compare_object(oparg);
                    decStack(2);
                    errorCheck(CUR_HANDLER, "compare failed", nullptr, op.index);
                    incStack(1);
                }

                break;
            }
            case LOAD_BUILD_CLASS:
                m_comp->emit_load_build_class();
                errorCheck(CUR_HANDLER, "load build class failed", nullptr, op.index);
                incStack();
                break;
            case SETUP_ANNOTATIONS:
                m_comp->emit_setup_annotations();
                intErrorCheck(CUR_HANDLER, "failed to setup annotations", nullptr, op.index);
                break;
            case JUMP_ABSOLUTE:
            case JUMP_FORWARD:
                if (op.jumpsTo <= opcodeIndex) {
                    m_comp->emit_pending_calls();
                }
                m_offsetStack[op.jumpsTo] = ValueStack(m_stack);
                m_comp->emit_branch(BranchAlways, offsets.get(op.jumpsTo));
                break;
            case JUMP_IF_FALSE_OR_POP:
            case JUMP_IF_TRUE_OR_POP:
            {
                bool isTrue = byte != JUMP_IF_FALSE_OR_POP;
                if (op.jumpsTo <= opcodeIndex) {
                    m_comp->emit_pending_calls();
                }
                auto target = offsets.get(op.jumpsTo);
                m_offsetStack[op.jumpsTo] = ValueStack(m_stack);
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

                raiseOnNegativeOne(CUR_HANDLER, opcodeIndex);

                m_comp->emit_branch(isTrue ? BranchFalse : BranchTrue, noJump);

                // Jumping, load the value back and jump
                m_comp->emit_mark_label(willJump);
                m_comp->emit_load_local(tmp);// load the value back onto the stack
                m_comp->emit_branch(BranchAlways, target);

                // not jumping, load the value and dec ref it
                m_comp->emit_mark_label(noJump);
                m_comp->emit_load_local(tmp);
                m_comp->emit_pop_top();

                m_comp->emit_free_local(tmp);
                skipEffect = true;
                }
                break;
            case JUMP_IF_NOT_EXC_MATCH: {
                Label handle = m_comp->emit_define_label();
                if (op.jumpsTo <= opcodeIndex) {
                    m_comp->emit_pending_calls();
                }
                auto target = offsets.get(op.jumpsTo);
                m_comp->emit_compare_exceptions();
                decStack(2);
                errorCheck(CUR_HANDLER, "failed to compare exceptions", "", opcodeIndex);
                m_comp->emit_ptr(Py_True);
                m_comp->emit_branch(BranchEqual, handle);
                m_comp->emit_branch(BranchAlways, target);
                m_comp->emit_mark_label(handle);
                m_offsetStack[op.jumpsTo] = ValueStack(m_stack);
            }
                break;
            case POP_JUMP_IF_TRUE:
            case POP_JUMP_IF_FALSE: {
                bool isTrue = byte != POP_JUMP_IF_FALSE;
                if (op.jumpsTo <= opcodeIndex) {
                    m_comp->emit_pending_calls();
                }
                auto target = offsets.get(op.jumpsTo);
                if (CAN_UNBOX() && op.escape) {
                    auto top = stackInfo.top();
                    if (!top.hasValue())
                        // Just see if its null/0
                        m_comp->emit_branch(isTrue ? BranchTrue : BranchFalse, target);
                    else {
                        switch (top.Value->kind()) {
                            case AVK_Float:
                                m_comp->emit_float(0.0);
                                m_comp->emit_branch(isTrue ? BranchNotEqual : BranchEqual, target);
                                break;
                            case AVK_Bool:
                            case AVK_Integer:
                                m_comp->emit_branch(isTrue ? BranchTrue : BranchFalse, target);
                                break;
                        }
                    }
                } else {
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

                    raiseOnNegativeOne(CUR_HANDLER, opcodeIndex);

                    m_comp->emit_branch(isTrue ? BranchFalse : BranchTrue, noJump);

                    // Branching, pop the value and branch
                    m_comp->emit_mark_label(willJump);
                    m_comp->emit_pop_top();
                    m_comp->emit_branch(BranchAlways, target);

                    // Not branching, just pop the value and fall through
                    m_comp->emit_mark_label(noJump);
                    m_comp->emit_pop_top();
                }
                decStack();
                m_offsetStack[op.jumpsTo] = ValueStack(m_stack);
            }
                break;
            case LOAD_NAME:
                if (OPT_ENABLED(HashedNames)) {
                    FLAG_OPT_USAGE(HashedNames);
                    m_comp->emit_load_name_hashed(PyTuple_GetItem(mCode->co_names, oparg), nameHashes[oparg]);
                } else {
                    m_comp->emit_load_name(PyTuple_GetItem(mCode->co_names, oparg));
                }
                errorCheck(CUR_HANDLER, "load name failed", PyUnicode_AsUTF8(PyTuple_GetItem(mCode->co_names, oparg)), op.index);
                incStack();
                break;
            case STORE_ATTR:
                m_comp->emit_store_attr(PyTuple_GetItem(mCode->co_names, oparg));
                decStack(2);
                intErrorCheck(CUR_HANDLER, "store attr failed", PyUnicode_AsUTF8(PyTuple_GetItem(mCode->co_names, oparg)), op.index);
                break;
            case DELETE_ATTR:
                m_comp->emit_delete_attr(PyTuple_GetItem(mCode->co_names, oparg));
                decStack();
                intErrorCheck(CUR_HANDLER, "delete attr failed", PyUnicode_AsUTF8(PyTuple_GetItem(mCode->co_names, oparg)), op.index);
                break;
            case LOAD_ATTR:
                if (OPT_ENABLED(LoadAttr) && !stackInfo.empty()) {
                    FLAG_OPT_USAGE(LoadAttr);
                    m_comp->emit_load_attr(PyTuple_GetItem(mCode->co_names, oparg), stackInfo.top());
                } else {
                    m_comp->emit_load_attr(PyTuple_GetItem(mCode->co_names, oparg));
                }
                decStack();
                errorCheck(CUR_HANDLER, "load attr failed", PyUnicode_AsUTF8(PyTuple_GetItem(mCode->co_names, oparg)), op.index);
                incStack();
                break;
            case STORE_GLOBAL:
                m_comp->emit_store_global(PyTuple_GetItem(mCode->co_names, oparg));
                decStack();
                intErrorCheck(CUR_HANDLER, "store global failed", PyUnicode_AsUTF8(PyTuple_GetItem(mCode->co_names, oparg)), op.index);
                break;
            case DELETE_GLOBAL:
                m_comp->emit_delete_global(PyTuple_GetItem(mCode->co_names, oparg));
                intErrorCheck(CUR_HANDLER, "delete global failed", PyUnicode_AsUTF8(PyTuple_GetItem(mCode->co_names, oparg)), op.index);
                break;
            case LOAD_GLOBAL:
                m_comp->emit_load_global(PyTuple_GetItem(mCode->co_names, oparg), lastResolvedGlobal[oparg], mGlobalsVersion, mBuiltinsVersion);
                errorCheck(CUR_HANDLER, "load global failed", PyUnicode_AsUTF8(PyTuple_GetItem(mCode->co_names, oparg)), op.index);
                incStack();
                break;
            case LOAD_CONST:
                if (CAN_UNBOX() && op.escape) {
                    loadUnboxedConst(oparg, opcodeIndex);
                } else {
                    loadConst(oparg, opcodeIndex);
                }
                break;
            case STORE_NAME:
                m_comp->emit_store_name(PyTuple_GetItem(mCode->co_names, oparg));
                decStack();
                intErrorCheck(CUR_HANDLER, "store name failed", PyUnicode_AsUTF8(PyTuple_GetItem(mCode->co_names, oparg)), op.index);
                break;
            case DELETE_NAME:
                m_comp->emit_delete_name(PyTuple_GetItem(mCode->co_names, oparg));
                intErrorCheck(CUR_HANDLER, "delete name failed", PyUnicode_AsUTF8(PyTuple_GetItem(mCode->co_names, oparg)), op.index);
                break;
            case DELETE_FAST:
                if (CAN_UNBOX() && op.escape) {
                    // TODO : Decide if we need to store some junk value in the local?
                } else {
                    loadFastWorker(CUR_HANDLER, oparg, true, curByte);
                    m_comp->emit_pop_top();
                    m_comp->emit_delete_fast(oparg);
                }
                m_assignmentState[oparg] = false;
                break;
            case STORE_FAST:
                if (CAN_UNBOX() && op.escape) {
                    storeFastUnboxed(oparg);
                } else {
                    m_comp->emit_store_fast(oparg);
                    decStack();
                }
                m_assignmentState[oparg] = true;
                break;
            case LOAD_FAST:
                if (CAN_UNBOX() && op.escape) {
                    loadFastUnboxed(oparg, opcodeIndex);
                } else {
                    bool checkUnbound = m_assignmentState.find(op.oparg) == m_assignmentState.end() || !m_assignmentState.find(op.oparg)->second;
                    loadFastWorker(CUR_HANDLER, op.oparg, checkUnbound, opcodeIndex);
                    incStack();
                }
                break;
            case UNPACK_SEQUENCE:
                m_comp->emit_unpack_sequence(oparg, stackInfo.top());
                decStack();
                incStack(oparg);
                intErrorCheck(CUR_HANDLER, "failed to unpack sequence", stackInfo.top().Value->describe());
                break;
            case UNPACK_EX: {
                size_t rightSize = oparg >> 8, leftSize = oparg & 0xff;
                m_comp->emit_unpack_sequence_ex(leftSize, rightSize, stackInfo.top());
                decStack();
                incStack(leftSize + rightSize + 1);
                intErrorCheck(CUR_HANDLER, "failed to unpack extended");
                break;
            }
            case CALL_FUNCTION_KW: {
                // names is a tuple on the stack, should have come from a LOAD_CONST
                auto names = m_comp->emit_spill();
                decStack();// names
                buildTuple(CUR_HANDLER, oparg);
                m_comp->emit_load_and_free_local(names);

                m_comp->emit_kwcall_with_tuple();
                decStack();// function & names

                errorCheck(CUR_HANDLER, "kwcall failed", "", op.index);// TODO: emit function name?
                incStack();
                break;
            }
            case CALL_FUNCTION_EX:
                if (oparg & 0x01) {
                    // kwargs, then args, then function
                    m_comp->emit_call_kwargs();
                    decStack(3);
                } else {
                    m_comp->emit_call_args();
                    decStack(2);
                }

                errorCheck(CUR_HANDLER, "call failed", "", op.index);// TODO: function name
                incStack();
                break;
            case CALL_FUNCTION: {
                if (OPT_ENABLED(FunctionCalls) &&
                    stackInfo.size() >= (oparg + 1) &&
                    stackInfo.nth(oparg + 1).hasSource() &&
                    stackInfo.nth(oparg + 1).hasValue() && !mTracingEnabled) {
                    FLAG_OPT_USAGE(FunctionCalls);
                    m_comp->emit_call_function_inline(oparg, stackInfo.nth(oparg + 1));
                    decStack(oparg + 1);// target + args(oparg)
                    errorCheck(CUR_HANDLER, "inline function call failed", "", op.index);
                } else {
                    if (!m_comp->emit_call_function(oparg)) {
                        buildTuple(CUR_HANDLER, oparg);
                        incStack();
                        m_comp->emit_call_with_tuple();
                        decStack(2);// target + args
                        errorCheck(CUR_HANDLER, "call n-function failed", "", op.index);
                    } else {
                        decStack(oparg + 1);// target + args(oparg)
                        errorCheck(CUR_HANDLER, "call function failed", "", op.index);
                    }
                }
                incStack();
                break;
            }
            case BUILD_TUPLE:
                buildTuple(CUR_HANDLER, oparg);
                incStack();
                break;
            case BUILD_LIST:
                m_comp->emit_new_list(op.oparg);
                errorCheck(CUR_HANDLER, "build list failed");
                if (op.oparg != 0) {
                    m_comp->emit_list_store(op.oparg);
                }
                decStack(op.oparg);
                incStack();
                break;
            case BUILD_MAP:
                m_comp->emit_new_dict(op.oparg);
                errorCheck(CUR_HANDLER, "build map failed");

                if (op.oparg > 0) {
                    auto map = m_comp->emit_spill();
                    for (py_oparg curArg = 0; curArg < op.oparg; curArg++) {
                        m_comp->emit_load_local(map);

                        m_comp->emit_dict_store();

                        decStack(2);
                        intErrorCheck(CUR_HANDLER, "dict store failed");
                    }
                    m_comp->emit_load_and_free_local(map);
                }
                incStack();
                break;
            case STORE_SUBSCR:
                if (CAN_UNBOX() && op.escape){
                    m_comp->emit_store_subscr_unboxed(stackInfo.third(), stackInfo.second(), stackInfo.top());
                } else if (OPT_ENABLED(KnownStoreSubscr) && stackInfo.size() >= 3) {
                    FLAG_OPT_USAGE(KnownStoreSubscr);
                    m_comp->emit_store_subscr(stackInfo.third(), stackInfo.second(), stackInfo.top());
                } else {
                    m_comp->emit_store_subscr();
                }
                decStack(3);
                intErrorCheck(CUR_HANDLER, "store subscr failed", "", op.index);
                break;
            case DELETE_SUBSCR:
                decStack(2);
                m_comp->emit_delete_subscr();
                intErrorCheck(CUR_HANDLER, "delete subscr failed", "", op.index);
                break;
            case BUILD_SLICE:
                assert(oparg == 2 || 3);
                if (oparg != 3) {
                    m_comp->emit_null();
                }
                m_comp->emit_build_slice();
                decStack(oparg);
                errorCheck(CUR_HANDLER, "slice failed", "", op.index);
                incStack();
                break;
            case BUILD_SET: {
                m_comp->emit_new_set();
                errorCheck(CUR_HANDLER, "build set failed");

                if (op.oparg != 0) {
                    auto setTmp = m_comp->emit_define_local();
                    m_comp->emit_store_local(setTmp);
                    auto* tmps = new Local[op.oparg];
                    auto* frees = new Label[op.oparg];
                    for (size_t i = 0; i < op.oparg; i++) {
                        tmps[op.oparg - (i + 1)] = m_comp->emit_spill();
                        decStack();
                    }

                    // load all the values into the set...
                    auto err = m_comp->emit_define_label();
                    for (size_t i = 0; i < op.oparg; i++) {
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
                    for (size_t i = 1; i < op.oparg; i++) {
                        m_comp->emit_mark_label(frees[i - 1]);
                        m_comp->emit_load_local(tmps[i]);
                        m_comp->emit_pop_top();
                    }

                    // And if the last one failed, then all of the values have been
                    // decref'd
                    m_comp->emit_mark_label(frees[op.oparg - 1]);
                    branchRaise(CUR_HANDLER, "build set failed");

                    m_comp->emit_mark_label(noErr);
                    delete[] frees;
                    delete[] tmps;

                    m_comp->emit_load_local(setTmp);
                    m_comp->emit_free_local(setTmp);
                }
                incStack();
            }
                break;
            case UNARY_POSITIVE:
                if (CAN_UNBOX() && op.escape) {
                    m_comp->emit_unboxed_unary_positive(stackInfo.top());
                } else {
                    m_comp->emit_unary_positive();
                    decStack();
                    errorCheck(CUR_HANDLER, "unary positive failed", "", opcodeIndex);
                    incStack();
                }
                break;
            case UNARY_NEGATIVE:
                if (CAN_UNBOX() && op.escape) {
                    m_comp->emit_unboxed_unary_negative(stackInfo.top());
                } else {
                    m_comp->emit_unary_negative();
                    decStack();
                    errorCheck(CUR_HANDLER, "unary negative failed", "", opcodeIndex);
                    incStack();
                }
                break;
            case UNARY_NOT:
                if (CAN_UNBOX() && op.escape) {
                    m_comp->emit_unboxed_unary_not(stackInfo.top());
                    decStack(1);
                    incStack(1, LK_Int);
                } else {
                    m_comp->emit_unary_not();
                    decStack(1);
                    errorCheck(CUR_HANDLER, "unary not failed", "", opcodeIndex);
                    incStack();
                }
                break;
            case UNARY_INVERT:
                if (CAN_UNBOX() && op.escape) {
                    m_comp->emit_unboxed_unary_invert(stackInfo.top());
                    decStack(1);
                    incStack(1, LK_Int);
                } else {
                    m_comp->emit_unary_invert();
                    decStack(1);
                    errorCheck(CUR_HANDLER, "unary invert failed", "", op.index);
                    incStack();
                }
                break;
            case BINARY_SUBSCR:
                if (CAN_UNBOX() && op.escape) {
                    auto retKind = m_comp->emit_unboxed_binary_subscr(stackInfo.second(), stackInfo.top());
                    decStack(2);
                    invalidIntErrorCheck(CUR_HANDLER, "unboxed binary op failed", op.index, byte, PyExc_IndexError, "bytearray index out of range");
                    incStack(1, retKind);
                } else if (OPT_ENABLED(KnownBinarySubscr) && stackInfo.size() >= 2) {
                    FLAG_OPT_USAGE(KnownBinarySubscr);
                    m_comp->emit_binary_subscr(stackInfo.second(), stackInfo.top());
                    decStack(2);
                    errorCheck(CUR_HANDLER, "optimized binary subscr failed", "", op.index);
                    incStack();
                } else {
                    m_comp->emit_binary_subscr();
                    decStack(2);
                    errorCheck(CUR_HANDLER, "binary subscr failed", "", op.index);
                    incStack();
                }

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
                if (OPT_ENABLED(TypeSlotLookups) && stackInfo.size() >= 2) {
                    if (CAN_UNBOX() && op.escape) {
                        auto retKind = m_comp->emit_unboxed_binary_object(byte, stackInfo.second(), stackInfo.top());
                        decStack(2);
                        if (canReturnInfinity(byte)) {
                            switch (retKind) {
                                case LK_Float:
                                    invalidFloatErrorCheck(CUR_HANDLER, "unboxed binary op failed", op.index, byte);
                                    break;
                                case LK_Int:
                                    invalidIntErrorCheck(CUR_HANDLER, "unboxed binary op failed", op.index, byte);
                                    break;
                            }
                        }
                        incStack(1, retKind);
                    } else {
                        FLAG_OPT_USAGE(TypeSlotLookups);
                        m_comp->emit_binary_object(byte, stackInfo.second(), stackInfo.top());
                        decStack(2);
                        errorCheck(CUR_HANDLER, "optimized binary op failed", "", op.index);
                        incStack();
                    }
                } else {
                    m_comp->emit_binary_object(byte);
                    decStack(2);
                    errorCheck(CUR_HANDLER, "binary op failed", "", op.index);
                    incStack();
                }
                break;
            case RETURN_VALUE:
                m_comp->emit_return_value(m_retValue, m_retLabel);
                decStack();
                break;
            case MAKE_FUNCTION:
                m_comp->emit_new_function();
                decStack(2);
                errorCheck(CUR_HANDLER, "new function failed");

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
                break;
            case LOAD_DEREF:
                m_comp->emit_load_deref(oparg);
                errorCheck(CUR_HANDLER, "load deref failed", "", op.index);
                incStack();
                break;
            case STORE_DEREF:
                m_comp->emit_store_deref(oparg);
                decStack();
                break;
            case DELETE_DEREF:
                m_comp->emit_delete_deref(oparg);
                break;
            case LOAD_CLOSURE:
                m_comp->emit_load_closure(oparg);
                errorCheck(CUR_HANDLER, "load closure failed", "", op.index);
                incStack();
                break;
            case GET_ITER: {
                if (CAN_UNBOX()) {
                    auto edgesOut = graph->getEdgesFrom(curByte);
                    if (edgesOut.size() == 1
                        && edgesOut[0].kind == AVK_UnboxedRangeIterator
                        && graph->operator[](edgesOut[0].to).escape
                        && graph->operator[](edgesOut[0].to).opcode == FOR_ITER) {
                        m_comp->emit_getiter_unboxed();
                    } else {
                        m_comp->emit_getiter();
                    }
                } else {
                    m_comp->emit_getiter();
                }
                decStack();
                errorCheck(CUR_HANDLER, "get iter failed", "", op.index);
                incStack();
                break;
            }
            case FOR_ITER: {
                auto postIterStack = ValueStack(m_stack);
                postIterStack.dec(1);// pop iter when stopiter happens
                if (CAN_UNBOX() && op.escape) {
                    // dup the iter so that it stays on the stack for the next iteration
                    m_comp->emit_dup();// ..., iter -> iter, iter, ...

                    // emits NULL on error, 0xff on StopIter and ptr on next
                    m_comp->emit_for_next_unboxed();// ..., iter, iter -> iter, iter(), ...

                    incStack(1);// value

                    auto next = m_comp->emit_define_label();

                    /* Start next iter branch */
                    m_comp->emit_dup();
                    m_comp->emit_ptr((void*) SIG_STOP_ITER);
                    m_comp->emit_branch(BranchNotEqual, next);
                    /* End next iter branch */
                    /* Start stop iter branch */
                    m_comp->emit_pop();                                          // Pop the 0xff StopIter value
                    m_comp->emit_pop_top();                                      // POP and DECREF iter
                    m_comp->emit_branch(BranchAlways, offsets.get(op.jumpsTo));// Goto: post-stack
                    /* End stop iter error branch */

                    m_comp->emit_mark_label(next);
                } else {
                    // dup the iter so that it stays on the stack for the next iteration
                    m_comp->emit_dup();// ..., iter -> iter, iter, ...

                    // emits NULL on error, 0xff on StopIter and ptr on next
                    m_comp->emit_for_next();// ..., iter, iter -> iter, iter(), ...

                    /* Check for SIG_ITER_ERROR on the stack, indicating error (not stopiter) */
                    auto noErr = m_comp->emit_define_label();
                    m_comp->emit_dup();
                    m_comp->emit_store_local(mErrorCheckLocal);
                    m_comp->emit_ptr((void*) SIG_ITER_ERROR);
                    m_comp->emit_branch(BranchNotEqual, noErr);

                    branchRaise(CUR_HANDLER, "failed to fetch iter", "", 0);
                    m_comp->emit_mark_label(noErr);
                    m_comp->emit_load_local(mErrorCheckLocal);

                    incStack(1);// value

                    auto next = m_comp->emit_define_label();

                    /* Start next iter branch */
                    m_comp->emit_dup();
                    m_comp->emit_ptr((void*) SIG_STOP_ITER);
                    m_comp->emit_branch(BranchNotEqual, next);
                    /* End next iter branch */

                    /* Start stop iter branch */
                    m_comp->emit_pop();                                          // Pop the 0xff StopIter value
                    m_comp->emit_pop_top();                                      // POP and DECREF iter
                    m_comp->emit_branch(BranchAlways, offsets.get(op.jumpsTo));// Goto: post-stack
                    /* End stop iter error branch */

                    m_comp->emit_mark_label(next);
                }
                m_offsetStack[op.jumpsTo] = postIterStack;
                break;
            }
            case SET_ADD:
                // Calls set.update(TOS1[-i], TOS). Used to build sets.
                m_comp->lift_n_to_second(oparg);
                m_comp->emit_set_add();
                decStack(2);// set, value
                errorCheck(CUR_HANDLER, "set update failed", "", op.index);
                incStack(1);// set
                m_comp->sink_top_to_n(oparg - 1);
                break;
            case MAP_ADD:
                // Calls dict.__setitem__(TOS1[-i], TOS1, TOS). Used to implement dict comprehensions.
                m_comp->lift_n_to_third(oparg + 1);
                m_comp->emit_map_add();
                decStack(3);
                errorCheck(CUR_HANDLER, "map add failed", "", op.index);
                incStack();
                m_comp->sink_top_to_n(oparg - 1);
                break;
            case LIST_APPEND: {
                // Calls list.append(TOS1[-i], TOS).
                m_comp->lift_n_to_second(oparg);
                m_comp->emit_list_append();
                decStack(2);// list, value
                errorCheck(CUR_HANDLER, "list append failed", "", op.index);
                incStack(1);
                m_comp->sink_top_to_n(oparg - 1);
                break;
            }
            case DICT_MERGE: {
                // Calls dict.update(TOS1[-i], TOS). Used to merge dicts.
                m_comp->lift_n_to_second(oparg);
                m_comp->emit_dict_merge();
                decStack(2);// list, value
                errorCheck(CUR_HANDLER, "dict merge failed", "", op.index);
                incStack(1);
                m_comp->sink_top_to_n(oparg - 1);
                break;
            }
            case PRINT_EXPR:
                m_comp->emit_print_expr();
                decStack();
                intErrorCheck(CUR_HANDLER, "print expr failed", "", op.index);
                break;
            case LOAD_CLASSDEREF:
                m_comp->emit_load_classderef(oparg);
                errorCheck(CUR_HANDLER, "load classderef failed", "", op.index);
                incStack();
                break;
            case RAISE_VARARGS:
                // do raise (exception, cause)
                // We can be invoked with no args (bare raise), raise exception, or raise w/ exception and cause
                switch (oparg) {// NOLINT(hicpp-multiway-paths-covered)
                    case 0:
                        m_comp->emit_null();// NOLINT(bugprone-branch-clone)
                    case 1:
                        m_comp->emit_null();
                    case 2:
                        decStack(oparg);
                        // returns 1 if we're doing a re-raise in which case we don't need
                        // to update the traceback.  Otherwise returns 0.
                        m_comp->emit_raise_varargs();
                        if (oparg == 0) {
                            Label reraise = m_comp->emit_define_label();
                            Label done = m_comp->emit_define_label();
                            m_comp->emit_branch(BranchTrue, reraise);
                            branchRaise(CUR_HANDLER, "hit native error", "", op.index);
                            m_comp->emit_branch(BranchAlways, done);
                            m_comp->emit_mark_label(reraise);
                            branchRaise(CUR_HANDLER, "hit reraise", "", op.index, false, false);
                            m_comp->emit_mark_label(done);
                        } else {
                            m_comp->emit_pop();
                            branchRaise(CUR_HANDLER, "hit native error", "", op.index);
                        }
                        break;
                }
                break;
            case SETUP_FINALLY: {
                auto handlerLabel = m_comp->emit_define_label();
                auto newHandler = m_exceptionHandler.AddSetupFinallyHandler(
                        handlerLabel,
                        m_stack,
                        CUR_HANDLER,
                        op.jumpsTo);

                auto newBlock = BlockInfo(
                        op.jumpsTo,
                        SETUP_FINALLY,
                        newHandler);

                m_blockStack.emplace_back(newBlock);
                m_comp->emit_push_block(SETUP_FINALLY, op.jumpsTo, 0);

                ValueStack newStack = ValueStack(m_stack);
                newStack.inc(6, STACK_KIND_OBJECT);
                // This stack only gets used if an error occurs within the try:
                m_offsetStack[op.jumpsTo] = newStack;
                skipEffect = true;
            } break;
            case RERAISE: {
                m_comp->emit_restore_err();
                // TODO: Both RERAISE and POP_EXCEPT are unreachable in some scenarios.
                // Potentially mark these instructions and skip them.
                if (m_stack.size() >= 3)
                    decStack(3);
                else
                    skipEffect = true;
                branchRaise(CUR_HANDLER, "reraise error", "", op.index);
                break;
            }
            case POP_EXCEPT: {
                auto block = m_blockStack.back();
                unwindEh(block.CurrentHandler, block.CurrentHandler->BackHandler);
                m_comp->emit_pop_except();
                if (m_stack.size() >= 3)
                    decStack(3);
                else
                    skipEffect = true;
            }
                break;
            case POP_BLOCK:
                m_comp->emit_pop_block();
                m_comp->emit_pop();// Dont do anything with the block atm
                m_blockStack.pop_back();
                break;
            case SETUP_WITH:
                return {nullptr, IncompatibleOpcode_With};
            case YIELD_FROM:
                return {nullptr, IncompatibleOpcode_Yield};
            case IMPORT_NAME:
                m_comp->emit_import_name(PyTuple_GetItem(mCode->co_names, oparg));
                decStack(2);
                errorCheck(CUR_HANDLER, "import name failed", PyUnicode_AsUTF8(PyTuple_GetItem(mCode->co_names, oparg)), op.index);
                incStack();
                break;
            case IMPORT_FROM:
                m_comp->emit_import_from(PyTuple_GetItem(mCode->co_names, oparg));
                errorCheck(CUR_HANDLER, "import from failed", PyUnicode_AsUTF8(PyTuple_GetItem(mCode->co_names, oparg)), op.index);
                incStack();
                break;
            case IMPORT_STAR:
                m_comp->emit_import_star();
                decStack(1);
                intErrorCheck(CUR_HANDLER, "import star failed", "*", op.index);
                break;
            case FORMAT_VALUE: {
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
                    switch (whichConversion) {// NOLINT(hicpp-multiway-paths-covered)
                        case FVC_STR:
                            m_comp->emit_pyobject_str();
                            break;
                        case FVC_REPR:
                            m_comp->emit_pyobject_repr();
                            break;
                        case FVC_ASCII:
                            m_comp->emit_pyobject_ascii();
                            break;
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

                    branchRaise(CUR_HANDLER, "conversion failed", "", op.index);
                    m_comp->emit_mark_label(noErr);
                    m_comp->emit_load_local(mErrorCheckLocal);
                }

                if ((oparg & FVS_MASK) == FVS_HAVE_SPEC) {
                    // format spec
                    m_comp->emit_load_and_free_local(fmtSpec);
                    m_comp->emit_pyobject_format();

                    errorCheck(CUR_HANDLER, "format object", "FVS_HAVE_SPEC", op.index);
                } else if (!whichConversion) {
                    // TODO: This could also be avoided if we knew we had a string on the stack

                    // If we did a conversion we know we have a string...
                    // Otherwise we need to convert
                    m_comp->emit_format_value();
                }
                incStack();
                break;
            }
            case BUILD_STRING: {
                buildTuple(CUR_HANDLER, oparg);
                m_comp->emit_long_long(oparg);
                incStack(2);
                m_comp->emit_unicode_joinarray();
                decStack(2);
                errorCheck(CUR_HANDLER, "build string (fstring) failed", "", op.index);
                incStack();
                break;
            }
            case BUILD_CONST_KEY_MAP: {
                /*
                 * The version of BUILD_MAP specialized for constant keys.
                 * Pops the top element on the stack which contains a tuple of keys,
                 * then starting from TOS1, pops count values to form values
                 * in the built dictionary.
                 */
                // spill TOP into keys and then build a tuple for stack
                buildTuple(CUR_HANDLER, oparg + (py_oparg) 1);
                incStack();
                m_comp->emit_dict_build_from_map();
                decStack(1);
                errorCheck(CUR_HANDLER, "dict map failed", "", op.index);
                incStack(1);
                break;
            }
            case LIST_EXTEND: {
                assert(oparg == 1);// always 1 in 3.9
                m_comp->lift_n_to_top(oparg);
                m_comp->emit_list_extend();
                decStack(2);
                errorCheck(CUR_HANDLER, "list extend failed", "", op.index);
                incStack(1);
                // Takes list, value from stack and pushes list back onto stack
                break;
            }
            case DICT_UPDATE: {
                // Calls dict.update(TOS1[-i], TOS). Used to build dicts.
                assert(oparg == 1);// always 1 in 3.9
                m_comp->lift_n_to_top(oparg);
                m_comp->emit_dict_update();
                decStack(2);// dict, item
                errorCheck(CUR_HANDLER, "dict update failed", "", op.index);
                incStack(1);
                break;
            }
            case SET_UPDATE: {
                // Calls set.update(TOS1[-i], TOS). Used to build sets.
                assert(oparg == 1);// always 1 in 3.9
                m_comp->lift_n_to_top(oparg);
                m_comp->emit_set_extend();
                decStack(2);// set, iterable
                errorCheck(CUR_HANDLER, "set update failed", "", op.index);
                incStack(1);// set
                break;
            }
            case LIST_TO_TUPLE: {
                m_comp->emit_list_to_tuple();
                decStack(1);// list
                errorCheck(CUR_HANDLER, "list to tuple failed", "", op.index);
                incStack(1);// tuple
                break;
            }
            case IS_OP: {
                if (OPT_ENABLED(InlineIs) && stackInfo.size() >= 2) {
                    FLAG_OPT_USAGE(InlineIs);
                    m_comp->emit_is(oparg, stackInfo.second(), stackInfo.top());
                } else {
                    m_comp->emit_is(oparg);
                }
                decStack(2);
                errorCheck(CUR_HANDLER, "is check failed", "", op.index);
                incStack(1);
                break;
            }
            case CONTAINS_OP: {
                // Oparg is 0 if "in", and 1 if "not in"
                if (oparg == 0)
                    m_comp->emit_in();
                else
                    m_comp->emit_not_in();
                decStack(2);// in/not in takes two
                errorCheck(CUR_HANDLER, "in failed", "", op.index);
                incStack();// pushes result
                break;
            }
            case LOAD_METHOD: {
                if (OPT_ENABLED(BuiltinMethods) && !stackInfo.empty() && stackInfo.top().hasValue() && stackInfo.top().Value->known()) {
                    FLAG_OPT_USAGE(BuiltinMethods);
                    m_comp->emit_builtin_method(PyTuple_GetItem(mCode->co_names, oparg), stackInfo.top().Value);
                } else {
                    m_comp->emit_load_method(PyTuple_GetItem(mCode->co_names, oparg));
                }
                incStack(1);
                intErrorCheck(CUR_HANDLER, "failed to load method", PyUnicode_AsUTF8(PyTuple_GetItem(mCode->co_names, oparg)), op.index);
                break;
            }
            case CALL_METHOD: {
                if (!m_comp->emit_method_call(oparg)) {
                    buildTuple(CUR_HANDLER, oparg);
                    m_comp->emit_method_call_n();
                    decStack(2);// + method + name + nargs
                } else {
                    decStack(2 + oparg);// + method + name + nargs
                }
                errorCheck(CUR_HANDLER, "failed to call method", "", op.index);
                incStack();//result
                break;
            }
            case LOAD_ASSERTION_ERROR: {
                m_comp->emit_load_assertion_error();
                incStack();
                break;
            }
            case YIELD_VALUE: {
                m_comp->emit_yield_value(m_retValue, m_retLabel, op.index, curStackSize, yieldOffsets);
                break;
            }
            case GEN_START: {
                m_comp->emit_set_frame_stackdepth(0);
                skipEffect = true;
                break;
            }
            default:
                return {nullptr, IncompatibleOpcode_Unknown};
        }

        if (!skipEffect && static_cast<size_t>(PyCompile_OpcodeStackEffect(byte, oparg)) != (m_stack.size() - curStackSize)) {
#ifdef DEBUG_VERBOSE
            printf("Opcode %s at %d should have stack effect %d, but was %lu\n", opcodeName(byte), op.index, PyCompile_OpcodeStackEffect(byte, oparg), (m_stack.size() - curStackSize));
#endif
            return {nullptr, CompilationStackEffectFault};
        }
    }

    // label branch for error handling when we have no EH handlers, (return NULL).
    m_comp->emit_branch(BranchAlways, rootHandlerLabel);
    m_comp->emit_mark_label(rootHandlerLabel);

    m_comp->emit_null();
    m_comp->emit_set_frame_state(PY_FRAME_RAISED);
    m_comp->emit_set_frame_stackdepth(0);

    auto finalRet = m_comp->emit_define_label();
    m_comp->emit_branch(BranchAlways, finalRet);

    // Return value from local
    m_comp->emit_mark_label(m_retLabel);
    m_comp->emit_load_local(m_retValue);

    // Final return position, pop frame and return
    m_comp->emit_mark_label(finalRet);

    if (mTracingEnabled) {
        m_comp->emit_trace_frame_exit(m_retValue);
    }
    if (mProfilingEnabled) {
        m_comp->emit_profile_frame_exit(m_retValue);
    }

    if (m_comp->emit_pop_frame()) {
        FLAG_OPT_USAGE(InlineFramePushPop);
    }

    m_comp->emit_ret();
    auto code = m_comp->emit_compile();
    if (code != nullptr) {
        return {
                .compiledCode = code,
                .result = Success,
                .optimizations = optimizationsMade,
        };
    } else {
        return {nullptr, CompilationJitFailure};
    }
}

void AbstractInterpreter::testBoolAndBranch(Local value, bool isTrue, Label target) {
    m_comp->emit_load_local(value);
    m_comp->emit_ptr(isTrue ? Py_False : Py_True);
    m_comp->emit_branch(BranchEqual, target);
}

InstructionGraph* AbstractInterpreter::buildInstructionGraph(bool escapeLocals) {
    unordered_map<py_opindex, const InterpreterStack*> stacks;
    for (const auto& state : mStartStates) {
        stacks[state.first] = &state.second.mStack;
    }
    return new InstructionGraph(mCode, stacks, escapeLocals);
}

AbstactInterpreterCompileResult AbstractInterpreter::compile(PyObject* builtins, PyObject* globals, PyjionCodeProfile* profile, PgcStatus pgc_status) {
    try {

        AbstractInterpreterResult interpreted = interpret(builtins, globals, profile, pgc_status);
        if (interpreted != Success) {
            return {nullptr, nullptr, interpreted};
        }
        bool unboxVars = OPT_ENABLED(Unboxing) && !(mCode->co_flags & CO_GENERATOR);
        auto boxedGraph = buildInstructionGraph(unboxVars);
        PythonCompiler jitter(mCode);
        auto workerResult = compileWorker(pgc_status, boxedGraph, &jitter);
        if (workerResult.result != Success){
            return {nullptr, nullptr, workerResult.result};
        }
        AbstactInterpreterCompileResult result = {workerResult.compiledCode, nullptr, Success, nullptr, nullptr, workerResult.optimizations};
        if (g_pyjionSettings.graph) {
            result.instructionGraph = boxedGraph->makeGraph(PyUnicode_AsUTF8(mCode->co_name));

//                        // This snippet is really useful from time to time. Keep it here commented out
//                        if (PyUnicode_CompareWithASCIIString(mCode->co_name, "resolve_expression") == 0){
//                            printf("%s", PyUnicode_AsUTF8(result.instructionGraph));
//                        }

#ifdef DUMP_INSTRUCTION_GRAPHS
            printf("%s", PyUnicode_AsUTF8(result.instructionGraph));
#endif
        }
        auto genericGraph = buildInstructionGraph(false);
        PythonCompiler unboxedJitter(mCode);
        auto genericResult = compileWorker(Optimized, genericGraph, &unboxedJitter);
        if (genericResult.result == Success){
            if (g_pyjionSettings.graph) {
                result.genericGraph = genericGraph->makeGraph(PyUnicode_AsUTF8(mCode->co_name));
            }
            result.genericCompiledCode = genericResult.compiledCode;
        }
        delete genericGraph;
        delete boxedGraph;
        return result;
    } catch (const exception& e) {
#ifdef DEBUG_VERBOSE
        printf("Error whilst compiling %s: %s\n", PyUnicode_AsUTF8(mCode->co_name), e.what());
#endif
        return {nullptr, nullptr, CompilationException, };
    }
}

bool AbstractInterpreter::canSkipLastiUpdate(py_opcode opcode) {
    switch (opcode) {
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
        case JUMP_FORWARD:
        case JUMP_ABSOLUTE:
        case GEN_START:
        case SETUP_ANNOTATIONS:
            return true;
        default:
            return false;
    }
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
    auto abstractT = GetAbstractType(constValue->ob_type, constValue);
    switch (abstractT) {
        case AVK_Float:
            m_comp->emit_float(PyFloat_AS_DOUBLE(constValue));
            incStack(1, STACK_KIND_VALUE_FLOAT);
            break;
        case AVK_BigInteger:
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
        default:
            throw UnexpectedValueException();
    }
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

void AbstractInterpreter::loadFastWorker(ExceptionHandler* handler, py_oparg local, bool checkUnbound, py_opindex curByte) {
    m_comp->emit_load_fast(local);

    // Check if arg is unbound, raises UnboundLocalError
    if (checkUnbound) {
        Label success = m_comp->emit_define_label();

        m_comp->emit_dup();
        m_comp->emit_store_local(mErrorCheckLocal);
        m_comp->emit_branch(BranchTrue, success);

        m_comp->emit_ptr(PyTuple_GetItem(mCode->co_varnames, local));

        m_comp->emit_unbound_local_check();

        branchRaise(handler, "unbound local", PyUnicode_AsUTF8(PyTuple_GetItem(mCode->co_varnames, local)), curByte);

        m_comp->emit_mark_label(success);
        m_comp->emit_load_local(mErrorCheckLocal);
    }

    m_comp->emit_dup();
    m_comp->emit_incref();
}

// Unwinds exception handling starting at the current handler.  Emits the unwind for all
// of the current handlers until we reach one which will actually handle the current
// exception.
void AbstractInterpreter::unwindEh(ExceptionHandler* fromHandler, ExceptionHandler* toHandler) {
    auto cur = fromHandler;
    do {
        // TODO : Skip for now, this does nothing useful.
        //        if (exVars.PrevExc.is_valid()) {
        //            m_comp->emit_unwind_eh(exVars.PrevExc, exVars.PrevExcVal, exVars.PrevTraceback);
        //        }
        if (cur->IsRootHandler())
            break;
        cur = cur->BackHandler;
    } while (cur != nullptr && !cur->IsRootHandler() && cur != toHandler && !cur->IsTryExceptOrFinally());
}

// We want to maintain a mapping between locations in the Python byte code
// and generated locations in the code.  So for each Python byte code index
// we define a label in the generated code.  If we ever branch to a specific
// opcode then we'll branch to the generated label.
void OffsetMap::mark(py_opindex index) {
    auto existingLabel = m_offsetLabels.find(index);
    if (existingLabel != m_offsetLabels.end()) {
        m_comp->emit_mark_label(existingLabel->second);
    } else {
        auto label = m_comp->emit_define_label();
        m_offsetLabels[index] = label;
        m_comp->emit_mark_label(label);
    }
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
