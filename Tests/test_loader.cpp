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
#include "peloader.h"
#include "corehost.h"
#include "error_codes.h"
#include <filesystem>

TEST_CASE("Test basic loader") {
    SECTION("Test builtin R2R image") {
        auto dotnetroot = getenv("DOTNET_ROOT");
        auto version = getenv("DOTNET_VERSION");
        std::filesystem::path console_path = ( std::filesystem::path(dotnetroot) / std::filesystem::path("shared/Microsoft.NETCore.App/") / std::filesystem::path(version) / std::filesystem::path("System.Console.dll"));
        PEDecoder decoder = PEDecoder(console_path.c_str());
        CHECK(decoder.GetCorHeader()->Flags & COMIMAGE_FLAGS_IL_LIBRARY);
        CHECK(decoder.GetReadyToRunHeader()->MajorVersion == 5);
        CHECK(decoder.GetModuleName() == "System.Console.dll");
        auto typeRefs = decoder.GetTypeRefs();
        CHECK(typeRefs.size() == 125);
        CHECK(typeRefs[124].Name == 6055);
        auto typeDefs = decoder.GetTypeDefs();
        CHECK(typeDefs.size() == 53);
        CHECK(typeDefs[0].Flags == 0);
        CHECK(typeDefs[0].Name == 0x0577);
        CHECK(typeDefs[0].Namespace == 0);
        CHECK(typeDefs[0].Extends == 0);
        CHECK(typeDefs[0].FieldList == 1);
        CHECK(typeDefs[0].MethodList == 1);

        CHECK(typeDefs[1].Flags == 0x00100100);
        CHECK(typeDefs[1].Name == 0x1523);
        CHECK(typeDefs[1].Namespace == 0x2e74);
        CHECK(typeDefs[1].Extends == 5);
        CHECK(typeDefs[1].FieldList == 1);
        CHECK(typeDefs[1].MethodList == 1);
        // ...
        CHECK(typeDefs[40].Name == 2380);

        auto publicTypeDefs = decoder.GetPublicClasses();
        CHECK(publicTypeDefs.size() == 8);
        CHECK(decoder.GetString(publicTypeDefs[0].Name) == "Console");
        auto methods = decoder.GetClassMethods(publicTypeDefs[0]);
        CHECK(methods.size() == 59);
        CHECK(decoder.GetString(methods[0].Name) == "ReadKey");
        CHECK(decoder.GetString(methods[58].Name) == "Write");

        auto signature = decoder.GetBlob(methods[0].Signature);
        CHECK(signature.size() == 4);
        CHECK(!decoder.GetMethodParams(methods[58]).empty());
    }

    SECTION("Test method execution"){
        std::filesystem::path libPath = filesystem::path("../pyjion-lib/bin/Debug/Pyjionlib.dll");
        REQUIRE(std::filesystem::exists(libPath));
        CHECK(load_hostfxr());
        auto t = get_dotnet_load_assembly();
        REQUIRE(t != nullptr);

        component_entry_point_fn hello = nullptr;
        int ret = t(
                libPath.c_str(),
                "Pyjion.Test, Pyjionlib",
                "Hello",
                nullptr /*delegate_type_name*/,
                nullptr,
                (void**) &hello);
        REQUIRE(ret == Success);
        REQUIRE(hello != nullptr);

        struct lib_args
        {
            const char_t *message;
            int number;
        };
        for (int i = 0; i < 3; ++i)
        {
            lib_args args
                    {
                        "from host!",
                        i
                    };

            CHECK(hello(&args, sizeof(args)) == 0);
        }
    }

    SECTION("Test delegate method execution"){
        std::filesystem::path libPath = filesystem::path("../pyjion-lib/bin/Debug/Pyjionlib.dll");
        REQUIRE(std::filesystem::exists(libPath));
        CHECK(load_hostfxr());
        auto t = get_dotnet_load_assembly();
        REQUIRE(t != nullptr);
        typedef int (CORECLR_DELEGATE_CALLTYPE *custom_entry_point_fn)(int, int);
        custom_entry_point_fn hello2 = nullptr;
        int ret = t(
                libPath.c_str(),
                "Pyjion.Test, Pyjionlib",
                "Hello2",
                "Pyjion.Test+HelloDelegate, Pyjionlib" /*delegate_type_name*/,
                nullptr,
                (void**) &hello2);
        REQUIRE(ret == Success);
        REQUIRE(hello2 != nullptr);
        CHECK(hello2(3, 4) == 12);
    }

    SECTION("Test delegate plane execution"){
        std::filesystem::path libPath = filesystem::path("../pyjion-lib/bin/Debug/Pyjionlib.dll");
        REQUIRE(std::filesystem::exists(libPath));
        CHECK(load_hostfxr());
        auto load = get_dotnet_load_assembly();
        REQUIRE(load != nullptr);
        component_entry_point_fn dot = nullptr;
        struct lib_args
        {
            double x1;
            double y1;
            double z1;
            double d1;
            double x2;
            double y2;
            double z2;
            double d2;
        };

        int ret = load(
                libPath.c_str(),
                "Pyjion.PlaneOperations, Pyjionlib",
                "VectorDotProduct",
                nullptr,//"Pyjion.PlaneOperations+VectorDotProductDelegate, Pyjionlib" /*delegate_type_name*/,
                nullptr,
                (void**) &dot);

        switch (ret) {
            case 0: {
                REQUIRE(ret == Success);
                REQUIRE(dot != nullptr);
                lib_args args = {1., 2., 3., 4., 5., 6., 7., 8.};
                CHECK(dot(&args, sizeof(lib_args)) == 70.);
            }
                break;
            case COR_E_MISSINGMETHOD:
                FAIL("Missing method exception");
                break;
            case COR_E_ARGUMENTOUTOFRANGE:
                FAIL("Argument out of range exception");
                break;
            case COR_E_INVALIDOPERATION:
                FAIL("Invalid operation exception");
                break;
            case E_POINTER:
                FAIL("Invalid argument exception");
                break;
            case COR_E_TYPELOAD:
                FAIL("Type load failure");
                break;
            default:
                FAIL("Unexpected exception");
                break;
        }

    }

    SECTION("Test custom image") {
        std::filesystem::path libPath = filesystem::path("../pyjion-lib/bin/Debug/Pyjionlib.dll");
        REQUIRE(std::filesystem::exists(libPath));
        PEDecoder decoder = PEDecoder(libPath.c_str());
        CHECK(decoder.GetCorHeader()->Flags & COMIMAGE_FLAGS_ILONLY);
        CHECK(decoder.GetModuleName() == "Pyjionlib.dll");
        auto typeRefs = decoder.GetTypeRefs();
        CHECK(!typeRefs.empty());
        auto typeDefs = decoder.GetTypeDefs();
        CHECK(!typeDefs.empty());
        auto publicTypeDefs = decoder.GetPublicClasses();
        CHECK(!publicTypeDefs.empty());
        auto methods = decoder.GetClassMethods(publicTypeDefs[0]);
        CHECK(!methods.empty());
    }
}
