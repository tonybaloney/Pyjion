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

#ifndef PYJION_CODEMODEL_H
#define PYJION_CODEMODEL_H

#include <stdint.h>
#include <windows.h>
#include <wchar.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <float.h>
#include <share.h>
#include <cstdlib>
#include <intrin.h>

#include <vector>
#include <unordered_map>

#include <corjit.h>

#include <Python.h>

#include "exceptions.h"

#define METHOD_SLOT_SPACE 0x00100000

using namespace std;

class Method;
class BaseMethod;
class CorClass;

class Parameter {
public:
    CorInfoType m_type;
    explicit Parameter(CorInfoType type) {
        m_type = type;
    }
};

class BaseModule {
public:
    unordered_map<int, BaseMethod*> m_methods;
    unordered_map<void*, int> existingSlots;
    int slotCursor = 0;
    BaseModule() = default;

    virtual BaseMethod* ResolveMethod(unsigned int tokenId) {
        return m_methods[tokenId];
    }

    virtual int AddMethod(CorInfoType returnType, std::vector<Parameter> params, void* addr);
};

class UserModule : public BaseModule {
    BaseModule& m_parent;
public:
    explicit UserModule(BaseModule& parent) : m_parent(parent) {

    }

    BaseMethod* ResolveMethod(unsigned int tokenId) override {
        auto res = m_methods.find(tokenId);
        if (res == m_methods.end()) {
            return m_parent.ResolveMethod(tokenId);
        }

        return res->second;
    }
};

struct SequencePoint {
    uint32_t ilOffset;
    uint32_t nativeOffset;
    uint32_t pythonOpcodeIndex;
};

class BaseMethod {
public:

    virtual void getCallInfo(CORINFO_CALL_INFO *pResult) = 0;
    virtual uint32_t getMethodAttrs() {
        return CORINFO_FLG_STATIC | CORINFO_FLG_NATIVE ;
    }
    virtual void findSig(CORINFO_SIG_INFO  *sig) = 0;
    virtual void* getAddr() = 0;
    virtual void getFunctionEntryPoint(CORINFO_CONST_LOOKUP *  pResult) = 0;
    virtual unsigned int getSequencePointCount() = 0;
    virtual uint32_t* getSequencePointOffsets() = 0;
    virtual void recordSequencePointOffsetPosition(uint32_t ilOffset, uint32_t nativeOffset) = 0;
    virtual vector<SequencePoint> getSequencePoints() = 0;
};

class JITMethod : public BaseMethod {
    BaseModule* m_module;
public:
    vector<Parameter> m_params;
    CorInfoType m_retType;
    void* m_addr;
    vector<SequencePoint> m_sequencePoints;

    JITMethod(BaseModule* module, CorInfoType returnType, std::vector<Parameter> params, void* addr) {
        m_retType = returnType;
        m_params = params;
        m_module = module;
        m_addr = addr;
    }

    JITMethod(BaseModule *module, CorInfoType returnType, vector<struct Parameter> params, void* addr,
              vector<pair<size_t, uint32_t>> sequencePoints) :
            JITMethod(module, returnType, params, addr){
        for (auto & point: sequencePoints){
            m_sequencePoints.push_back({
                static_cast<uint32_t>(point.first),
                0,
                point.second
            });
        }
    }

    void* getAddr() override {
        return m_addr;
    }

    void getCallInfo(CORINFO_CALL_INFO *pResult) override {
        pResult->codePointerLookup.lookupKind.needsRuntimeLookup = false;
        pResult->codePointerLookup.constLookup.accessType = IAT_PVALUE;
        pResult->codePointerLookup.constLookup.addr = &m_addr;
        pResult->verMethodFlags = pResult->methodFlags = CORINFO_FLG_STATIC;
        pResult->kind = CORINFO_CALL;
        pResult->sig.args = (CORINFO_ARG_LIST_HANDLE)(m_params.empty() ? nullptr : &m_params[0]);
        pResult->sig.retType = m_retType;
        pResult->sig.numArgs = m_params.size();
    }
    void findSig(CORINFO_SIG_INFO  *sig) override {
        sig->retType = m_retType;
        sig->callConv = CORINFO_CALLCONV_DEFAULT;
        sig->retTypeClass = nullptr;
        sig->args = (CORINFO_ARG_LIST_HANDLE)(!m_params.empty() ? &m_params[0] : nullptr);
        sig->numArgs = m_params.size();
    }
    void getFunctionEntryPoint(CORINFO_CONST_LOOKUP *  pResult) override {
        pResult->accessType = IAT_PVALUE;
        pResult->addr = &m_addr;
    }

    unsigned int getSequencePointCount() override {
        return static_cast<unsigned int>(m_sequencePoints.size());
    }

    uint32_t * getSequencePointOffsets() override {
        uint32_t * pts = static_cast<uint32_t *>(PyMem_RawMalloc(sizeof(uint32_t) * m_sequencePoints.size()));
        if (pts == nullptr){
            throw OutOfMemoryException();
        }
        size_t i = 0;
        for (auto & p: m_sequencePoints){
            pts[i] = p.ilOffset;
            i++;
        }
        return pts;
    }

    void recordSequencePointOffsetPosition(uint32_t ilOffset, uint32_t nativeOffset) override {
        for (auto & pt: m_sequencePoints){
            if (pt.ilOffset == ilOffset){
                pt.nativeOffset = nativeOffset;
            }
        }
    }

    vector<SequencePoint> getSequencePoints() override {
        return m_sequencePoints;
    }
};


#endif