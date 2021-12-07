#pragma once

#ifndef PYJION_TESTING_UTIL_H
#define PYJION_TESTING_UTIL_H 1

#include <Python.h>
#include <absint.h>
#include <util.h>
#include <pyjit.h>

#include <utility>

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
        m_args = std::move(args);
    }
};

class TestCase {
public:
    const char* m_code;
    vector<TestInput> m_inputs;

    TestCase(const char* code, const char* expected) {
        m_code = code;
        m_inputs.emplace_back(expected);
    }

    TestCase(const char* code, const TestInput& input) {
        m_code = code;
        m_inputs.push_back(input);
    }

    TestCase(const char* code, vector<TestInput> inputs) {
        m_code = code;
        m_inputs = std::move(inputs);
    }
};

/* Verify the inferred type stored in the locals array before a specified bytecode executes. */
class VariableVerifier {
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
    VariableVerifier(size_t byteCodeIndex, size_t localIndex, AbstractValueKind kind, bool undefined = false);
    void verify(AbstractInterpreter& interpreter);
};

class AITestCase {
private:
public:
    const char* m_code;
    vector<VariableVerifier*> m_verifiers;

    AITestCase(const char* code, VariableVerifier* verifier) {
        m_code = code;
        m_verifiers.push_back(verifier);
    }

    AITestCase(const char* code, vector<VariableVerifier*> verifiers) {
        m_code = code;
        m_verifiers = std::move(verifiers);
    }

    AITestCase(const char* code, std::initializer_list<VariableVerifier*> list) {
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

class EmissionTest {
private:
    py_ptr<PyCodeObject> m_code;
    PyjionJittedCode* m_jittedcode;

    PyObject* run() {
        auto sysModule = PyObject_ptr(PyImport_ImportModule("sys"));
        auto globals = PyObject_ptr(PyDict_New());
        auto builtins = PyEval_GetBuiltins();
        PyDict_SetItemString(globals.get(), "__builtins__", builtins);
        PyDict_SetItemString(globals.get(), "sys", sysModule.get());
        auto profile = new PyjionCodeProfile();
        auto tstate = PyThreadState_Get();
        auto frame = PyFrame_New(tstate, m_code.get(), globals.get(), PyObject_ptr(PyDict_New()).get());
        auto prev = _PyInterpreterState_GetEvalFrameFunc(PyInterpreterState_Main());
        _PyInterpreterState_SetEvalFrameFunc(PyInterpreterState_Main(), PyJit_EvalFrame);
        auto res = PyJit_ExecuteAndCompileFrame(m_jittedcode, frame, tstate, profile);
        CHECK(frame->f_stackdepth != -1);
        CHECK(frame->f_lasti >= 0);
        CHECK(frame->f_lasti * 2 < PyBytes_GET_SIZE(m_code->co_code));
        _PyInterpreterState_SetEvalFrameFunc(PyInterpreterState_Main(), prev);
        Py_DECREF(frame);
        PyGC_Collect();
        REQUIRE(!m_jittedcode->j_failed);
        REQUIRE(m_jittedcode->j_genericAddr != nullptr);
        delete profile;
        return res;
    }

public:
    explicit EmissionTest(const char* code) {
        PyErr_Clear();
#ifdef DEBUG_VERBOSE
        printf("--- Executing Code ---\n%s \n-----------------\n", code);
#endif
        m_code.reset(CompileCode(code));
        if (m_code.get() == nullptr) {
            FAIL("failed to compile in JIT code");
        }
        auto jitted = PyJit_EnsureExtra((PyObject*) *m_code);
        m_jittedcode = jitted;
    }

    std::string returns() {
        auto res = PyObject_ptr(run());
        CHECK(res.get() != nullptr);
        if (res.get() == nullptr || PyErr_Occurred()) {
            PyErr_PrintEx(-1);
            FAIL("Error on Python execution");
        }

        auto repr = PyUnicode_AsUTF8(PyObject_Repr(res.get()));
#ifdef DEBUG_VERBOSE
        printf("Returned: %s \n", repr);
#endif
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
        if (res != nullptr) {
            FAIL(PyUnicode_AsUTF8(PyObject_Repr(res)));
            return nullptr;
        }
        auto excType = PyErr_Occurred();
        PyErr_Clear();
        return excType;
    }


    BYTE* il() {
        return m_jittedcode->j_il;
    }

    unsigned long native_len() {
        return m_jittedcode->j_nativeSize;
    }

    PyObject* native() {
        auto result_t = PyTuple_New(3);
        if (result_t == nullptr)
            return nullptr;

        auto res = PyByteArray_FromStringAndSize(reinterpret_cast<const char*>(m_jittedcode->j_addr), m_jittedcode->j_nativeSize);
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
    py_ptr<PyCodeObject> m_code;
    PyjionJittedCode* m_jittedcode;
    PyjionCodeProfile* profile;

    PyObject* run() {
        auto sysModule = PyObject_ptr(PyImport_ImportModule("sys"));
        auto globals = PyObject_ptr(PyDict_New());
        auto builtins = PyEval_GetBuiltins();
        PyDict_SetItemString(globals.get(), "__builtins__", builtins);
        PyDict_SetItemString(globals.get(), "sys", sysModule.get());

        auto tstate = PyThreadState_Get();
        auto frame = PyFrame_New(tstate, m_code.get(), globals.get(), PyObject_ptr(PyDict_New()).get());
        auto prev = _PyInterpreterState_GetEvalFrameFunc(PyInterpreterState_Main());
        _PyInterpreterState_SetEvalFrameFunc(PyInterpreterState_Main(), PyJit_EvalFrame);
        m_jittedcode->j_profile = profile;
        auto res = PyJit_EvalFrame(tstate, frame, 0);
        CHECK(frame->f_stackdepth != -1);
        _PyInterpreterState_SetEvalFrameFunc(PyInterpreterState_Main(), prev);
        Py_DECREF(frame);
        size_t collected = PyGC_Collect();
        REQUIRE(!m_jittedcode->j_failed);
        REQUIRE(m_jittedcode->j_genericAddr != nullptr);
        return res;
    }

public:
    explicit PgcProfilingTest(const char* code) {
        PyErr_Clear();
        profile = new PyjionCodeProfile();
        m_code.reset(CompileCode(code));
        if (m_code.get() == nullptr) {
            FAIL("failed to compile code");
        }
        auto jitted = PyJit_EnsureExtra((PyObject*) *m_code);
        m_jittedcode = jitted;
    }

    std::string returns() {
        auto res = PyObject_ptr(run());
        if (PyErr_Occurred()) {
            PyErr_PrintEx(-1);
            FAIL("Error on Python execution");
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

    PyObject* ret() {
        return run();
    }

    PyObject* raises() {
        auto res = run();
        REQUIRE(res == nullptr);
        auto excType = PyErr_Occurred();
        PyErr_Clear();
        return excType;
    }

    bool profileEquals(int position, int stackPosition, PyTypeObject* pyType) {
        return profile->getType(position, stackPosition) == pyType;
    }

    PgcStatus pgcStatus() {
        return m_jittedcode->j_pgcStatus;
    }
};

class InstructionGraphTest {
private:
    std::unique_ptr<AbstractInterpreter> m_absint;
    InstructionGraph* m_graph;

public:
    explicit InstructionGraphTest(const char* code, const char* name) {
        auto pyCode = CompileCode(code);
        m_absint = std::make_unique<AbstractInterpreter>(pyCode);
        auto builtins = PyEval_GetBuiltins();
        auto globals_dict = PyObject_ptr(PyDict_New());
        auto profile = new PyjionCodeProfile();
        auto success = m_absint->interpret(builtins, globals_dict.get(), profile, Uncompiled);
        delete profile;
        if (success != Success) {
            Py_DECREF(pyCode);
            FAIL("Failed to interpret code");
        }
        m_graph = m_absint->buildInstructionGraph(true);
        auto result = m_graph->makeGraph(name);
#ifdef DEBUG_VERBOSE
        printf("%s", PyUnicode_AsUTF8(result));
#endif
    }

    ~InstructionGraphTest() {
        delete m_graph;
    }

    size_t size() {
        return m_graph->size();
    }

    Instruction instruction(size_t n) {
        return m_graph->operator[](n);
    }

    void assertInstruction(size_t n, py_opcode opcode, py_oparg oparg, bool escaped) {
        auto i = instruction(n);
        CHECK(i.escape == escaped);
        CHECK(i.opcode == opcode);
        CHECK(i.index == n);
        CHECK(i.oparg == oparg);
    }

    size_t edgesIn(py_opindex idx) {
        auto edges = m_graph->getEdges(idx);
        return edges.size();
    }

    EscapeTransition edgeInIs(py_opindex idx, size_t position) {
        auto edges = m_graph->getEdges(idx);
        return edges[position].escaped;
    }

    size_t edgesOut(py_opindex idx) {
        auto edges = m_graph->getEdgesFrom(idx);
        return edges.size();
    }

    EscapeTransition edgeOutIs(py_opindex idx, size_t position) {
        auto edges = m_graph->getEdgesFrom(idx);
        return edges[position].escaped;
    }
};

#endif// !PYJION_TESTING_UTIL_H
