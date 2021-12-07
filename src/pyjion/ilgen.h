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

#ifndef PYJION_ILGEN_H
#define PYJION_ILGEN_H

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
#include <cstdlib>
#include <intrin.h>

#include <utility>
#include <vector>
#include <unordered_map>

#include <corjit.h>
#include <openum.h>

#include "codemodel.h"
#include "ipycomp.h"
#include "disasm.h"

using namespace std;

class LabelInfo : public PyjionBase {
public:
    ssize_t m_location;
    vector<ssize_t> m_branchOffsets;

    LabelInfo() {
        m_location = -1;
    }
};

struct CorInfoTypeHash {
    std::size_t operator()(CorInfoType e) const {
        return static_cast<std::size_t>(e);
    }
};

class ILGenerator : public PyjionBase {
    vector<Parameter> m_params, m_locals;
    CorInfoType m_retType;
    BaseModule* m_module;
    unordered_map<CorInfoType, vector<Local>, CorInfoTypeHash> m_freedLocals;
    vector<pair<size_t, uint32_t>> m_sequencePoints;
    vector<pair<size_t, int32_t>> m_callPoints;

public:
    vector<BYTE> m_il;
    uint16_t m_localCount;
    vector<LabelInfo> m_labels;

public:
    ILGenerator(BaseModule* module, CorInfoType returnType, std::vector<Parameter> params) {
        m_module = module;
        m_retType = returnType;
        m_params = std::move(params);
        m_localCount = 0;
    }

    Local define_local(Parameter param) {
        auto existing = m_freedLocals.find(param.m_type);
        if (existing != m_freedLocals.end() && !existing->second.empty()) {
            auto res = existing->second[existing->second.size() - 1];
            existing->second.pop_back();
            return res;
        }
        return define_local_no_cache(param);
    }

    Local define_local_no_cache(Parameter param) {
        m_locals.emplace_back(param);
        return Local(m_localCount++);
    }

    void free_local(Local local) {
        auto param = m_locals[local.m_index];
        auto existing = m_freedLocals.find(param.m_type);
        vector<Local>* localList;
        if (existing == m_freedLocals.end()) {
            m_freedLocals[param.m_type] = vector<Local>();
            localList = &(m_freedLocals.find(param.m_type)->second);
        } else {
            localList = &(existing->second);
        }
        for (auto& i : *localList) {
            if (i.m_index == local.m_index) {
                // locals shouldn't be double freed...
                throw UnexpectedValueException();
            }
        }
        localList->emplace_back(local);
    }

    Label define_label() {
        m_labels.emplace_back();
        return Label((ssize_t) m_labels.size() - 1);
    }

    void mark_label(Label label) {
        auto info = &m_labels[label.m_index];
        info->m_location = (ssize_t) m_il.size();
        for (size_t i = 0; i < info->m_branchOffsets.size(); i++) {
            auto from = info->m_branchOffsets[i];
            auto offset = info->m_location - (from + 4);// relative to the end of the instruction
            m_il[from] = offset & 0xFF;
            m_il[from + 1] = (offset >> 8) & 0xFF;
            m_il[from + 2] = (offset >> 16) & 0xFF;
            m_il[from + 3] = (offset >> 24) & 0xFF;
        }
    }

    void brk() {
        // emit a breakpoint in the IL
        push_back(CEE_BREAK);
    }

    void ret() {
        push_back(CEE_RET);// VarPop (size)
    }

    void ld_r8(double i) {
        push_back(CEE_LDC_R8);// Pop0 + PushR8
        auto* value = (unsigned char*) (&i);
        for (size_t j = 0; j < 8; j++) {
            push_back(value[j]);
        }
    }

    void ld_i4(int32_t i) {
        switch (i) {
            case -1:
                push_back(CEE_LDC_I4_M1);
                break;
            case 0:
                push_back(CEE_LDC_I4_0);
                break;
            case 1:
                push_back(CEE_LDC_I4_1);
                break;
            case 2:
                push_back(CEE_LDC_I4_2);
                break;
            case 3:
                push_back(CEE_LDC_I4_3);
                break;
            case 4:
                push_back(CEE_LDC_I4_4);
                break;
            case 5:
                push_back(CEE_LDC_I4_5);
                break;
            case 6:
                push_back(CEE_LDC_I4_6);
                break;
            case 7:
                push_back(CEE_LDC_I4_7);
                break;
            case 8:
                push_back(CEE_LDC_I4_8);
                break;
            default:
                if (i > -128 && i < 128) {
                    push_back(CEE_LDC_I4_S);
                    push_back((BYTE) i);
                } else {
                    push_back(CEE_LDC_I4);
                    emit_int(i);
                }
        }
    }

    void ld_u4(uint32_t i) {
        ld_i4(i);
        push_back(CEE_CONV_U4);
    }

    void ld_i8(int64_t i) {
        push_back(CEE_LDC_I8);// Pop0 + PushI8
        auto* value = (unsigned char*) (&i);
        for (int j = 0; j < 8; j++) {
            push_back(value[j]);
        }
    }

    void load_null() {
        ld_i4(0);
        push_back(CEE_CONV_I);// Pop1 + PushI
    }

    void load_one() {
        ld_i4(1);
        push_back(CEE_CONV_I);// Pop1 + PushI
    }

    void st_ind_i() {
        push_back(CEE_STIND_I);// PopI + PopI / Push0
    }

    void ld_ind_i() {
        push_back(CEE_LDIND_I);// PopI / PushI
    }

    void st_ind_i4() {
        push_back(CEE_STIND_I4);// PopI + PopI / Push0
    }

    void st_ind_i8() {
        push_back(CEE_STIND_I8);// PopI + PopI / Push0
    }

    void ld_ind_i1() {
        push_back(CEE_LDIND_I1);// PopI + PopI / Push0
    }

    void ld_ind_u1() {
        push_back(CEE_LDIND_U1);// PopI + PopI / Push0
    }

    void ld_ind_i4() {
        push_back(CEE_LDIND_I4);// PopI  / PushI
    }

    void ld_ind_i8() {
        push_back(CEE_LDIND_I8);// PopI  / PushI
    }

    void ld_ind_r8() {
        push_back(CEE_LDIND_R8);// PopI + PopI / Push0
    }

    void branch(BranchType branchType, Label label) {
        auto info = &m_labels[label.m_index];
        if (info->m_location == -1) {
            info->m_branchOffsets.push_back((int) m_il.size() + 1);
            branch(branchType, 0xFFFF);
        } else {
            branch(branchType, (int) (info->m_location - m_il.size()));
        }
    }

    void branch(BranchType branchType, int offset) {
        /*
            For jump offsets that can fit into a single byte, there is a "_S"
            suffix to the CIL opcode to notate that the jump address is a single byte.
            The default is 4 bytes. 
        */
        if ((offset - 2) <= 128 && (offset - 2) >= -127) {
            switch (branchType) {
                case BranchAlways:
                    push_back(CEE_BR_S);// Pop0 + Push0
                    break;
                case BranchTrue:
                    push_back(CEE_BRTRUE_S);// PopI + Push0
                    break;
                case BranchFalse:
                    push_back(CEE_BRFALSE_S);// PopI, Push0
                    break;
                case BranchEqual:
                    push_back(CEE_BEQ_S);// Pop1+Pop1, Push0
                    break;
                case BranchNotEqual:
                    push_back(CEE_BNE_UN_S);// Pop1+Pop1, Push0
                    break;
                case BranchLeave:
                    push_back(CEE_LEAVE_S);// Pop0 + Push0
                    break;
                case BranchLessThanEqual:
                    push_back(CEE_BLE_S);// Pop1+Pop1, Push0
                    break;
                case BranchLessThanEqualUnsigned:
                    push_back(CEE_BLE_UN_S);// Pop1+Pop1, Push0
                    break;
                case BranchGreaterThan:
                    push_back(CEE_BGT_S);// Pop1+Pop1, Push0
                    break;
                case BranchGreaterThanUnsigned:
                    push_back(CEE_BGT_UN_S);// Pop1+Pop1, Push0
                    break;
                case BranchGreaterThanEqual:
                    push_back(CEE_BGE_S);// Pop1+Pop1, Push0
                    break;
                case BranchGreaterThanEqualUnsigned:
                    push_back(CEE_BGE_UN_S);// Pop1+Pop1, Push0
                    break;
                case BranchLessThan:
                    push_back(CEE_BLT_S);// Pop1+Pop1, Push0
                    break;
                case BranchLessThanUnsigned:
                    push_back(CEE_BLT_UN_S);// Pop1+Pop1, Push0
                    break;
            }
            push_back((BYTE) offset - 2);
        } else {
            switch (branchType) {
                case BranchAlways:
                    push_back(CEE_BR);// Pop0 + Push0
                    break;
                case BranchTrue:
                    push_back(CEE_BRTRUE);// PopI + Push0
                    break;
                case BranchFalse:
                    push_back(CEE_BRFALSE);// PopI, Push0
                    break;
                case BranchEqual:
                    push_back(CEE_BEQ);// Pop1+Pop1, Push0
                    break;
                case BranchNotEqual:
                    push_back(CEE_BNE_UN);// Pop1+Pop1, Push0
                    break;
                case BranchLeave:
                    push_back(CEE_LEAVE);// Pop0 + Push0
                    break;
                case BranchLessThanEqual:
                    push_back(CEE_BLE);// Pop1+Pop1, Push0
                    break;
                case BranchLessThanEqualUnsigned:
                    push_back(CEE_BLE_UN);// Pop1+Pop1, Push0
                    break;
                case BranchGreaterThan:
                    push_back(CEE_BGT);// Pop1+Pop1, Push0
                    break;
                case BranchGreaterThanUnsigned:
                    push_back(CEE_BGT_UN);// Pop1+Pop1, Push0
                    break;
                case BranchGreaterThanEqual:
                    push_back(CEE_BGE);// Pop1+Pop1, Push0
                    break;
                case BranchGreaterThanEqualUnsigned:
                    push_back(CEE_BGE_UN);// Pop1+Pop1, Push0
                    break;
                case BranchLessThan:
                    push_back(CEE_BLT);// Pop1+Pop1, Push0
                    break;
                case BranchLessThanUnsigned:
                    push_back(CEE_BLT_UN);// Pop1+Pop1, Push0
                    break;
            }
            emit_int(offset - 5);
        }
    }

    void neg() {
        push_back(CEE_NEG);//  Pop1, Push1
    }

    void dup() {
        push_back(CEE_DUP);//  Pop1, Push1+Push1
    }

    void bitwise_and() {
        push_back(CEE_AND);//  Pop1+Pop1, Push1
    }

    void bitwise_or(){
        push_back(CEE_OR);
    }

    void bitwise_xor(){
        push_back(CEE_XOR);
    }

    void bitwise_not(){
        push_back(CEE_NOT);
    }

    void lshift() {
        push_back(CEE_SHL);
    }

    void rshift() {
        push_back(CEE_SHR);
    }

    void pop() {
        push_back(CEE_POP);//  Pop1, Push0
    }

    void compare_eq() {
        push_back(CEE_PREFIX1);   // NIL SE
        push_back((BYTE) CEE_CEQ);//  Pop1+Pop1, PushI
    }

    void compare_ne() {
        compare_eq();
        ld_i4(0);
        compare_eq();
    }

    void compare_gt() {
        push_back(CEE_PREFIX1);   // NIL SE
        push_back((BYTE) CEE_CGT);//  Pop1+Pop1, PushI
    }

    void compare_lt() {
        push_back(CEE_PREFIX1);   // NIL
        push_back((BYTE) CEE_CLT);//  Pop1+Pop1, PushI
    }

    void compare_ge() {
        push_back(CEE_PREFIX1);   // NIL
        push_back((BYTE) CEE_CLT);//  Pop1+Pop1, PushI
        ld_i4(0);
        compare_eq();
    }

    void compare_le() {
        push_back(CEE_PREFIX1);   // NIL
        push_back((BYTE) CEE_CGT);//  Pop1+Pop1, PushI
        ld_i4(0);
        compare_eq();
    }

    void compare_ge_float() {
        push_back(CEE_PREFIX1);      // NIL
        push_back((BYTE) CEE_CLT_UN);//  Pop1+Pop1, PushI
        ld_i4(0);
        compare_eq();
    }

    void compare_le_float() {
        push_back(CEE_PREFIX1);      // NIL
        push_back((BYTE) CEE_CGT_UN);//  Pop1+Pop1, PushI
        ld_i4(0);
        compare_eq();
    }

    void conv_i(){
        push_back(CEE_CONV_I);
    }

    void conv_r8() {
        push_back(CEE_CONV_R8);
    }

    void ld_i(int32_t i) {
        push_back(CEE_LDC_I4);
        emit_int(i);
        push_back(CEE_CONV_I);// Pop1, PushI
    }

    void ld_i(void* ptr) {
        auto value = (size_t) ptr;
#ifdef HOST_64BIT
        if ((value & 0xFFFFFFFF) == value) {
            ld_i((int) value);
        } else {
            push_back(CEE_LDC_I8);// Pop0, PushI8
            push_back(value & 0xff);
            push_back((value >> 8) & 0xff);
            push_back((value >> 16) & 0xff);
            push_back((value >> 24) & 0xff);
            push_back((value >> 32) & 0xff);
            push_back((value >> 40) & 0xff);
            push_back((value >> 48) & 0xff);
            push_back((value >> 56) & 0xff);
            push_back(CEE_CONV_I);// Pop1, PushI
        }
#else
        ld_i(value);
        push_back(CEE_CONV_I);
#endif
    }

    void emit_call(int32_t token) {
        m_callPoints.emplace_back(make_pair(m_il.size(), token));

        push_back(CEE_CALL);// VarPop, VarPush
        emit_int(token);
    }

    void st_loc(Local param) {
        param.raiseOnInvalid();
        st_loc(param.m_index);
    }

    void ld_loc(Local param) {
        param.raiseOnInvalid();
        ld_loc(param.m_index);
    }

    void ld_loca(Local param) {
        param.raiseOnInvalid();
        ld_loca(param.m_index);
    }

    void st_loc(uint16_t index) {
        switch (index) {
            case 0:
                push_back(CEE_STLOC_0);
                break;
            case 1:
                push_back(CEE_STLOC_1);
                break;
            case 2:
                push_back(CEE_STLOC_2);
                break;
            case 3:
                push_back(CEE_STLOC_3);
                break;
            default:
                if (index < 256) {
                    push_back(CEE_STLOC_S);
                    push_back(index);
                } else {
                    push_back(CEE_PREFIX1);// NIL
                    push_back((BYTE) CEE_STLOC);
                    push_back(index & 0xff);
                    push_back((index >> 8) & 0xff);
                }
        }
    }

    void ld_loc(uint16_t index) {
        switch (index) {
            case 0:
                push_back(CEE_LDLOC_0);
                break;
            case 1:
                push_back(CEE_LDLOC_1);
                break;
            case 2:
                push_back(CEE_LDLOC_2);
                break;
            case 3:
                push_back(CEE_LDLOC_3);
                break;
            default:
                if (index < 256) {
                    push_back(CEE_LDLOC_S);
                    push_back((BYTE) index);
                } else {
                    push_back(CEE_PREFIX1);// NIL
                    push_back((BYTE) CEE_LDLOC);
                    push_back(index & 0xff);
                    push_back((index >> 8) & 0xff);
                }
        }
    }

    void ld_loca(uint16_t index) {
        if (index < 256) {
            push_back(CEE_LDLOCA_S);// Pop0, PushI
            push_back(index);
        } else {
            push_back(CEE_PREFIX1);      // NIL
            push_back((BYTE) CEE_LDLOCA);// Pop0, PushI
            push_back(index & 0xff);
            push_back((index >> 8) & 0xff);
        }
    }

    void add() {
        push_back(CEE_ADD);// Pop1+Pop1, Push1
    }

    void sub() {
        push_back(CEE_SUB);// Pop1+Pop1, Push1
    }

    void sub_with_overflow() {
        push_back(CEE_SUB_OVF);// Pop1+Pop1, Push1
    }

    void div() {
        push_back(CEE_DIV);// Pop1+Pop1, Push1
    }

    void mod() {
        push_back(CEE_REM);// Pop1+Pop1, Push1
    }

    void mul() {
        push_back(CEE_MUL);// Pop1+Pop1, Push1
    }

    void ld_arg(uint16_t index) {
        switch (index) {
            case 0:
                push_back(CEE_LDARG_0);// Pop0, Push1
                break;
            case 1:
                push_back(CEE_LDARG_1);// Pop0, Push1
                break;
            case 2:
                push_back(CEE_LDARG_2);// Pop0, Push1
                break;
            case 3:
                push_back(CEE_LDARG_3);// Pop0, Push1
                break;
            default:
                if (index < 256) {
                    push_back(CEE_LDARG_S);// Pop0, Push1
                    push_back(index);
                } else {
                    push_back(CEE_PREFIX1);     // NIL
                    push_back((BYTE) CEE_LDARG);// Pop0, Push1
                    push_back(index & 0xff);
                    push_back((index >> 8) & 0xff);
                }

                break;
        }
    }

    void newarr(int32_t size, CorInfoType type) {
        ld_i4(size);
        push_back(CEE_NEWARR);
        emit_int(type);
    }

    void mark_sequence_point(size_t idx) {
#ifdef DUMP_SEQUENCE_POINTS
        printf("Sequence Point: IL_%04lX: %zu\n", m_il.size(), idx);
#endif
        m_sequencePoints.emplace_back(make_pair(m_il.size(), idx));
    }

    CORINFO_METHOD_INFO to_method(JITMethod* addr, size_t stackSize) {
        CORINFO_METHOD_INFO methodInfo{};
        methodInfo.ftn = (CORINFO_METHOD_HANDLE) addr;
        methodInfo.scope = (CORINFO_MODULE_HANDLE) m_module;
        methodInfo.ILCode = &m_il[0];
        methodInfo.ILCodeSize = (unsigned int) m_il.size();
        methodInfo.maxStack = stackSize;
        methodInfo.EHcount = 0;
        methodInfo.options = CORINFO_OPT_INIT_LOCALS;
        methodInfo.regionKind = CORINFO_REGION_JIT;
        methodInfo.args = CORINFO_SIG_INFO{CORINFO_CALLCONV_DEFAULT};
        methodInfo.args.args = (CORINFO_ARG_LIST_HANDLE) (m_params.empty() ? nullptr : &m_params[0]);
        methodInfo.args.numArgs = m_params.size();
        methodInfo.args.retType = m_retType;
        methodInfo.args.retTypeClass = nullptr;
        methodInfo.locals = CORINFO_SIG_INFO{CORINFO_CALLCONV_DEFAULT};
        methodInfo.locals.args = (CORINFO_ARG_LIST_HANDLE) (m_locals.empty() ? nullptr : &m_locals[0]);
        methodInfo.locals.numArgs = m_locals.size();
        return methodInfo;
    }

    JITMethod compile(CorJitInfo* jitInfo, ICorJitCompiler* jit, size_t stackSize) {
        uint8_t* nativeEntry;
        uint32_t nativeSizeOfCode;
        jitInfo->assignIL(m_il);
        auto res = JITMethod(m_module, m_retType, m_params, nullptr, m_sequencePoints, m_callPoints, false);
        CORINFO_METHOD_INFO methodInfo = to_method(&res, stackSize);
#if (defined(HOST_OSX) && defined(HOST_ARM64))
        pthread_jit_write_protect_np(0);
#endif
        CorJitResult result = jit->compileMethod(
                jitInfo,
                &methodInfo,
                CORJIT_FLAGS::CORJIT_FLAG_CALL_GETJITFLAGS,
                &nativeEntry,
                &nativeSizeOfCode);
#if (defined(HOST_OSX) && defined(HOST_ARM64))
        pthread_jit_write_protect_np(1);
#endif
        jitInfo->setNativeSize(nativeSizeOfCode);
        switch (result) {
            case CORJIT_OK:
                res.m_addr = nativeEntry;
                break;
            case CORJIT_BADCODE:
#ifdef DEBUG_VERBOSE
                printf("JIT failed to compile the submitted method.\n");
#endif
                res.m_addr = nullptr;
                break;
            case CORJIT_OUTOFMEM:
#ifdef DEBUG_VERBOSE
                printf("out of memory.\n");
#endif
                res.m_addr = nullptr;
                break;
            case CORJIT_IMPLLIMITATION:
            case CORJIT_INTERNALERROR:
#ifdef DEBUG_VERBOSE
                printf("internal error code.\n");
#endif
                res.m_addr = nullptr;
                break;
            case CORJIT_SKIPPED:
#ifdef DEBUG_VERBOSE
                printf("skipped code.\n");
#endif
                res.m_addr = nullptr;
                break;
            case CORJIT_RECOVERABLEERROR:
#ifdef DEBUG_VERBOSE
                printf("recoverable error code.\n");
#endif
                res.m_addr = nullptr;
                break;
        }
        return res;
    }


private:
    void emit_int(int32_t value) {
        push_back(value & 0xff);
        push_back((value >> 8) & 0xff);
        push_back((value >> 16) & 0xff);
        push_back((value >> 24) & 0xff);
    }

    void push_back(BYTE b) {
        m_il.emplace_back(b);
    }
};

#endif