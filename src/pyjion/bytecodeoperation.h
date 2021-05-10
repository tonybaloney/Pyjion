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


#ifndef PYJION_BYTECODEOPERATION_H
#define PYJION_BYTECODEOPERATION_H

#include <vector>
#include "intstate.h"
#include "pyjit.h"

using namespace std;

class BytecodeOperation {
public:
    size_t index;
    uint16_t opcode;
    uint16_t oparg;
    Label label;
    InterpreterState state;
    bool hasState;
    AbstractSource* source;
    bool hasSource;

    AbstractSource* addLocalSource(size_t localIndex);
    AbstractSource* addConstSource(size_t constIndex, PyObject* value);
    AbstractSource* addGlobalSource(size_t constIndex, const char * name, PyObject* value);
    AbstractSource* addBuiltinSource(size_t constIndex, const char * name, PyObject* value);
    AbstractSource* addPgcSource();
    bool pgcProbeRequired(PgcStatus status);
    short pgcProbeSize();

    // Returns information about the specified local variable at a specific
    // byte code index.
    AbstractLocalInfo getLocalInfo(size_t localIndex);

    // Returns information about the stack at the specific byte code index.
    InterpreterStack& getStackInfo();
};

class BytecodeOperationSequence: public std::vector<BytecodeOperation> {

};

#endif //PYJION_BYTECODEOPERATION_H
