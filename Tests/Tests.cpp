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

#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

#include <Python.h>
#include <pyjit.h>

int main(int argc, char* const argv[]) {
    Py_Initialize();
    PyjionUnboxingError = PyErr_NewException("pyjion.PyjionUnboxingError", PyExc_ValueError, nullptr);
    auto dotnetroot = getenv("DOTNET_ROOT");
    auto version = getenv("DOTNET_VERSION");
    if (dotnetroot == nullptr || version == nullptr){
        printf("Test suite requires both DOTNET_ROOT and DOTNET_VERSION environment variables to be set");
        exit(1);
    }
    JitInit(dotnetroot, version);
    g_pyjionSettings.graph = true;
    g_pyjionSettings.debug = true;
    g_pyjionSettings.codeObjectSizeLimit = 1000000;
    g_pyjionSettings.exceptionHandling = true;
    setOptimizationLevel(2);
    int result = Catch::Session().run(argc, argv);
    Py_Finalize();
    return result;
}