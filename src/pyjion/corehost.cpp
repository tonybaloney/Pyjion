//
// Created by Anthony Shaw on 30/10/21.
//

// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

// Standard headers
#include <cassert>
#include <iostream>

#include "corehost.h"
#include "pyjit.h"

#include <coreclr_delegates.h>
#include <hostfxr.h>

#ifdef WINDOWS
#include <Windows.h>

#define STR(s) L ## s
#define CH(c) L ## c
#define DIR_SEPARATOR L'\\'

#else
#include <dlfcn.h>

#define STR(s) s
#define CH(c) c
#define DIR_SEPARATOR '/'

#endif

using string_t = std::basic_string<char_t>;

hostfxr_initialize_for_runtime_config_fn init_fptr = nullptr;
hostfxr_get_runtime_delegate_fn get_delegate_fptr = nullptr;
hostfxr_close_fn close_fptr = nullptr;

// Forward declarations
void *load_library(const char *);
void *get_export(void *, const char *);

#ifdef WINDOWS
void *load_library(const char_t* path)
{
    HMODULE h = ::LoadLibraryW(path);
    assert(h != nullptr);
    return (void*)h;
}
void *get_export(void *h, const char *name)
{
    void *f = ::GetProcAddress((HMODULE)h, name);
    assert(f != nullptr);
    return f;
}
#else
void *load_library(const char* path)
{
    void *h = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
    assert(h != nullptr);
    return h;
}
void *get_export(void *h, const char *name)
{
    void *f = dlsym(h, name);
    assert(f != nullptr);
    return f;
}
#endif

// <SnippetLoadHostFxr>
// Using the nethost library, discover the location of hostfxr and get exports
bool load_hostfxr()
{
    // Load hostfxr and get desired exports
    void *lib = load_library(g_pyjionSettings.hostfxrpath.c_str());
    init_fptr = (hostfxr_initialize_for_runtime_config_fn)get_export(lib, "hostfxr_initialize_for_runtime_config");
    get_delegate_fptr = (hostfxr_get_runtime_delegate_fn)get_export(lib, "hostfxr_get_runtime_delegate");
    close_fptr = (hostfxr_close_fn)get_export(lib, "hostfxr_close");

    return (init_fptr && get_delegate_fptr && close_fptr);
}

// <SnippetInitialize>
// Load and initialize .NET Core and get desired function pointer for scenario
load_assembly_and_get_function_pointer_fn get_dotnet_load_assembly()
{
    // Load .NET Core
    void *load_assembly_and_get_function_pointer = nullptr;
    hostfxr_handle cxt = nullptr;
    int rc = init_fptr(g_pyjionSettings.runtimeconfigpath.c_str(), nullptr, &cxt);
    if (rc > 2 || cxt == nullptr)
    {
        std::cerr << "Init failed: " << std::hex << std::showbase << rc << std::endl;
        close_fptr(cxt);
        return nullptr;
    }

    // Get the load assembly function pointer
    rc = get_delegate_fptr(
            cxt,
            hdt_load_assembly_and_get_function_pointer,
            &load_assembly_and_get_function_pointer);
    if (rc != 0 || load_assembly_and_get_function_pointer == nullptr)
        std::cerr << "Get delegate failed: " << std::hex << std::showbase << rc << std::endl;

    close_fptr(cxt);
    return (load_assembly_and_get_function_pointer_fn)load_assembly_and_get_function_pointer;
}
// </SnippetInitialize>
