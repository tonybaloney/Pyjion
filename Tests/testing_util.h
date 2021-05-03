#pragma once

#ifndef PYJION_TESTING_UTIL_H
#define PYJION_TESTING_UTIL_H 1

#include <Python.h>
#include <absint.h>
#include <util.h>
#include <pyjit.h>

PyCodeObject* CompileCode(const char*);
PyCodeObject* CompileCode(const char* code, vector<const char*> locals, vector<const char*> globals);
PyObject* CompileFunction(const char* code);

class TestInput {
public:
    const char* m_expected;
    vector<PyObject*> m_args;

    explicit TestInput(const char* expected) {
        m_expected = expected;
    }

    TestInput(const char* expected, vector<PyObject*> args) {
        m_expected = expected;
        m_args = args;
    }
};

class TestCase {
public:
    const char* m_code;
    vector<TestInput> m_inputs;

    TestCase(const char *code, const char* expected) {
        m_code = code;
        m_inputs.push_back(TestInput(expected));

    }

    TestCase(const char *code, TestInput input) {
        m_code = code;
        m_inputs.push_back(input);
    }

    TestCase(const char *code, vector<TestInput> inputs) {
        m_code = code;
        m_inputs = inputs;
    }
};

class AIVerifier {
public:
    virtual void verify(AbstractInterpreter& interpreter) = 0;
};

class StackVerifier : public AIVerifier {
    size_t m_byteCodeIndex, m_stackIndex;
    AbstractValueKind m_kind;
public:
    StackVerifier(size_t byteCodeIndex, size_t stackIndex, AbstractValueKind kind);

    void verify(AbstractInterpreter& interpreter) override;
};

/* Verify the inferred type stored in the locals array before a specified bytecode executes. */
class VariableVerifier : public AIVerifier {
private:
    // The bytecode whose locals state we are checking *before* execution.
    size_t m_byteCodeIndex;
    // The locals index whose type we are checking.
    size_t m_localIndex;
    // The inferred type.
    AbstractValueKind m_kind;
    // Has the value been defined yet?
    bool m_undefined;
public:
    VariableVerifier(size_t byteCodeIndex, size_t localIndex, AbstractValueKind kind, bool undefined = false) ;

    void verify(AbstractInterpreter& interpreter) override;
};

class ReturnVerifier : public AIVerifier {
    AbstractValueKind m_kind;
public:
    explicit ReturnVerifier(AbstractValueKind kind);

    void verify(AbstractInterpreter& interpreter) override;
};

class BoxVerifier : public AIVerifier {
public:
    BoxVerifier(size_t byteCodeIndex, bool shouldBox);

    void verify(AbstractInterpreter& interpreter) override {};
};

class AITestCase {
private:
public:
    const char* m_code;
    vector<AIVerifier*> m_verifiers;

    AITestCase(const char *code, AIVerifier* verifier) {
        m_code = code;
        m_verifiers.push_back(verifier);
    }

    AITestCase(const char *code, vector<AIVerifier*> verifiers) {
        m_code = code;
        m_verifiers = std::move(verifiers);
    }

    AITestCase(const char *code, std::initializer_list<AIVerifier*> list) {
        m_code = code;
        m_verifiers = list;
    }

    ~AITestCase() {
        for (auto verifier : m_verifiers) {
            delete verifier;
        }
    }

    void verify(AbstractInterpreter& interpreter) {
        for (auto cur : m_verifiers) {
            cur->verify(interpreter);
        }
    }
};

void VerifyOldTest(AITestCase testCase);

PyObject* Incremented(PyObject*o);

class EmissionTest {
private:
    py_ptr<PyCodeObject> m_code;
    py_ptr<PyjionJittedCode> m_jittedcode;

    PyObject* run() {
        auto sysModule = PyObject_ptr(PyImport_ImportModule("sys"));
        auto globals = PyObject_ptr(PyDict_New());
        auto builtins = PyEval_GetBuiltins();
        PyDict_SetItemString(globals.get(), "__builtins__", builtins);
        PyDict_SetItemString(globals.get(), "sys", sysModule.get());
        auto profile = new PyjionCodeProfile();
        auto tstate = PyThreadState_Get();
        // Don't DECREF as frames are recycled.
        auto frame = PyFrame_New(tstate, m_code.get(), globals.get(), PyObject_ptr(PyDict_New()).get());
        auto prev = _PyInterpreterState_GetEvalFrameFunc(PyInterpreterState_Main());
        _PyInterpreterState_SetEvalFrameFunc(PyInterpreterState_Main(), PyJit_EvalFrame);
        auto res = PyJit_ExecuteAndCompileFrame(m_jittedcode.get(), frame, tstate, profile);
        _PyInterpreterState_SetEvalFrameFunc(PyInterpreterState_Main(), prev);

        size_t collected = PyGC_Collect();
        printf("Collected %zu values\n", collected);
        REQUIRE(!m_jittedcode->j_failed);
        delete profile;
        return res;
    }

public:
    explicit EmissionTest(const char *code) {
        PyErr_Clear();
        m_code.reset(CompileCode(code));
        if (m_code.get() == nullptr) {
            FAIL("failed to compile in JIT code");
        }
        auto jitted = PyJit_EnsureExtra((PyObject*)*m_code);
        m_jittedcode.reset(jitted);
    }

    std::string returns() {
        auto res = PyObject_ptr(run());
        REQUIRE(res.get() != nullptr);
        if (PyErr_Occurred()){
            PyErr_PrintEx(-1);
            FAIL("Error on Python execution");
            return nullptr;
        }

        auto repr = PyUnicode_AsUTF8(PyObject_Repr(res.get()));
        auto tstate = PyThreadState_GET();
        REQUIRE(tstate->curexc_value == nullptr);
        REQUIRE(tstate->curexc_traceback == nullptr);
        if (tstate->curexc_type != nullptr) {
            REQUIRE(tstate->curexc_type == Py_None);
        }

        return std::string(repr);
    }

    PyObject* raises() {
        auto res = run();
        REQUIRE(res == nullptr);
        auto excType = PyErr_Occurred();
        PyErr_Clear();
        return excType;
    }


    BYTE* il (){
        return m_jittedcode->j_il;
    }

    unsigned long native_len(){
        return m_jittedcode->j_nativeSize;
    }

    PyObject* native(){
        auto result_t = PyTuple_New(3);
        if (result_t == nullptr)
            return nullptr;

        auto res = PyByteArray_FromStringAndSize(reinterpret_cast<const char *>(m_jittedcode->j_addr), m_jittedcode->j_nativeSize);
        if (res == nullptr)
            return nullptr;

        PyTuple_SET_ITEM(result_t, 0, res);
        Py_INCREF(res);

        auto codeLen = PyLong_FromUnsignedLong(m_jittedcode->j_nativeSize);
        if (codeLen == nullptr)
            return nullptr;
        PyTuple_SET_ITEM(result_t, 1, codeLen);
        Py_INCREF(codeLen);

        auto codePosition = PyLong_FromUnsignedLongLong(reinterpret_cast<unsigned long long>(&m_jittedcode->j_addr));
        if (codePosition == nullptr)
            return nullptr;
        PyTuple_SET_ITEM(result_t, 2, codePosition);
        Py_INCREF(codePosition);
        return result_t;
    }
};


class PgcProfilingTest {
private:
    py_ptr <PyCodeObject> m_code;
    py_ptr <PyjionJittedCode> m_jittedcode;
    PyjionCodeProfile* profile;

    PyObject *run() {
        auto sysModule = PyObject_ptr(PyImport_ImportModule("sys"));
        auto globals = PyObject_ptr(PyDict_New());
        auto builtins = PyEval_GetBuiltins();
        PyDict_SetItemString(globals.get(), "__builtins__", builtins);
        PyDict_SetItemString(globals.get(), "sys", sysModule.get());

        // Don't DECREF as frames are recycled.
        auto tstate = PyThreadState_Get();
        auto frame = PyFrame_New(tstate, m_code.get(), globals.get(), PyObject_ptr(PyDict_New()).get());
        auto prev = _PyInterpreterState_GetEvalFrameFunc(PyInterpreterState_Main());
        _PyInterpreterState_SetEvalFrameFunc(PyInterpreterState_Main(), PyJit_EvalFrame);
        m_jittedcode->j_profile = profile;
        auto res = PyJit_EvalFrame(tstate, frame, 0);

        _PyInterpreterState_SetEvalFrameFunc(PyInterpreterState_Main(), prev);
        //Py_DECREF(frame);
        size_t collected = PyGC_Collect();
        printf("Collected %zu values\n", collected);
        REQUIRE(!m_jittedcode->j_failed);
        return res;
    }

public:
    explicit PgcProfilingTest(const char *code) {
        PyErr_Clear();
        profile = new PyjionCodeProfile();
        m_code.reset(CompileCode(code));
        if (m_code.get() == nullptr) {
            FAIL("failed to compile code");
        }
        auto jitted = PyJit_EnsureExtra((PyObject *) *m_code);
        m_jittedcode.reset(jitted);
    }

    ~PgcProfilingTest(){
        //delete profile;
    }

    std::string returns() {
        auto res = PyObject_ptr(run());
        if (PyErr_Occurred()) {
            PyErr_PrintEx(-1);
            FAIL("Error on Python execution");
            return nullptr;
        }
        REQUIRE(res.get() != nullptr);
        PyObject* v = res.get();
        auto repr = PyUnicode_AsUTF8(PyObject_Repr(v));
        auto tstate = PyThreadState_GET();
        REQUIRE(tstate->curexc_value == nullptr);
        REQUIRE(tstate->curexc_traceback == nullptr);
        if (tstate->curexc_type != nullptr) {
            REQUIRE(tstate->curexc_type == Py_None);
        }

        return std::string(repr);
    }

    PyObject* ret(){
        return run();
    }

    PyObject *raises() {
        auto res = run();
        REQUIRE(res == nullptr);
        auto excType = PyErr_Occurred();
        PyErr_Print();
        PyErr_Clear();
        return excType;
    }

    bool profileEquals(int position, int stackPosition, PyTypeObject* pyType) {
        return profile->getType(position, stackPosition) == pyType;
    }

    PgcStatus pgcStatus() {
        return m_jittedcode->j_pgc_status;
    }
};
#endif // !PYJION_TESTING_UTIL_H
