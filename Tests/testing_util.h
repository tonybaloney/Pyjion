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

        auto tstate = PyThreadState_Get();
        // Don't DECREF as frames are recycled.
        auto frame = PyFrame_New(tstate, m_code.get(), globals.get(), PyObject_ptr(PyDict_New()).get());
        auto prev = _PyInterpreterState_GetEvalFrameFunc(PyInterpreterState_Main());
        _PyInterpreterState_SetEvalFrameFunc(PyInterpreterState_Main(), PyJit_EvalFrame);
        auto res = m_jittedcode->j_evalfunc(m_jittedcode.get(), frame, tstate, nullptr);
        _PyInterpreterState_SetEvalFrameFunc(PyInterpreterState_Main(), prev);

        size_t collected = PyGC_Collect();
        printf("Collected %zu values\n", collected);
        REQUIRE(!m_jittedcode->j_failed);
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
        if (!jit_compile(m_code.get())) {
            FAIL("failed to JIT code");
        }
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
};

#endif // !PYJION_TESTING_UTIL_H
