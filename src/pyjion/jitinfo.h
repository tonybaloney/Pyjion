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

#ifndef PYJION_JITINFO_H
#define PYJION_JITINFO_H

#include <Python.h>
#include <frameobject.h>
#include <opcode.h>
#include <windows.h>
#include <share.h>
#include <intrin.h>

#include <vector>
#include <unordered_map>
#include <cmath>
#include <corjit.h>

#include <openum.h>

#include "codemodel.h"
#include "cee.h"
#include "ipycomp.h"
#include "exceptions.h"

#ifndef WINDOWS
#include <sys/mman.h>
#endif

#ifdef DEBUG
#define WARN(msg, ...) printf(#msg, ##__VA_ARGS__);
#else
#define WARN(msg, ...) 
#endif

using namespace std;

extern "C" void JIT_StackProbe(); // Implemented in helpers.asm

const CORINFO_CLASS_HANDLE PYOBJECT_PTR_TYPE = (CORINFO_CLASS_HANDLE)0x11;

class CorJitInfo : public ICorJitInfo, public JittedCode {
    void* m_codeAddr;
    void* m_dataAddr;
    PyCodeObject *m_code;
    UserModule* m_module;
    vector<uint8_t> m_il;
    uint32_t m_nativeSize;
    vector<SequencePoint> m_sequencePoints;
    bool m_compileDebug;

    volatile const GSCookie s_gsCookie = 0x1234;

#ifdef WINDOWS
    HANDLE m_winHeap;
    SYSTEM_INFO systemInfo;
#endif

public:

    CorJitInfo(PyCodeObject* code, UserModule* module, bool compileDebug) {
        m_codeAddr = m_dataAddr = nullptr;
        m_code = code;
        m_module = module;
        m_il = vector<uint8_t>(0);
        m_nativeSize = 0;
        m_compileDebug = compileDebug;
#ifdef WINDOWS
        m_winHeap = HeapCreate(HEAP_CREATE_ENABLE_EXECUTE, 0, 0);
        GetSystemInfo(&systemInfo);
#endif
    }

    ~CorJitInfo() override {
        if (m_codeAddr != nullptr) {
            freeMem(m_codeAddr);
        }
        if (m_dataAddr != nullptr) {
            free(m_dataAddr);
        }
#ifdef WINDOWS
        HeapDestroy(m_winHeap);
#endif
        delete m_module;
    }

    /// Empty breakpoint function, put some bonus code in here if you want to debug anything between
    /// CPython opcodes.
    static void breakpointFtn() {};

    static void raiseOverflowExceptionHelper() {
        throw IntegerOverflowException();
    };

    static void rangeCheckExceptionHelper() {
        throw RangeCheckException();
    };

    static void divisionByZeroExceptionHelper() {
        throw DivisionByZeroException();
    };

    static void nullReferenceExceptionHelper() {
        throw NullReferenceException();
    };

    static void verificationExceptionHelper() {
        throw CilVerficationException();
    };

    static void securityExceptionHelper() {
        throw UnmanagedCodeSecurityException();
    };

    static void failFastExceptionHelper() {
        throw GuardStackException();
    };

    static double dblRemHelper(double dividend, double divisor) {
        if (divisor==0 || !isfinite(dividend))
        {
            return INFINITY;
        }
        else if (!isfinite(divisor) && !isnan(divisor))
        {
            return dividend;
        }
        else {
            return fmod(dividend, divisor);
        }
    };

    /// Override the default .NET CIL_NEWARR with a custom array allocator. See getHelperFtn
    /// \param size Requested array size
    /// \param arrayMT Array type handle
    /// \return new vector
    static vector<PyObject*> newArrayHelperFtn(int64_t size, CORINFO_CLASS_HANDLE arrayMT) {
        return std::vector<PyObject*>(size);
    }

    static void stArrayHelperFtn(std::vector<PyObject*>* array, INT_PTR idx, PyObject* ref) {
        // TODO : Implement vector allocation and assignment logic for CIL_STELEM.x
    }

    void* get_code_addr() override {
        return m_codeAddr;
    }

    unsigned char* get_il() override {
        return (unsigned char *)m_il.data();
    }

    size_t get_il_len() override {
        return m_il.size();
    }

    size_t get_native_size() override {
        return m_nativeSize;
    }

    SequencePoint* get_sequence_points() override {
        if (!m_sequencePoints.empty())
            return &m_sequencePoints[0];
        else
            return nullptr;
    }

    size_t get_sequence_points_length() override {
        return m_sequencePoints.size();
    }

    void freeMem(PVOID code) {
        // TODO: Validate memory free for CorJitInfo
        PyMem_Free(code);
    }

    void allocMem(
        AllocMemArgs *pArgs
        ) override {
        // NB: Not honouring flag alignment requested in <flag>, but it is "optional"
#ifdef WINDOWS
        pArgs->hotCodeBlock = m_codeAddr = HeapAlloc(m_winHeap, 0 , pArgs->hotCodeSize);
#else
#if defined(__APPLE__) && defined(MAP_JIT)
        const int mode = MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT;
#elif defined(MAP_ANONYMOUS)
        const int mode = MAP_PRIVATE | MAP_ANONYMOUS;
#elif defined(MAP_ANON)
		const int mode = MAP_PRIVATE | MAP_ANON;
#else
		#error "not supported"
#endif
        pArgs->hotCodeBlock = m_codeAddr = mmap(
                nullptr,
                pArgs->hotCodeSize,
                PROT_READ | PROT_WRITE | PROT_EXEC,
                mode,
                -1,
                0);
        assert (pArgs->hotCodeBlock != MAP_FAILED);
#endif

        if (pArgs->coldCodeSize>0) // PyMem_Malloc passes with 0 but it confuses the JIT
            pArgs->coldCodeBlock = PyMem_Malloc(pArgs->coldCodeSize);
        if (pArgs->roDataSize>0) // Same as above
            pArgs->roDataBlock = PyMem_Malloc(pArgs->roDataSize);

        pArgs->hotCodeBlockRW = pArgs->hotCodeBlock;
        pArgs->coldCodeBlockRW = pArgs->coldCodeBlock;
        pArgs->roDataBlockRW = pArgs->roDataBlock;
    }

    bool logMsg(unsigned level, const char* fmt, va_list args) override {
#ifdef REPORT_CLR_FAULTS
        if (level <= 3)
            vprintf(fmt, args);
        return false;
#else
        return true;
#endif
    }

    int doAssert(const char* szFile, int iLine, const char* szExpr) override {
#ifdef REPORT_CLR_FAULTS
        printf(".NET failed assertion: %s %d %s\n", szFile, iLine, szExpr);
#endif
        return 1;
    }

    void reportFatalError(CorJitResult result) override {
#ifdef DEBUG
        printf("Fatal error from .NET JIT %X\r\n", result);
#endif
    }

    void recordRelocation(
        void *                 location,   /* IN  */
        void *                 locationRW, /* IN  */
        void *                 target,     /* IN  */
        uint16_t                   fRelocType, /* IN  */
        uint16_t                   slotNum,  /* IN  */
        int32_t                  addlDelta /* IN  */
        ) override {
        switch (fRelocType) {
            case IMAGE_REL_BASED_DIR64:
                *((uint64_t *)((uint8_t *)location + slotNum)) = (uint64_t)target;
                break;
#ifdef _TARGET_AMD64_
            case IMAGE_REL_BASED_REL32:
            {
                target = (uint8_t *)target + addlDelta;

                auto * fixupLocation = (int32_t *)((uint8_t *)location + slotNum);
                uint8_t * baseAddr = (uint8_t *)fixupLocation + sizeof(int32_t);

                auto delta = (int64_t)((uint8_t *)target - baseAddr);

                // Write the 32-bits pc-relative delta into location
                *fixupLocation = (int32_t)delta;
            }
            break;
#endif // _TARGET_AMD64_
            default:
                printf("Unsupported relocation type (%d)\r\n", fRelocType);
        }
    }

    uint16_t getRelocTypeHint(void * target) override {
        return -1;
    }

    // For what machine does the VM expect the JIT to generate code? The VM
    // returns one of the IMAGE_FILE_MACHINE_* values. Note that if the VM
    // is cross-compiling (such as the case for crossgen), it will return a
    // different value than if it was compiling for the host architecture.
    // 
    uint32_t getExpectedTargetArchitecture() override {
#ifdef _TARGET_AMD64_
        return IMAGE_FILE_MACHINE_AMD64;
#elif defined(_TARGET_X86_)
        return IMAGE_FILE_MACHINE_I386;
#elif defined(_TARGET_ARM_)
        return IMAGE_FILE_MACHINE_ARM;
#endif
    }

    /* ICorDynamicInfo */

    //
    // These methods return values to the JIT which are not constant
    // from session to session.
    //
    // These methods take an extra parameter : void **ppIndirection.
    // If a JIT supports generation of prejit code (install-o-jit), it
    // must pass a non-null value for this parameter, and check the
    // resulting value.  If *ppIndirection is NULL, code should be
    // generated normally.  If non-null, then the value of
    // *ppIndirection is an address in the cookie table, and the code
    // generator needs to generate an indirection through the table to
    // get the resulting value.  In this case, the return result of the
    // function must NOT be directly embedded in the generated code.
    //
    // Note that if a JIT does not support prejit code generation, it
    // may ignore the extra parameter & pass the default of NULL - the
    // prejit ICorDynamicInfo implementation will see this & generate
    // an error if the jitter is used in a prejit scenario.
    //

    // Return details about EE internal data structures

    uint32_t getThreadTLSIndex(
        void                  **ppIndirection
        ) override {
        //printf("getThreadTLSIndex  not implemented\r\n");
        return 0;
    }

    const void * getInlinedCallFrameVptr(
        void                  **ppIndirection
        ) override {
        //printf("getInlinedCallFrameVptr  not implemented\r\n");
        return nullptr;
    }

    int32_t * getAddrOfCaptureThreadGlobal(
        void                  **ppIndirection
        ) override {
        //printf("getAddrOfCaptureThreadGlobal  not implemented\r\n");
        return nullptr;
    }

    // return a callable address of the function (native code). This function
    // may return a different value (depending on whether the method has
    // been JITed or not.
    void getFunctionEntryPoint(
        CORINFO_METHOD_HANDLE   ftn,                 /* IN  */
        CORINFO_CONST_LOOKUP *  pResult,             /* OUT */
        CORINFO_ACCESS_FLAGS    accessFlags) override {
        auto* method = reinterpret_cast<BaseMethod*>(ftn);
        method->getFunctionEntryPoint(pResult);
    }

    // return a directly callable address. This can be used similarly to the
    // value returned by getFunctionEntryPoint() except that it is
    // guaranteed to be multi callable entrypoint.
    void getFunctionFixedEntryPoint(
        CORINFO_METHOD_HANDLE   ftn,
        CORINFO_CONST_LOOKUP *  pResult) override {
        WARN("getFunctionFixedEntryPoint not implemented\r\n");
    }

    // get the synchronization handle that is passed to monXstatic function
    void* getMethodSync(
        CORINFO_METHOD_HANDLE               ftn,
        void                  **ppIndirection
        ) override {
        WARN("getMethodSync  not implemented\r\n");
        return nullptr;
    }

    // get slow lazy string literal helper to use (CORINFO_HELP_STRCNS*). 
    // Returns CORINFO_HELP_UNDEF if lazy string literal helper cannot be used.
    CorInfoHelpFunc getLazyStringLiteralHelper(
        CORINFO_MODULE_HANDLE   handle
        ) override {
        WARN("getLazyStringLiteralHelper\r\n");
        return CORINFO_HELP_UNDEF;
    }

    CORINFO_MODULE_HANDLE embedModuleHandle(
        CORINFO_MODULE_HANDLE   handle,
        void                  **ppIndirection
        ) override {
        WARN("embedModuleHandle  not implemented\r\n");
        return nullptr;
    }

    CORINFO_CLASS_HANDLE embedClassHandle(
        CORINFO_CLASS_HANDLE    handle,
        void                  **ppIndirection
        ) override {
        WARN("embedClassHandle  not implemented\r\n");
        return nullptr;
    }

    CORINFO_METHOD_HANDLE embedMethodHandle(
        CORINFO_METHOD_HANDLE   handle,
        void                  **ppIndirection
        ) override {
        //("embedMethodHandle  not implemented\r\n");
        *ppIndirection = nullptr;
        return handle;
    }

    CORINFO_FIELD_HANDLE embedFieldHandle(
        CORINFO_FIELD_HANDLE    handle,
        void                  **ppIndirection
        ) override {
        WARN("embedFieldHandle  not implemented\r\n");
        return nullptr;
    }

    // Generate a cookie based on the signature that would needs to be passed
    // to CORINFO_HELP_PINVOKE_CALLI
    void* GetCookieForPInvokeCalliSig(
        CORINFO_SIG_INFO* szMetaSig,
        void           ** ppIndirection
        ) override {
        WARN("GetCookieForPInvokeCalliSig  not implemented\r\n");
        return nullptr;
    }

    // returns true if a VM cookie can be generated for it (might be false due to cross-module
    // inlining, in which case the inlining should be aborted)
    bool canGetCookieForPInvokeCalliSig(
        CORINFO_SIG_INFO* szMetaSig
        ) override {
        WARN("canGetCookieForPInvokeCalliSig not implemented\r\n");
        return false;
    }

    // Gets a handle that is checked to see if the current method is
    // included in "JustMyCode"
    CORINFO_JUST_MY_CODE_HANDLE getJustMyCodeHandle(
        CORINFO_METHOD_HANDLE       method,
        CORINFO_JUST_MY_CODE_HANDLE**ppIndirection
        ) override {
        CORINFO_JUST_MY_CODE_HANDLE result;
        if (ppIndirection)
            *ppIndirection = nullptr;
        uint32_t * pFlagAddr = nullptr;
        result = (CORINFO_JUST_MY_CODE_HANDLE) pFlagAddr;
        return result;
    }

    // Gets a method handle that can be used to correlate profiling data.
    // This is the IP of a native method, or the address of the descriptor struct
    // for IL.  Always guaranteed to be unique per process, and not to move. */
    void GetProfilingHandle(
        bool                      *pbHookFunction,
        void                     **pProfilerHandle,
        bool                      *pbIndirectedHandles
        ) override {
        WARN("GetProfilingHandle not implemented\r\n");
    }

    // Returns instructions on how to make the call. See code:CORINFO_CALL_INFO for possible return values.
    void getCallInfo(
        // Token info
        CORINFO_RESOLVED_TOKEN * pResolvedToken,

        //Generics info
        CORINFO_RESOLVED_TOKEN * pConstrainedResolvedToken,

        //Security info
        CORINFO_METHOD_HANDLE   callerHandle,

        //Jit info
        CORINFO_CALLINFO_FLAGS  flags,

        //out params (OUT)
        CORINFO_CALL_INFO       *pResult
        ) override {
        auto method = reinterpret_cast<BaseMethod*>(pResolvedToken->hMethod);
        pResult->hMethod = (CORINFO_METHOD_HANDLE)method;

        method->getCallInfo(pResult);
        pResult->nullInstanceCheck = false;
        pResult->sig.callConv = CORINFO_CALLCONV_DEFAULT;
        pResult->sig.retTypeClass = nullptr;
        pResult->sig.flags = CORINFO_SIGFLAG_IS_LOCAL_SIG;
        pResult->verSig = pResult->sig;
        pResult->accessAllowed = CORINFO_ACCESS_ALLOWED;
    }

    bool canAccessFamily(CORINFO_METHOD_HANDLE hCaller,
        CORINFO_CLASS_HANDLE hInstanceType) override {
        WARN("canAccessFamily not implemented\r\n");
        return false;
    }

    // Returns true if the Class Domain ID is the RID of the class (currently true for every class
    // except reflection emitted classes and generics)
    bool isRIDClassDomainID(CORINFO_CLASS_HANDLE cls) override {
        WARN("isRIDClassDomainID not implemented\r\n");
        return false;
    }

    // returns the class's domain ID for accessing shared statics
    unsigned getClassDomainID(
        CORINFO_CLASS_HANDLE    cls,
        void                  **ppIndirection
        ) override {
        WARN("getClassDomainID not implemented\r\n");
        return 0;
    }

    // return the data's address (for static fields only)
    void* getFieldAddress(
        CORINFO_FIELD_HANDLE    field,
        void                  **ppIndirection
        ) override {
        WARN("getFieldAddress  not implemented\r\n");
        return nullptr;
    }

    // registers a vararg sig & returns a VM cookie for it (which can contain other stuff)
    CORINFO_VARARGS_HANDLE getVarArgsHandle(
        CORINFO_SIG_INFO       *pSig,
        void                  **ppIndirection
        ) override {
        WARN("getVarArgsHandle  not implemented\r\n");
        return nullptr;
    }

    // returns true if a VM cookie can be generated for it (might be false due to cross-module
    // inlining, in which case the inlining should be aborted)
    bool canGetVarArgsHandle(
        CORINFO_SIG_INFO       *pSig
        ) override {
        WARN("canGetVarArgsHandle\r\n");
        return false;
    }

    // Allocate a string literal on the heap and return a handle to it
    InfoAccessType constructStringLiteral(
        CORINFO_MODULE_HANDLE   module,
        mdToken                 metaTok,
        void                  **ppValue
        ) override {
        WARN("constructStringLiteral not implemented\r\n");
        return IAT_VALUE;
    }

    InfoAccessType emptyStringLiteral(
        void                  **ppValue
        ) override {
        WARN("emptyStringLiteral not implemented\r\n");
        return IAT_VALUE;
    }

    // return flags (defined above, CORINFO_FLG_PUBLIC ...)
    uint32_t getMethodAttribs(
        CORINFO_METHOD_HANDLE       ftn         /* IN */
        ) override {
        auto method = reinterpret_cast<BaseMethod*>(ftn);
        return method->getMethodAttrs();
    }

    // sets private JIT flags, which can be, retrieved using getAttrib.
    void setMethodAttribs(
        CORINFO_METHOD_HANDLE       ftn,        /* IN */
        CorInfoMethodRuntimeFlags   attribs     /* IN */
        ) override {
        WARN("setMethodAttribs  not implemented\r\n");
    }

    // Given a method descriptor ftnHnd, extract signature information into sigInfo
    //
    // 'memberParent' is typically only set when verifying.  It should be the
    // result of calling getMemberParent.
    void getMethodSig(
        CORINFO_METHOD_HANDLE      ftn,        /* IN  */
        CORINFO_SIG_INFO          *sig,        /* OUT */
        CORINFO_CLASS_HANDLE      memberParent /* IN */
        ) override {
        auto* m = reinterpret_cast<BaseMethod*>(ftn);
        m->findSig(sig);
    }

    /*********************************************************************
    * Note the following methods can only be used on functions known
    * to be IL.  This includes the method being compiled and any method
    * that 'getMethodInfo' returns true for
    *********************************************************************/

    // return information about a method private to the implementation
    //      returns false if method is not IL, or is otherwise unavailable.
    //      This method is used to fetch data needed to inline functions
    bool getMethodInfo(
        CORINFO_METHOD_HANDLE   ftn,            /* IN  */
        CORINFO_METHOD_INFO*    info            /* OUT */
        ) override {
        WARN("getMethodInfo  not implemented\r\n");
        return false;
    }

    // Decides if you have any limitations for inlining. If everything's OK, it will return
    // INLINE_PASS and will fill out pRestrictions with a mask of restrictions the caller of this
    // function must respect. If caller passes pRestrictions = NULL, if there are any restrictions
    // INLINE_FAIL will be returned
    //
    // The callerHnd must be the immediate caller (i.e. when we have a chain of inlined calls)
    //
    // The inlined method need not be verified

    CorInfoInline canInline(
        CORINFO_METHOD_HANDLE       callerHnd,                  /* IN  */
        CORINFO_METHOD_HANDLE       calleeHnd,                  /* IN  */
        uint32_t*                      pRestrictions               /* OUT */
        ) override {
        WARN("canInline not implemented\r\n");
        return INLINE_PASS;
    }

    // Reports whether or not a method can be inlined, and why.  canInline is responsible for reporting all
    // inlining results when it returns INLINE_FAIL and INLINE_NEVER.  All other results are reported by the
    // JIT.
    void reportInliningDecision(CORINFO_METHOD_HANDLE inlinerHnd,
        CORINFO_METHOD_HANDLE inlineeHnd,
        CorInfoInline inlineResult,
        const char * reason) override {
        if (inlineResult == CorInfoInline::INLINE_FAIL) {
            // This happens a lot. Investigate why far in the future...
            // WARN("Inlining failed : %s.\r\n", reason);
        }
    }


    // Returns false if the call is across security boundaries thus we cannot tailcall
    //
    // The callerHnd must be the immediate caller (i.e. when we have a chain of inlined calls)
    bool canTailCall(
        CORINFO_METHOD_HANDLE   callerHnd,          /* IN */
        CORINFO_METHOD_HANDLE   declaredCalleeHnd,  /* IN */
        CORINFO_METHOD_HANDLE   exactCalleeHnd,     /* IN */
        bool fIsTailPrefix                          /* IN */
        ) override {
        return false;
    }

    // Reports whether or not a method can be tail called, and why.
    // canTailCall is responsible for reporting all results when it returns
    // false.  All other results are reported by the JIT.
    void reportTailCallDecision(CORINFO_METHOD_HANDLE callerHnd,
        CORINFO_METHOD_HANDLE calleeHnd,
        bool fIsTailPrefix,
        CorInfoTailCall tailCallResult,
        const char * reason) override {
        WARN("reportTailCallDecision\r\n");
    }

    // get individual exception handler
    void getEHinfo(
        CORINFO_METHOD_HANDLE ftn,              /* IN  */
        unsigned          EHnumber,             /* IN */
        CORINFO_EH_CLAUSE* clause               /* OUT */
        ) override {
        WARN("getEHinfo not implemented\r\n");
    }

    // return class it belongs to
    CORINFO_CLASS_HANDLE getMethodClass(
        CORINFO_METHOD_HANDLE       method
        ) override {
        return nullptr; // Pyjion does not use CLR classes
    }

    // return module it belongs to
    CORINFO_MODULE_HANDLE getMethodModule(
        CORINFO_METHOD_HANDLE       method
        ) override {
        //printf("getMethodModule  not implemented\r\n");
        return nullptr;
    }

    // return if any marshaling is required for PInvoke methods.  Note that
    // method == 0 => calli.  The call site sig is only needed for the varargs or calli case
    bool pInvokeMarshalingRequired(
        CORINFO_METHOD_HANDLE       method,
        CORINFO_SIG_INFO*           callSiteSig
        ) override {
        WARN("pInvokeMarshalingRequired not implemented\r\n");
        return true;
    }

    // Check constraints on method type arguments (only).
    // The parent class should be checked separately using satisfiesClassConstraints(parent).
    bool satisfiesMethodConstraints(
        CORINFO_CLASS_HANDLE        parent, // the exact parent of the method
        CORINFO_METHOD_HANDLE       method
        ) override {
        WARN("satisfiesMethodConstraints not implemented\r\n"); 
        return true;
    }

    // Given a delegate target class, a target method parent class,  a  target method,
    // a delegate class, check if the method signature is compatible with the Invoke method of the delegate
    // (under the typical instantiation of any free type variables in the memberref signatures).
    bool isCompatibleDelegate(
        CORINFO_CLASS_HANDLE        objCls,           /* type of the delegate target, if any */
        CORINFO_CLASS_HANDLE        methodParentCls,  /* exact parent of the target method, if any */
        CORINFO_METHOD_HANDLE       method,           /* (representative) target method, if any */
        CORINFO_CLASS_HANDLE        delegateCls,      /* exact type of the delegate */
        bool                        *pfIsOpenDelegate /* is the delegate open */
        ) override {
        WARN("isCompatibleDelegate not implemented\r\n");
        return true;
    }

    // Determines whether the delegate creation obeys security transparency rules
    virtual bool isDelegateCreationAllowed(
        CORINFO_CLASS_HANDLE        delegateHnd,
        CORINFO_METHOD_HANDLE       calleeHnd
        ) {
        WARN("isDelegateCreationAllowed not implemented\r\n");
        return false;
    }

    // load and restore the method
    void methodMustBeLoadedBeforeCodeIsRun(
        CORINFO_METHOD_HANDLE       method
        ) override {
        WARN("methodMustBeLoadedBeforeCodeIsRun\r\n");
    }

    CORINFO_METHOD_HANDLE mapMethodDeclToMethodImpl(
        CORINFO_METHOD_HANDLE       method
        ) override {
        WARN("mapMethodDeclToMethodImpl\r\n");
        return nullptr;
    }

    // Returns the global cookie for the /GS unsafe buffer checks
    // The cookie might be a constant value (JIT), or a handle to memory location (Ngen)
    void getGSCookie(
        GSCookie * pCookieVal,                     // OUT
        GSCookie ** ppCookieVal                    // OUT
        ) override {
        if (pCookieVal)
        {
            *pCookieVal = s_gsCookie;
            *ppCookieVal = nullptr;
        }
        else
        {
            *ppCookieVal = const_cast<GSCookie *>(&s_gsCookie);
        }
    }

    /**********************************************************************************/
    //
    // ICorModuleInfo
    //
    /**********************************************************************************/

    // Resolve metadata token into runtime method handles.
    void resolveToken(/* IN, OUT */ CORINFO_RESOLVED_TOKEN * pResolvedToken) override {
        auto* mod = reinterpret_cast<BaseModule*>(pResolvedToken->tokenScope);
        BaseMethod* method = mod->ResolveMethod(pResolvedToken->token);
        pResolvedToken->hMethod = (CORINFO_METHOD_HANDLE)method;
        pResolvedToken->hClass = PYOBJECT_PTR_TYPE; // Internal reference for Pyobject ptr
    }

    // Signature information about the call sig
    void findSig(
        CORINFO_MODULE_HANDLE       module,     /* IN */
        unsigned                    sigTOK,     /* IN */
        CORINFO_CONTEXT_HANDLE      context,    /* IN */
        CORINFO_SIG_INFO           *sig         /* OUT */
        ) override {
        auto mod = reinterpret_cast<BaseModule*>(module);
        auto method = mod->ResolveMethod(sigTOK);
        method->findSig(sig);
    }

    // for Varargs, the signature at the call site may differ from
    // the signature at the definition.  Thus we need a way of
    // fetching the call site information
    void findCallSiteSig(
        CORINFO_MODULE_HANDLE       module,     /* IN */
        unsigned                    methTOK,    /* IN */
        CORINFO_CONTEXT_HANDLE      context,    /* IN */
        CORINFO_SIG_INFO           *sig         /* OUT */
        ) override {
        WARN("Find call site sig not implemented \r\n");
    }

    CORINFO_CLASS_HANDLE getTokenTypeAsHandle(
        CORINFO_RESOLVED_TOKEN *    pResolvedToken /* IN  */) override {
        return nullptr;
    }

    // Returns true if the module does not require verification
    //
    // If fQuickCheckOnlyWithoutCommit=true, the function only checks that the
    // module does not currently require verification in the current AppDomain.
    // This decision could change in the future, and so should not be cached.
    // If it is cached, it should only be used as a hint.
    // This is only used by ngen for calculating certain hints.
    //

    // Checks if the given metadata token is valid
    bool isValidToken(
        CORINFO_MODULE_HANDLE       module,     /* IN  */
        unsigned                    metaTOK     /* IN  */
        ) override {
        WARN("isValidToken not implemented\r\n"); 
        return true;
    }

    // Checks if the given metadata token is valid StringRef
    bool isValidStringRef(
        CORINFO_MODULE_HANDLE       module,     /* IN  */
        unsigned                    metaTOK     /* IN  */
        ) override {
        WARN("isValidStringRef not implemented\r\n");
        return true;
    }

    /**********************************************************************************/
    //
    // ICorClassInfo
    //
    /**********************************************************************************/

    // If the value class 'cls' is isomorphic to a primitive type it will
    // return that type, otherwise it will return CORINFO_TYPE_VALUECLASS
    CorInfoType asCorInfoType(
        CORINFO_CLASS_HANDLE    cls
        ) override {
        if (cls == PYOBJECT_PTR_TYPE){
            return CORINFO_TYPE_PTR;
        }
        WARN("unimplemented asCorInfoType\r\n");
        return CORINFO_TYPE_UNDEF;
    }

    // for completeness
    const char* getClassName(
        CORINFO_CLASS_HANDLE    cls
        ) override {
        if (cls == PYOBJECT_PTR_TYPE)
            return "PyObject";
        return "classname";
    }

    // Append a (possibly truncated) representation of the type cls to the preallocated buffer ppBuf of length pnBufLen
    // If fNamespace=true, include the namespace/enclosing classes
    // If fFullInst=true (regardless of fNamespace and fAssembly), include namespace and assembly for any type parameters
    // If fAssembly=true, suffix with a comma and the full assembly qualification
    // return size of representation
    int appendClassName(
        __deref_inout_ecount(*pnBufLen) char16_t** ppBuf,
        int* pnBufLen,
        CORINFO_CLASS_HANDLE    cls,
        bool fNamespace,
        bool fFullInst,
        bool fAssembly
        ) override {
        WARN("appendClassName not implemented\r\n"); return 0;
    }

    // Quick check whether the type is a value class. Returns the same value as getClassAttribs(cls) & CORINFO_FLG_VALUECLASS, except faster.
    bool isValueClass(CORINFO_CLASS_HANDLE cls) override {
        if (cls == PYOBJECT_PTR_TYPE)
            return false;
        return false;
    }

    // return flags (defined above, CORINFO_FLG_PUBLIC ...)
    uint32_t getClassAttribs(
        CORINFO_CLASS_HANDLE    cls
        ) override {
        if (cls == PYOBJECT_PTR_TYPE)
            return CORINFO_FLG_NATIVE;
        return CORINFO_FLG_VALUECLASS;
    }

    // Returns "true" iff "cls" is a struct type such that return buffers used for returning a value
    // of this type must be stack-allocated.  This will generally be true only if the struct 
    // contains GC pointers, and does not exceed some size limit.  Maintaining this as an invariant allows
    // an optimization: the JIT may assume that return buffer pointers for return types for which this predicate
    // returns true are always stack allocated, and thus, that stores to the GC-pointer fields of such return
    // buffers do not require GC write barriers.
    bool isStructRequiringStackAllocRetBuf(CORINFO_CLASS_HANDLE cls) override {
        WARN("isStructRequiringStackAllocRetBuf\r\n");
        return false;
    }

    CORINFO_MODULE_HANDLE getClassModule(
        CORINFO_CLASS_HANDLE    cls
        ) override {
        WARN("getClassModule  not implemented\r\n");
        return nullptr;
    }

    // Returns the assembly that contains the module "mod".
    CORINFO_ASSEMBLY_HANDLE getModuleAssembly(
        CORINFO_MODULE_HANDLE   mod
        ) override {
        WARN("getModuleAssembly  not implemented\r\n");
        return nullptr;
    }

    // Returns the name of the assembly "assem".
    const char* getAssemblyName(
        CORINFO_ASSEMBLY_HANDLE assem
        ) override {
        WARN("getAssemblyName  not implemented\r\n");
        return "assem";
    }

    // Allocate and delete process-lifetime objects.  Should only be
    // referred to from static fields, lest a leak occur.
    // Note that "LongLifetimeFree" does not execute destructors, if "obj"
    // is an array of a struct type with a destructor.
    void* LongLifetimeMalloc(size_t sz) override {
        WARN("LongLifetimeMalloc\r\n");
        return nullptr;
    }

    void LongLifetimeFree(void* obj) override {
        WARN("LongLifetimeFree\r\n");
    }

    size_t getClassModuleIdForStatics(
        CORINFO_CLASS_HANDLE    cls,
        CORINFO_MODULE_HANDLE *pModule,
        void **ppIndirection
        ) override {
        WARN("getClassModuleIdForStatics  not implemented\r\n");
        return 0;
    }

    // return the number of bytes needed by an instance of the class
    unsigned getClassSize(
        CORINFO_CLASS_HANDLE        cls
        ) override {
        WARN("getClassSize  not implemented\r\n");
        return 0;
    }

    unsigned getClassAlignmentRequirement(
        CORINFO_CLASS_HANDLE        cls,
        bool                        fDoubleAlignHint
        ) override {
        WARN("getClassAlignmentRequirement\r\n");
        return 0;
    }

    // This is only called for Value classes.  It returns a boolean array
    // in representing of 'cls' from a GC perspective.  The class is
    // assumed to be an array of machine words
    // (of length // getClassSize(cls) / sizeof(void*)),
    // 'gcPtrs' is a poitner to an array of uint8_ts of this length.
    // getClassGClayout fills in this array so that gcPtrs[i] is set
    // to one of the CorInfoGCType values which is the GC type of
    // the i-th machine word of an object of type 'cls'
    // returns the number of GC pointers in the array
    unsigned getClassGClayout(
        CORINFO_CLASS_HANDLE        cls,        /* IN */
        uint8_t                       *gcPtrs      /* OUT */
        ) override {
        WARN("getClassGClayout\r\n");
        return 0;
    }

    // returns the number of instance fields in a class
    unsigned getClassNumInstanceFields(
        CORINFO_CLASS_HANDLE        cls        /* IN */
        ) override {
        WARN("getClassNumInstanceFields\r\n");
        return 0;
    }

    CORINFO_FIELD_HANDLE getFieldInClass(
        CORINFO_CLASS_HANDLE clsHnd,
        int32_t num
        ) override {
        WARN("getFieldInClass\r\n");
        return nullptr;
    }

    bool checkMethodModifier(
        CORINFO_METHOD_HANDLE hMethod,
        const char * modifier,
        bool fOptional
        ) override {
        WARN("checkMethodModifier\r\n");
        return false;
    }

    // returns the newArr (1-Dim array) helper optimized for "arrayCls."
    CorInfoHelpFunc getNewArrHelper(
        CORINFO_CLASS_HANDLE        arrayCls
        ) override {
        if (arrayCls == PYOBJECT_PTR_TYPE){
            return CORINFO_HELP_NEWARR_1_VC;
        }
        WARN("getNewArrHelper\r\n");
        return CORINFO_HELP_UNDEF;
    }

    // returns the optimized "IsInstanceOf" or "ChkCast" helper
    CorInfoHelpFunc getCastingHelper(
        CORINFO_RESOLVED_TOKEN * pResolvedToken,
        bool fThrowing
        ) override {
        WARN("getCastingHelper\r\n");
        return CORINFO_HELP_UNDEF;
    }

    // returns helper to trigger static constructor
    CorInfoHelpFunc getSharedCCtorHelper(
        CORINFO_CLASS_HANDLE clsHnd
        ) override {
        WARN("getSharedCCtorHelper\r\n");
        return CORINFO_HELP_UNDEF;
    }

    // This is not pretty.  Boxing nullable<T> actually returns
    // a boxed<T> not a boxed Nullable<T>.  This call allows the verifier
    // to call back to the EE on the 'box' instruction and get the transformed
    // type to use for verification.
    CORINFO_CLASS_HANDLE  getTypeForBox(
        CORINFO_CLASS_HANDLE        cls
        ) override {
        WARN("getTypeForBox  not implemented\r\n");
        return nullptr;
    }

    // returns the correct box helper for a particular class.  Note
    // that if this returns CORINFO_HELP_BOX, the JIT can assume 
    // 'standard' boxing (allocate object and copy), and optimize
    CorInfoHelpFunc getBoxHelper(
        CORINFO_CLASS_HANDLE        cls
        ) override {
        WARN("getBoxHelper\r\n");
        return CORINFO_HELP_BOX;
    }

    // returns the unbox helper.  If 'helperCopies' points to a true 
    // value it means the JIT is requesting a helper that unboxes the
    // value into a particular location and thus has the signature
    //     void unboxHelper(void* dest, CORINFO_CLASS_HANDLE cls, Object* obj)
    // Otherwise (it is null or points at a false value) it is requesting 
    // a helper that returns a poitner to the unboxed data 
    //     void* unboxHelper(CORINFO_CLASS_HANDLE cls, Object* obj)
    // The EE has the option of NOT returning the copy style helper
    // (But must be able to always honor the non-copy style helper)
    // The EE set 'helperCopies' on return to indicate what kind of
    // helper has been created.  

    CorInfoHelpFunc getUnBoxHelper(
        CORINFO_CLASS_HANDLE        cls
        ) override {
        WARN("getUnBoxHelper\r\n");
        return CORINFO_HELP_UNBOX;
    }

    const char* getHelperName(
        CorInfoHelpFunc
        ) override {
        return "AnyJITHelper";
    }

    // This used to be called "loadClass".  This records the fact
    // that the class must be loaded (including restored if necessary) before we execute the
    // code that we are currently generating.  When jitting code
    // the function loads the class immediately.  When zapping code
    // the zapper will if necessary use the call to record the fact that we have
    // to do a fixup/restore before running the method currently being generated.
    //
    // This is typically used to ensure value types are loaded before zapped
    // code that manipulates them is executed, so that the GC can access information
    // about those value types.
    void classMustBeLoadedBeforeCodeIsRun(
        CORINFO_CLASS_HANDLE        cls
        ) override {
        // Do nothing. We don't load/compile classes. 
    }

    // returns the class handle for the special builtin classes
    CORINFO_CLASS_HANDLE getBuiltinClass(
        CorInfoClassId              classId
        ) override {
        WARN("getBuiltinClass\r\n");
        return nullptr;
    }

    // "System.Int32" ==> CORINFO_TYPE_INT..
    CorInfoType getTypeForPrimitiveValueClass(
        CORINFO_CLASS_HANDLE        cls
        ) override {
        if (cls == PYOBJECT_PTR_TYPE)
            return CORINFO_TYPE_NATIVEINT;
        WARN("getTypeForPrimitiveValueClass\r\n");
        return CORINFO_TYPE_UNDEF;
    }

    // true if child is a subtype of parent
    // if parent is an interface, then does child implement / extend parent
    bool canCast(
        CORINFO_CLASS_HANDLE        child,  // subtype (extends parent)
        CORINFO_CLASS_HANDLE        parent  // base type
        ) override {
        WARN("canCast\r\n");
        return true;
    }

    // true if cls1 and cls2 are considered equivalent types.
    bool areTypesEquivalent(
        CORINFO_CLASS_HANDLE        cls1,
        CORINFO_CLASS_HANDLE        cls2
        ) override {
        WARN("areTypesEquivalent\r\n");
        return false;
    }

    // returns is the intersection of cls1 and cls2.
    CORINFO_CLASS_HANDLE mergeClasses(
        CORINFO_CLASS_HANDLE        cls1,
        CORINFO_CLASS_HANDLE        cls2
        ) override {
        WARN("mergeClasses  not implemented\r\n");
        return nullptr;
    }

    // Given a class handle, returns the Parent type.
    // For COMObjectType, it returns Class Handle of System.Object.
    // Returns 0 if System.Object is passed in.
    CORINFO_CLASS_HANDLE getParentType(
        CORINFO_CLASS_HANDLE        cls
        ) override {
        WARN("getParentType  not implemented\r\n");
        return nullptr;
    }

    // Returns the CorInfoType of the "child type". If the child type is
    // not a primitive type, *clsRet will be set.
    // Given an Array of Type Foo, returns Foo.
    // Given BYREF Foo, returns Foo
    CorInfoType getChildType(
        CORINFO_CLASS_HANDLE       clsHnd,
        CORINFO_CLASS_HANDLE       *clsRet
        ) override {
        WARN("getChildType  not implemented\r\n");
        return CORINFO_TYPE_UNDEF;
    }

    // Check constraints on type arguments of this class and parent classes
    bool satisfiesClassConstraints(
        CORINFO_CLASS_HANDLE cls
        ) override {
        WARN("satisfiesClassConstraints\r\n");
        return true;
    }

    // Check if this is a single dimensional array type
    bool isSDArray(
        CORINFO_CLASS_HANDLE        cls
        ) override {
        WARN("isSDArray\r\n");
        return true;
    }

    // Get the numbmer of dimensions in an array 
    unsigned getArrayRank(
        CORINFO_CLASS_HANDLE        cls
        ) override {
        WARN("getArrayRank\r\n");
        return 0;
    }

    // Get static field data for an array
    void * getArrayInitializationData(
        CORINFO_FIELD_HANDLE        field,
        uint32_t                       size
        ) override {
        WARN("getArrayInitializationData\r\n");
        return nullptr;
    }

    // Check Visibility rules.
    CorInfoIsAccessAllowedResult canAccessClass(
        CORINFO_RESOLVED_TOKEN * pResolvedToken,
        CORINFO_METHOD_HANDLE   callerHandle,
        CORINFO_HELPER_DESC    *pAccessHelper /* If canAccessMethod returns something other
                                              than ALLOWED, then this is filled in. */
        ) override {
        return CORINFO_ACCESS_ALLOWED;
    }

    /**********************************************************************************/
    //
    // ICorFieldInfo
    //
    /**********************************************************************************/

    // this function is for debugging only.  It returns the field name
    // and if 'moduleName' is non-null, it sets it to something that will
    // says which method (a class name, or a module name)
    const char* getFieldName(
        CORINFO_FIELD_HANDLE        ftn,        /* IN */
        const char                **moduleName  /* OUT */
        ) override {
        WARN("getFieldName  not implemented\r\n");
        return "field";
    }

    // return class it belongs to
    CORINFO_CLASS_HANDLE getFieldClass(
        CORINFO_FIELD_HANDLE    field
        ) override {
        WARN("getFieldClass not implemented\r\n");
        return nullptr;
    }

    // Return the field's type, if it is CORINFO_TYPE_VALUECLASS 'structType' is set
    // the field's value class (if 'structType' == 0, then don't bother
    // the structure info).
    //
    // 'memberParent' is typically only set when verifying.  It should be the
    // result of calling getMemberParent.
    CorInfoType getFieldType(
        CORINFO_FIELD_HANDLE    field,
        CORINFO_CLASS_HANDLE   *structType,
        CORINFO_CLASS_HANDLE    memberParent /* IN */
        ) override {
        WARN("getFieldType\r\n");
        return CORINFO_TYPE_UNDEF;
    }

    // return the data member's instance offset
    unsigned getFieldOffset(
        CORINFO_FIELD_HANDLE    field
        ) override {
        WARN("getFieldOffset\r\n");
        return 0;
    }

    void getFieldInfo(CORINFO_RESOLVED_TOKEN * pResolvedToken,
        CORINFO_METHOD_HANDLE  callerHandle,
        CORINFO_ACCESS_FLAGS   flags,
        CORINFO_FIELD_INFO    *pResult
        ) override {
        WARN("getFieldInfo not implemented\r\n");
    }

    // Returns true iff "fldHnd" represents a static field.
    bool isFieldStatic(CORINFO_FIELD_HANDLE fldHnd) override {
        WARN("isFieldStatic not implemented\r\n");
        return false;
    }

    /*********************************************************************************/
    //
    // ICorDebugInfo
    //
    /*********************************************************************************/

    // Query the EE to find out where interesting break points
    // in the code are.  The native compiler will ensure that these places
    // have a corresponding break point in native code.
    //
    // Note that unless CORJIT_FLG_DEBUG_CODE is specified, this function will
    // be used only as a hint and the native compiler should not change its
    // code generation.
    void getBoundaries(
        CORINFO_METHOD_HANDLE   ftn,                // [IN] method of interest
        unsigned int           *cILOffsets,         // [OUT] size of pILOffsets
        uint32_t                 **pILOffsets,         // [OUT] IL offsets of interest
        ICorDebugInfo::BoundaryTypes *implicitBoundaries // [OUT] tell jit, all boundaries of this type
        ) override {
        BaseMethod* meth = reinterpret_cast<BaseMethod *>(ftn);
        *cILOffsets = meth->getSequencePointCount();
        *pILOffsets = meth->getSequencePointOffsets();
        *implicitBoundaries = ICorDebugInfo::BoundaryTypes::DEFAULT_BOUNDARIES;
    }

    // Report back the mapping from IL to native code,
    // this map should include all boundaries that 'getBoundaries'
    // reported as interesting to the debugger.

    // Note that debugger (and profiler) is assuming that all of the
    // offsets form a contiguous block of memory, and that the
    // OffsetMapping is sorted in order of increasing native offset.
    void setBoundaries(
        CORINFO_METHOD_HANDLE   ftn,            // [IN] method of interest
        uint32_t                 cMap,           // [IN] size of pMap
        ICorDebugInfo::OffsetMapping *pMap      // [IN] map including all points of interest.
        //      jit allocated with allocateArray, EE frees
        ) override {
        BaseMethod* meth = reinterpret_cast<BaseMethod*>(ftn);
        for (size_t i = 0; i< cMap; i++){
            if (pMap[i].source == ICorDebugInfo::SOURCE_TYPE_INVALID) {
                meth->recordSequencePointOffsetPosition(pMap[i].ilOffset, pMap[i].nativeOffset);
            }
        }
        m_sequencePoints = meth->getSequencePoints();
    }

    // Query the EE to find out the scope of local varables.
    // normally the JIT would trash variables after last use, but
    // under debugging, the JIT needs to keep them live over their
    // entire scope so that they can be inspected.
    //
    // Note that unless CORJIT_FLG_DEBUG_CODE is specified, this function will
    // be used only as a hint and the native compiler should not change its
    // code generation.
    void getVars(
        CORINFO_METHOD_HANDLE           ftn,            // [IN]  method of interest
        uint32_t                        *cVars,          // [OUT] size of 'vars'
        ICorDebugInfo::ILVarInfo       **vars,          // [OUT] scopes of variables of interest
        //       jit MUST free with freeArray!
        bool                           *extendOthers    // [OUT] it true, then assume the scope
        //       of unmentioned vars is entire method
        ) override {
        *cVars = 0;
        *vars = {}; // Explore how/where these could be used?
        *extendOthers = false;
    }

    // Report back to the EE the location of every variable.
    // note that the JIT might split lifetimes into different
    // locations etc.

    void setVars(
        CORINFO_METHOD_HANDLE           ftn,            // [IN] method of interest
        uint32_t                         cVars,          // [IN] size of 'vars'
        ICorDebugInfo::NativeVarInfo   *vars            // [IN] map telling where local vars are stored at what points
        //      jit allocated with allocateArray, EE frees
        ) override {
        if (cVars != 0) {
            WARN("setVars not implemented\r\n");
        }
    }


    /*********************************************************************************/
    //
    // ICorArgInfo
    //
    /*********************************************************************************/

    // advance the pointer to the argument list.
    // a ptr of 0, is special and always means the first argument
    CORINFO_ARG_LIST_HANDLE getArgNext(
        CORINFO_ARG_LIST_HANDLE     args            /* IN */
        ) override {
        return reinterpret_cast<CORINFO_ARG_LIST_HANDLE>((reinterpret_cast<Parameter*>(args) + 1));
    }

    // If the Arg is a CORINFO_TYPE_CLASS fetch the class handle associated with it
    CORINFO_CLASS_HANDLE getArgClass(
        CORINFO_SIG_INFO*           sig,            /* IN */
        CORINFO_ARG_LIST_HANDLE     args            /* IN */
        ) override {
        // Do nothing. We don't load/compile classes. 
        return nullptr;
    }

    /*****************************************************************************
    * ICorErrorInfo contains methods to deal with SEH exceptions being thrown
    * from the corinfo interface.  These methods may be called when an exception
    * with code EXCEPTION_COMPLUS is caught.
    *****************************************************************************/

    // Returns the HRESULT of the current exception
    JITINTERFACE_HRESULT GetErrorHRESULT(
    struct _EXCEPTION_POINTERS *pExceptionPointers
        ) override {
        WARN("GetErrorHRESULT\r\n");
        return E_FAIL;
    }

    // Fetches the message of the current exception
    // Returns the size of the message (including terminating null). This can be
    // greater than bufferLength if the buffer is insufficient.
    uint32_t GetErrorMessage(
        __inout_ecount(bufferLength) char16_t * buffer,
        uint32_t bufferLength
        ) override {
        WARN("GetErrorMessage\r\n");
        return 0;
    }

    // returns EXCEPTION_EXECUTE_HANDLER if it is OK for the compile to handle the
    //                        exception, abort some work (like the inlining) and continue compilation
    // returns EXCEPTION_CONTINUE_SEARCH if exception must always be handled by the EE
    //                    things like ThreadStoppedException ...
    // returns EXCEPTION_CONTINUE_EXECUTION if exception is fixed up by the EE

    int FilterException(
    struct _EXCEPTION_POINTERS *pExceptionPointers
        ) override {
        WARN("FilterException\r\n"); return 0;
    }

    // Cleans up internal EE tracking when an exception is caught.
    void HandleException(
    struct _EXCEPTION_POINTERS *pExceptionPointers
        ) override {
        WARN("HandleException\r\n");
    }

    void ThrowExceptionForJitResult(
        HRESULT result) override {
        WARN("ThrowExceptionForJitResult\r\n");
    }

    //Throws an exception defined by the given throw helper.
    void ThrowExceptionForHelper(
        const CORINFO_HELPER_DESC * throwHelper) override {
        WARN("ThrowExceptionForHelper\r\n");
    }

    /*****************************************************************************
    * ICorStaticInfo contains EE interface methods which return values that are
    * constant from invocation to invocation.  Thus they may be embedded in
    * persisted information like statically generated code. (This is of course
    * assuming that all code versions are identical each time.)
    *****************************************************************************/

    // Return details about EE internal data structures
    void getEEInfo(
        CORINFO_EE_INFO            *pEEInfoOut
        ) override {
        memset(pEEInfoOut, 0, sizeof(CORINFO_EE_INFO));
        pEEInfoOut->inlinedCallFrameInfo.size = 4;
#ifdef WINDOWS
        pEEInfoOut->osPageSize = systemInfo.dwPageSize; // Set to the windows default
        pEEInfoOut->osType = CORINFO_WINNT;
#else
        pEEInfoOut->osPageSize = getpagesize();
        pEEInfoOut->osType = CORINFO_UNIX;
#endif
    }

    // Returns name of the JIT timer log
    const char16_t * getJitTimeLogFilename() override {
#ifdef DEBUG
        return u"pyjion_timings.log";
#else
        return nullptr;
#endif
    }

    /*********************************************************************************/
    //
    // Diagnostic methods
    //
    /*********************************************************************************/

    // this function is for debugging only. Returns method token.
    // Returns mdMethodDefNil for dynamic methods.
    mdMethodDef getMethodDefFromMethod(
        CORINFO_METHOD_HANDLE hMethod
        ) override {
        WARN("getMethodDefFromMethod\r\n");
        return 0;
    }

    // this function is for debugging only.  It returns the method name
    // and if 'moduleName' is non-null, it sets it to something that will
    // says which method (a class name, or a module name)
    const char* getMethodName(
        CORINFO_METHOD_HANDLE       ftn,        /* IN */
        const char                **moduleName  /* OUT */
        ) override {
        *moduleName = PyUnicode_AsUTF8(m_code->co_filename);
        return PyUnicode_AsUTF8(m_code->co_name);
    }

    // this function is for debugging only.  It returns a value that
    // is will always be the same for a given method.  It is used
    // to implement the 'jitRange' functionality
    unsigned getMethodHash(
        CORINFO_METHOD_HANDLE       ftn         /* IN */
        ) override {
        return 0;
    }

    // this function is for debugging only.
    size_t findNameOfToken(
        CORINFO_MODULE_HANDLE       module,     /* IN  */
        mdToken                     metaTOK,     /* IN  */
        __out_ecount(FQNameCapacity) char * szFQName, /* OUT */
        size_t FQNameCapacity  /* IN */
        ) override {
        WARN("findNameOfToken\r\n");
        return 0;

    }

	void getAddressOfPInvokeTarget(CORINFO_METHOD_HANDLE method, CORINFO_CONST_LOOKUP * pLookup) override
	{
        WARN("getAddressOfPInvokeTarget\r\n");
	}

	uint32_t getJitFlags(CORJIT_FLAGS * flags, uint32_t sizeInBytes) override
	{
		flags->Add(flags->CORJIT_FLAG_SKIP_VERIFICATION);
        if (m_compileDebug) {
            flags->Add(flags->CORJIT_FLAG_DEBUG_INFO);
            flags->Add(flags->CORJIT_FLAG_DEBUG_CODE);
            flags->Add(flags->CORJIT_FLAG_NO_INLINING);
            flags->Add(flags->CORJIT_FLAG_MIN_OPT);
        } else {
            flags->Add(flags->CORJIT_FLAG_SPEED_OPT);
        }

#ifdef DOTNET_PGO
        flags->Add(flags->CORJIT_FLAG_BBINSTR);
        flags->Add(flags->CORJIT_FLAG_BBOPT);
#endif

		return sizeof(CORJIT_FLAGS);
	}

    // This function returns the offset of the specified method in the
    // vtable of it's owning class or interface.
    void getMethodVTableOffset(CORINFO_METHOD_HANDLE method,                /* IN */
                               unsigned*             offsetOfIndirection,   /* OUT */
                               unsigned*             offsetAfterIndirection,/* OUT */
                               bool*                 isRelative             /* OUT */) override {
        *offsetOfIndirection = 0x1234;
        *offsetAfterIndirection = 0x2468;
        *isRelative = true;
    }

    CORINFO_METHOD_HANDLE getUnboxedEntry(CORINFO_METHOD_HANDLE ftn, bool *requiresInstMethodTableArg) override {
        WARN("getUnboxedEntry not defined\r\n");
        return nullptr;
    }

    CORINFO_CLASS_HANDLE getDefaultEqualityComparerClass(CORINFO_CLASS_HANDLE elemType) override {
        WARN("getDefaultEqualityComparerClass not defined\r\n");
        return nullptr;
    }

    void
    expandRawHandleIntrinsic(CORINFO_RESOLVED_TOKEN *pResolvedToken, CORINFO_GENERICHANDLE_RESULT *pResult) override {

    }

    void setPatchpointInfo(PatchpointInfo *patchpointInfo) override {

    }

    PatchpointInfo *getOSRInfo(unsigned int *ilOffset) override {
        WARN("getOSRInfo not defined\r\n");
        return nullptr;
    }

    bool tryResolveToken(CORINFO_RESOLVED_TOKEN *pResolvedToken) override {
        return false;
    }

    const char16_t * getStringLiteral(CORINFO_MODULE_HANDLE module, unsigned metaTOK, int *length) override {
        WARN("getStringLiteral not defined\r\n");
        return nullptr;
    }

    const char *getClassNameFromMetadata(CORINFO_CLASS_HANDLE cls, const char **namespaceName) override {
        WARN("getClassNameFromMetadata not defined\r\n");
        return nullptr;
    }

    CORINFO_CLASS_HANDLE getTypeInstantiationArgument(CORINFO_CLASS_HANDLE cls, unsigned int index) override {
        WARN("getTypeInstantiationArgument not defined\r\n");
        return nullptr;
    }

    CorInfoInlineTypeCheck canInlineTypeCheck(CORINFO_CLASS_HANDLE cls, CorInfoInlineTypeCheckSource source) override {
        return CORINFO_INLINE_TYPECHECK_USE_HELPER;
    }

    unsigned int getHeapClassSize(CORINFO_CLASS_HANDLE cls) override {
        return 0;
    }

    bool canAllocateOnStack(CORINFO_CLASS_HANDLE cls) override {
        return false;
    }

    CorInfoHelpFunc getNewHelper(CORINFO_RESOLVED_TOKEN *pResolvedToken, CORINFO_METHOD_HANDLE callerHandle,
                                 bool *pHasSideEffects) override {
        return CORINFO_HELP_GETREFANY;
    }

    bool getReadyToRunHelper(CORINFO_RESOLVED_TOKEN *pResolvedToken, CORINFO_LOOKUP_KIND *pGenericLookupKind,
                             CorInfoHelpFunc id, CORINFO_CONST_LOOKUP *pLookup) override {
        return false;
    }

    void getReadyToRunDelegateCtorHelper(CORINFO_RESOLVED_TOKEN *pTargetMethod, CORINFO_CLASS_HANDLE delegateType,
                                         CORINFO_LOOKUP *pLookup) override {

    }

    CorInfoInitClassResult
    initClass(CORINFO_FIELD_HANDLE field, CORINFO_METHOD_HANDLE method, CORINFO_CONTEXT_HANDLE context) override {
        return CORINFO_INITCLASS_INITIALIZED;
    }

    CorInfoType getTypeForPrimitiveNumericClass(CORINFO_CLASS_HANDLE cls) override {
        return CORINFO_TYPE_BYREF;
    }

    TypeCompareState compareTypesForCast(CORINFO_CLASS_HANDLE fromClass, CORINFO_CLASS_HANDLE toClass) override {
        return TypeCompareState::May;
    }

    TypeCompareState compareTypesForEquality(CORINFO_CLASS_HANDLE cls1, CORINFO_CLASS_HANDLE cls2) override {
        return TypeCompareState::May;
    }

    bool isMoreSpecificType(CORINFO_CLASS_HANDLE cls1, CORINFO_CLASS_HANDLE cls2) override {
        return false;
    }

    void *allocateArray(size_t cBytes) override {
        return PyMem_RawMalloc(cBytes);
    }
    // Allocate/Free will be called by the JIT for debug boundaries.
    void freeArray(
            void               *array
    ) override {
        PyMem_RawFree(array);
    }

    CorInfoHFAElemType getHFAType(CORINFO_CLASS_HANDLE hClass) override {
        return CORINFO_HFA_ELEM_DOUBLE;
    }

    bool runWithErrorTrap(void (*function)(void *), void *parameter) override {
        return false;
    }

    const char *getMethodNameFromMetadata(CORINFO_METHOD_HANDLE ftn, const char **className, const char **namespaceName,
                                          const char **enclosingClassName) override {
        WARN("getMethodNameFromMetadata not defined\r\n");
        return nullptr;
    }

    bool getSystemVAmd64PassStructInRegisterDescriptor(CORINFO_CLASS_HANDLE structHnd,
                                                       SYSTEMV_AMD64_CORINFO_STRUCT_REG_PASSING_DESCRIPTOR *structPassInRegDescPtr) override {
        return false;
    }

#ifdef INDIRECT_HELPERS
    void *getHelperFtn(CorInfoHelpFunc ftnNum, void **ppIndirection) override {
        *ppIndirection = nullptr;
        static void* helper = nullptr;
        switch (ftnNum){
            case CORINFO_HELP_USER_BREAKPOINT:
                helper = (void*)&breakpointFtn;
                break;
            case CORINFO_HELP_NEWARR_1_VC:
                helper = (void*)&newArrayHelperFtn;
                break;
            case CORINFO_HELP_ARRADDR_ST:
                helper = (void*)&stArrayHelperFtn;
                break;
            case CORINFO_HELP_STACK_PROBE:
                helper = (void*)&JIT_StackProbe;
                break;

            /* Helpers that throw exceptions */
            case CORINFO_HELP_OVERFLOW:
                helper = (void*)&raiseOverflowExceptionHelper;
                break;
            case CORINFO_HELP_FAIL_FAST:
                failFastExceptionHelper();
                break;
            case CORINFO_HELP_RNGCHKFAIL:
                helper = (void*)&rangeCheckExceptionHelper;
                break;
            case CORINFO_HELP_THROWDIVZERO:
                helper = (void*)&divisionByZeroExceptionHelper;
                break;
            case CORINFO_HELP_THROWNULLREF:
                helper = (void*)&nullReferenceExceptionHelper;
                break;
            case CORINFO_HELP_VERIFICATION:
                helper = (void*)&verificationExceptionHelper;
                break;
            case CORINFO_HELP_DBLREM:
                helper = (void*)&dblRemHelper;
                break;
            default:
                throw UnsupportedHelperException(ftnNum);
                break;
        }
        *ppIndirection = &helper;
        return nullptr;
    }
#else
    void *getHelperFtn(CorInfoHelpFunc ftnNum, void **ppIndirection) override {
        *ppIndirection = nullptr;
        switch (ftnNum){
            case CORINFO_HELP_USER_BREAKPOINT:
                return (void*)breakpointFtn;
            case CORINFO_HELP_NEWARR_1_VC:
                return (void*)newArrayHelperFtn;
            case CORINFO_HELP_ARRADDR_ST:
                return (void*)stArrayHelperFtn;
            case CORINFO_HELP_STACK_PROBE:
                return (void*)JIT_StackProbe;
            case CORINFO_HELP_OVERFLOW:
                return (void*)raiseOverflowExceptionHelper;
            case CORINFO_HELP_FAIL_FAST:
                failFastExceptionHelper();
                break;
            case CORINFO_HELP_RNGCHKFAIL:
                return (void*)rangeCheckExceptionHelper;
            case CORINFO_HELP_THROWDIVZERO:
                return (void*)divisionByZeroExceptionHelper;
            case CORINFO_HELP_THROWNULLREF:
                return (void*)nullReferenceExceptionHelper;
            case CORINFO_HELP_VERIFICATION:
                return (void*)verificationExceptionHelper;
            case CORINFO_HELP_DBLREM:
                return (void*)dblRemHelper;
            default:
                throw UnsupportedHelperException(ftnNum);
        }
        return nullptr;
    }
#endif
    void getLocationOfThisType(CORINFO_METHOD_HANDLE context, CORINFO_LOOKUP_KIND *pLookupKind) override {
    }

    CORINFO_CLASS_HANDLE getStaticFieldCurrentClass(CORINFO_FIELD_HANDLE field, bool *pIsSpeculative) override {
        WARN("getStaticFieldCurrentClass not defined\r\n");
        return nullptr;
    }

    uint32_t getFieldThreadLocalStoreID(CORINFO_FIELD_HANDLE field, void **ppIndirection) override {
        return 0;
    }

    void setOverride(ICorDynamicInfo *pOverride, CORINFO_METHOD_HANDLE currentMethod) override {

    }

    void addActiveDependency(CORINFO_MODULE_HANDLE moduleFrom, CORINFO_MODULE_HANDLE moduleTo) override {

    }

    CORINFO_METHOD_HANDLE
    GetDelegateCtor(CORINFO_METHOD_HANDLE methHnd, CORINFO_CLASS_HANDLE clsHnd, CORINFO_METHOD_HANDLE targetMethodHnd,
                    DelegateCtorArgs *pCtorData) override {
        WARN("GetDelegateCtor not defined\r\n");
        return nullptr;
    }

    void MethodCompileComplete(CORINFO_METHOD_HANDLE methHnd) override {

    }

    bool getTailCallHelpers(CORINFO_RESOLVED_TOKEN *callToken, CORINFO_SIG_INFO *sig,
                            CORINFO_GET_TAILCALL_HELPERS_FLAGS flags, CORINFO_TAILCALL_HELPERS *pResult) override {
        return false;
    }

    bool convertPInvokeCalliToCall(CORINFO_RESOLVED_TOKEN *pResolvedToken, bool fMustConvert) override {
        return false;
    }

    // Notify EE about intent to use or not to use instruction set in the method. Returns true if the instruction set is supported unconditionally.
    bool notifyInstructionSetUsage(CORINFO_InstructionSet instructionSet, bool supportEnabled) override {
        return true;
    }

    void reserveUnwindInfo(bool isFunclet, bool isColdCode, uint32_t unwindSize) override {

    }

    void allocUnwindInfo (
            uint8_t *              pHotCode,              /* IN */
            uint8_t *              pColdCode,             /* IN */
            uint32_t               startOffset,           /* IN */
            uint32_t               endOffset,             /* IN */
            uint32_t               unwindSize,            /* IN */
            uint8_t *              pUnwindBlock,          /* IN */
            CorJitFuncKind      funcKind               /* IN */
    ) override {
        // Only used in .NET 5 for FEATURE_EH_FUNCLETS.
        // No requirement to have an implementation in Pyjion.
    }

    void *allocGCInfo(size_t size) override {
        return PyMem_Malloc(size);
    }

    void setEHcount(unsigned int cEH) override {
        WARN("setEHcount not implemented \r\n");
    }

    void setEHinfo(unsigned int EHnumber, const CORINFO_EH_CLAUSE *clause) override {
        WARN("setEHinfo not implemented \r\n");
    }

    CorInfoTypeWithMod
    getArgType(CORINFO_SIG_INFO *sig, CORINFO_ARG_LIST_HANDLE args, CORINFO_CLASS_HANDLE *vcTypeRet) override {
        *vcTypeRet = nullptr;
        return (CorInfoTypeWithMod)(reinterpret_cast<Parameter*>(args))->m_type;
    }

    void recordCallSite(uint32_t instrOffset,
                        CORINFO_SIG_INFO *callSig,
                        CORINFO_METHOD_HANDLE methodHandle) override {
    }

    void assignIL(vector<uint8_t>il) {
        m_il = il;
    }

    void setNativeSize(uint32_t i) {
        m_nativeSize = i;
    }

    /* New .NET 6 methods */
    bool resolveVirtualMethod(CORINFO_DEVIRTUALIZATION_INFO * info) override {
        WARN("resolveVirtualMethod not implemented \r\n");
        return false;
    }

    CORINFO_CLASS_HANDLE getDefaultComparerClass(
            CORINFO_CLASS_HANDLE elemType
    ) override {
        WARN("getDefaultComparerClass not implemented \r\n");
        return nullptr;
    }


    CorInfoCallConvExtension getUnmanagedCallConv(
            CORINFO_METHOD_HANDLE       method,
            CORINFO_SIG_INFO*           callSiteSig,
            bool*                       pSuppressGCTransition /* OUT */
    ) override {
        return CorInfoCallConvExtension::C;
    }

    void embedGenericHandle(
            CORINFO_RESOLVED_TOKEN *        pResolvedToken,
            bool                            fEmbedParent, // true - embeds parent type handle of the field/method handle
            CORINFO_GENERICHANDLE_RESULT *  pResult) override {
        if (pResolvedToken->tokenType == CORINFO_TOKENKIND_Newarr) {
            // Emitted from ILGenerator::new_array()
            pResult->lookup.lookupKind.needsRuntimeLookup = false;
            pResult->lookup.constLookup.handle = pResult->compileTimeHandle;
            pResult->lookup.constLookup.accessType = IAT_VALUE;
        }
    }

    JITINTERFACE_HRESULT getPgoInstrumentationResults(
            CORINFO_METHOD_HANDLE      ftnHnd,
            PgoInstrumentationSchema **pSchema,                    // OUT: pointer to the schema table (array) which describes the instrumentation results
            // (pointer will not remain valid after jit completes).
            uint32_t *                 pCountSchemaItems,          // OUT: pointer to the count of schema items in `pSchema` array.
            uint8_t **                 pInstrumentationData        // OUT: `*pInstrumentationData` is set to the address of the instrumentation data
            // (pointer will not remain valid after jit completes).
    ) override {
        return E_NOTIMPL;
    }

    JITINTERFACE_HRESULT allocPgoInstrumentationBySchema(
            CORINFO_METHOD_HANDLE     ftnHnd,
            PgoInstrumentationSchema *pSchema,                     // IN OUT: pointer to the schema table (array) which describes the instrumentation results. `Offset` field
            // is filled in by VM; other fields are set and passed in by caller.
            uint32_t                  countSchemaItems,            // IN: count of schema items in `pSchema` array.
            uint8_t **                pInstrumentationData         // OUT: `*pInstrumentationData` is set to the address of the instrumentation data.
    ) override {
        return E_NOTIMPL;
    }

    // If a method's attributes have (getMethodAttribs) CORINFO_FLG_INTRINSIC set,
    // getIntrinsicID() returns the intrinsic ID.
    CorInfoIntrinsics getIntrinsicID(
            CORINFO_METHOD_HANDLE       method,
            bool * pMustExpand
    ) override {
        WARN("getIntrinsicID not implemented\r\n");
        return CORINFO_INTRINSIC_Object_GetType;
    }

    // Quick check whether the method is a jit intrinsic. Returns the same value as getMethodAttribs(ftn) & CORINFO_FLG_JIT_INTRINSIC, except faster.
    bool isJitIntrinsic(CORINFO_METHOD_HANDLE ftn) override {
        // method attribs are CORINFO_FLG_STATIC | CORINFO_FLG_NATIVE - so always false.
        return false;
    }
};

#endif
