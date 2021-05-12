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

#ifndef PYJION_INTSTATE_H
#define PYJION_INTSTATE_H

#include "stack.h"
#include "absvalue.h"

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
    uint8_t pgcProbeSize = 0;

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

    AbstractValueWithSources pop() {
        if (mStack.empty())
            throw StackUnderflowException();
        auto res = mStack.back();
        mStack.pop_back();
        return res;
    }

    AbstractValueWithSources fromPgc(uint8_t stackPosition, PyTypeObject* pyTypeObject, PyObject* pyObject, AbstractSource* source) {
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
                    source
            );
        }
    }

    void push(AbstractValueWithSources& value) {
        mStack.push_back(value);
    }

    void push(AbstractValue* value) {
        mStack.emplace_back(value);
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


#endif //PYJION_INTSTATE_H
