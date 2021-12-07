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

/**
 Test type inferencing.

 Many tests use a style of test ported over from an old test runner. Any new
 set of tests should write their own testing code that would be easier to work
 with.
*/
#include <catch2/catch.hpp>
#include <memory>
#include <codemodel.h>
#include <jitinfo.h>
#include <ilgen.h>
#include <pycomp.h>

extern BaseModule g_module;
extern ICorJitCompiler* g_jit;

typedef int32_t (*Returns_int32)();
typedef uint32_t (*Returns_uint32)();
typedef int64_t (*Returns_int64)();
typedef double (*Returns_double)();

TEST_CASE("Test numerics") {
    SECTION("test ld_i4 emitter") {
        int32_t value = GENERATE(1, -1, 0, 100, 127, -127, 128, -128, 129, -129, -100, 1000, 202, -102, 65555, 2147483647, -2147483647);
        auto test_module = new UserModule(g_module);
        auto gen = new ILGenerator(
                test_module,
                CORINFO_TYPE_INT,
                std::vector<Parameter>{});
        gen->ld_i4(value);
        gen->ret();
        auto* jitInfo = new CorJitInfo("test_module", "test_32_int", test_module, true);
        JITMethod method = gen->compile(jitInfo, g_jit, 100);
        REQUIRE(method.m_addr != nullptr);
        int32_t result = ((Returns_int32) method.getAddr())();
        CHECK(result == value);
    }

    SECTION("test ld_u4 emitter") {
        int32_t value = GENERATE(1, 0, 100, 1000, 202, 65555, 4294967295);
        auto test_module = new UserModule(g_module);
        auto gen = new ILGenerator(
                test_module,
                CORINFO_TYPE_INT,
                std::vector<Parameter>{});
        gen->ld_u4(value);
        gen->ret();
        auto* jitInfo = new CorJitInfo("test_module", "test_32_int", test_module, true);
        JITMethod method = gen->compile(jitInfo, g_jit, 100);
        REQUIRE(method.m_addr != nullptr);
        uint32_t result = ((Returns_uint32) method.getAddr())();
        CHECK(result == value);
    }

    SECTION("test ld_i8 emitter") {
        int64_t value = GENERATE(1, 0, 100, 1000, 202, 65555, 4294967295, 9223372036854775807);
        auto test_module = new UserModule(g_module);
        auto gen = new ILGenerator(
                test_module,
                CORINFO_TYPE_LONG,
                std::vector<Parameter>{});
        gen->ld_i8(value);
        gen->ret();
        auto* jitInfo = new CorJitInfo("test_module", "test_32_int", test_module, true);
        JITMethod method = gen->compile(jitInfo, g_jit, 100);
        REQUIRE(method.m_addr != nullptr);
        int64_t result = ((Returns_int64) method.getAddr())();
        CHECK(result == value);
    }

    SECTION("test ld_r8 emitter") {
        double value = GENERATE(1., 0., 100., 1000., 202., 65555., 4294967295., .2222);
        auto test_module = new UserModule(g_module);
        auto gen = new ILGenerator(
                test_module,
                CORINFO_TYPE_DOUBLE,
                std::vector<Parameter>{});
        gen->ld_r8(value);
        gen->ret();
        auto* jitInfo = new CorJitInfo("test_module", "test_32_int", test_module, true);
        JITMethod method = gen->compile(jitInfo, g_jit, 100);
        REQUIRE(method.m_addr != nullptr);
        double result = ((Returns_double) method.getAddr())();
        CHECK(result == value);
    }
}

TEST_CASE("Test locals") {
    SECTION("test ld_loc emitter") {
        int32_t value = GENERATE(1, -1, 0, 100, 127, -127, 128, -128, 129, -129, -100, 1000, 202, -102, 65555,
                                 2147483647, -2147483647);
        auto test_module = new UserModule(g_module);
        auto gen = new ILGenerator(
                test_module,
                CORINFO_TYPE_INT,
                std::vector<Parameter>{});
        gen->ld_i4(value);
        auto l = gen->define_local(Parameter(CORINFO_TYPE_INT));
        gen->st_loc(l);
        gen->ld_loc(l);
        gen->ret();
        auto* jitInfo = new CorJitInfo("test_module", "test_32_int", test_module, true);
        JITMethod method = gen->compile(jitInfo, g_jit, 100);
        REQUIRE(method.m_addr != nullptr);
        int32_t result = ((Returns_int32) method.getAddr())();
        CHECK(result == value);
    }
}
TEST_CASE("Test branch true of floats") {
    SECTION("test branch true emitter") {
        auto test_module = new UserModule(g_module);
        auto gen = new ILGenerator(
                test_module,
                CORINFO_TYPE_INT,
                std::vector<Parameter>{});
        Label isTrue = gen->define_label(), end = gen->define_label();
        gen->ld_r8(1.0);
        gen->branch(BranchTrue, isTrue);
        gen->ld_i4(2);
        gen->branch(BranchAlways, end);
        gen->mark_label(isTrue);
        gen->ld_i4(3);
        gen->mark_label(end);
        gen->ret();
        auto* jitInfo = new CorJitInfo("test_module", "test_32_int", test_module, true);
        JITMethod method = gen->compile(jitInfo, g_jit, 100);
        REQUIRE(method.m_addr != nullptr);
        int32_t result = ((Returns_int32) method.getAddr())();
        CHECK(result == 3);
    }
    SECTION("test branch false emitter") {
        auto test_module = new UserModule(g_module);
        auto gen = new ILGenerator(
                test_module,
                CORINFO_TYPE_INT,
                std::vector<Parameter>{});
        Label isTrue = gen->define_label(), end = gen->define_label();
        gen->ld_r8(1.0);
        gen->branch(BranchFalse, isTrue);
        gen->ld_i4(2);
        gen->branch(BranchAlways, end);
        gen->mark_label(isTrue);
        gen->ld_i4(3);
        gen->mark_label(end);
        gen->ret();
        auto* jitInfo = new CorJitInfo("test_module", "test_32_int", test_module, true);
        JITMethod method = gen->compile(jitInfo, g_jit, 100);
        REQUIRE(method.m_addr != nullptr);
        int32_t result = ((Returns_int32) method.getAddr())();
        CHECK(result == 2);
    }
    SECTION("test branch r8 equivalence emitter") {
        auto test_module = new UserModule(g_module);
        auto gen = new ILGenerator(
                test_module,
                CORINFO_TYPE_INT,
                std::vector<Parameter>{});
        Label isTrue = gen->define_label(), end = gen->define_label();
        gen->ld_r8(1.0);
        gen->ld_r8(1.0);
        gen->branch(BranchEqual, isTrue);
        gen->ld_i4(2);
        gen->branch(BranchAlways, end);
        gen->mark_label(isTrue);
        gen->ld_i4(3);
        gen->mark_label(end);
        gen->ret();
        auto* jitInfo = new CorJitInfo("test_module", "test_32_int", test_module, true);
        JITMethod method = gen->compile(jitInfo, g_jit, 100);
        REQUIRE(method.m_addr != nullptr);
        int32_t result = ((Returns_int32) method.getAddr())();
        CHECK(result == 3);
    }
}

TEST_CASE("Test call") {
    SECTION("test call method emitter") {
        auto test_module = new UserModule(g_module);
        auto gen = new ILGenerator(
                test_module,
                CORINFO_TYPE_DOUBLE,
                std::vector<Parameter>{});
        gen->ld_i8(10);
        gen->ld_i8(5);
        gen->emit_call(METHOD_INT_TRUE_DIVIDE);
        gen->ret();
        auto* jitInfo = new CorJitInfo("test_module", "test_call", test_module, true);
        JITMethod method = gen->compile(jitInfo, g_jit, 100);
        REQUIRE(method.m_addr != nullptr);
        double result = ((Returns_double) method.getAddr())();
        CHECK(result == 2.0);
        auto symbols = jitInfo->get_symbol_table();
        CHECK(!symbols.empty());
    }
}

TEST_CASE("Test intrinsics") {
    SECTION("test define call intrinsic flagged method") {
        auto test_module = new UserModule(g_module);
        auto gen = new ILGenerator(
                test_module,
                CORINFO_TYPE_DOUBLE,
                std::vector<Parameter>{});
        gen->ld_i8(10);
        gen->ld_i8(5);
        gen->emit_call(INTRINSIC_TEST);
        gen->ret();
        auto* jitInfo = new CorJitInfo("test_module", "test_call", test_module, true);
        JITMethod method = gen->compile(jitInfo, g_jit, 100);
        REQUIRE(method.m_addr != nullptr);
        double result = ((Returns_double) method.getAddr())();
        CHECK(result == 2.0);
        auto symbols = jitInfo->get_symbol_table();
        CHECK(!symbols.empty());
    }
}

TEST_CASE("Test valuetype"){
    SECTION("test simple valuetype") {
        auto test_module = new UserModule(g_module);
        auto gen = new ILGenerator(
                test_module,
                CORINFO_TYPE_DOUBLE,
                std::vector<Parameter>{});
        gen->define_local(Parameter(CORINFO_TYPE_VALUECLASS));
        gen->ld_r8(2.0);
        gen->ret();
        auto* jitInfo = new CorJitInfo("test_module", "test_call", test_module, true);
        JITMethod method = gen->compile(jitInfo, g_jit, 100);
        REQUIRE(method.m_addr != nullptr);
        double result = ((Returns_double) method.getAddr())();
        CHECK(result == 2.0);
        auto symbols = jitInfo->get_symbol_table();
        CHECK(!symbols.empty());
    }
}
TEST_CASE("Test binary operations") {
    SECTION("test << ") {
        int32_t value1 = GENERATE(1, 4, 64);
        int32_t value2 = GENERATE(1, 4, 64);
        auto test_module = new UserModule(g_module);
        auto gen = new ILGenerator(
                test_module,
                CORINFO_TYPE_INT,
                std::vector<Parameter>{});
        gen->ld_i4(value1);
        gen->ld_i4(value2);
        gen->lshift();
        gen->ret();
        auto* jitInfo = new CorJitInfo("test_module", "test_32_int", test_module, true);
        JITMethod method = gen->compile(jitInfo, g_jit, 100);
        REQUIRE(method.m_addr != nullptr);
        int32_t result = ((Returns_int32) method.getAddr())();
        CHECK(result == (value1 << value2));
    }
    SECTION("test >> ") {
        int32_t value1 = GENERATE(1, 4, 64);
        int32_t value2 = GENERATE(1, 4, 64);
        auto test_module = new UserModule(g_module);
        auto gen = new ILGenerator(
                test_module,
                CORINFO_TYPE_INT,
                std::vector<Parameter>{});
        gen->ld_i4(value1);
        gen->ld_i4(value2);
        gen->rshift();
        gen->ret();
        auto* jitInfo = new CorJitInfo("test_module", "test_32_int", test_module, true);
        JITMethod method = gen->compile(jitInfo, g_jit, 100);
        REQUIRE(method.m_addr != nullptr);
        int32_t result = ((Returns_int32) method.getAddr())();
        CHECK(result == (value1 >> value2));
    }
}