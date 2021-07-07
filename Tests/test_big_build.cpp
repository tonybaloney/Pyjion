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

#include <catch2/catch.hpp>
#include "testing_util.h"
#include <Python.h>

TEST_CASE("Test big const builds") {

    SECTION("test BUILD_CONST_KEY_MAP") {
        auto n_keys = GENERATE(100, 1000, 10000, 65535);
        string code;
        code.append("def f():\n");
        code.append(" d = {\n");
        for (size_t i = 0 ; i < n_keys; i++){
            char snip[30];
            snprintf(snip, 30, " 'x%zu': 'test_%zu',", i, i);
            code.append(snip);
        }
        code.append(" }\n");
        code.append(" return len(d)\n");
        auto t = EmissionTest(
            code.c_str()
        );
        CHECK(stoi(t.returns()) == n_keys);
    }

    SECTION("test BUILD_TUPLE") {
        auto n_keys = GENERATE(100, 1000, 10000, 65535);
        string code;
        code.append("def f():\n");
        code.append(" d = (\n");
        for (size_t i = 0 ; i < n_keys; i++){
            char snip[30];
            snprintf(snip, 30, " 'test_%zu',", i);
            code.append(snip);
        }
        code.append(" )\n");
        code.append(" return len(d)\n");
        auto t = EmissionTest(
                code.c_str()
        );
        CHECK(stoi(t.returns()) == n_keys);
    }

    SECTION("test BUILD_LIST") {
        auto n_keys = GENERATE(100, 1000, 10000, 65535);
        string code;
        code.append("def f():\n");
        code.append(" d = [\n");
        for (size_t i = 0 ; i < n_keys; i++){
            char snip[30];
            snprintf(snip, 30, " 'test_%zu',", i);
            code.append(snip);
        }
        code.append(" ]\n");
        code.append(" return len(d)\n");
        auto t = EmissionTest(
                code.c_str()
        );
        CHECK(stoi(t.returns()) == n_keys);
    }

    SECTION("test BUILD_SET") {
        auto n_keys = GENERATE(100, 1000, 10000, 65535);
        string code;
        code.append("def f():\n");
        code.append(" d = {\n");
        for (size_t i = 0 ; i < n_keys; i++){
            char snip[30];
            snprintf(snip, 30, " 'test_%zu',", i);
            code.append(snip);
        }
        code.append(" }\n");
        code.append(" return len(d)\n");
        auto t = EmissionTest(
                code.c_str()
        );
        CHECK(stoi(t.returns()) == n_keys);
    }
}