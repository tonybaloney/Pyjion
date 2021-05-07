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

#include "pyjitallocator.h"

static PyMemAllocatorEx g_originalAllocator ; // Whichever allocator was set before JIT was enabled.

static void *
_PyJit_Malloc(void *ctx, size_t size)
{
    if (ctx != nullptr) {
        auto allocatorProfile = static_cast<Pyjit_AllocatorProfile*>(ctx);
        if (allocatorProfile->profile != nullptr && allocatorProfile->profile->status == PgcStatus::CompiledWithProbes) {
            allocatorProfile->profile->captureMalloc(allocatorProfile->executionCount, size);
        }
        if (allocatorProfile->profile != nullptr && allocatorProfile->profile->status == Optimized) {
            // See if allocated in profile pool
            if (allocatorProfile->pools.find(size) != allocatorProfile->pools.end()){
                auto addr = (allocatorProfile->pools[size].address + (size * allocatorProfile->pools[size].allocated));
                if (addr < allocatorProfile->pools[size].ceiling) {
                    allocatorProfile->pools[size].allocated++;
                    return (void *) addr;
                }
            }
        }
    }
    return g_originalAllocator.malloc(nullptr, size);
}

static void *
_PyJit_Calloc(void *ctx, size_t nelem, size_t elsize)
{
    return g_originalAllocator.calloc(nullptr, nelem, elsize);
}

static void *
_PyJit_Realloc(void *ctx, void *ptr, size_t size)
{
    return g_originalAllocator.realloc(nullptr, ptr, size);
}

static void
_PyJit_Free(void *ctx, void *ptr)
{
    if (ctx != nullptr) {
        auto allocatorProfile = static_cast<Pyjit_AllocatorProfile *>(ctx);
        for (auto & p: allocatorProfile->pools) {
            if (p.second.address > (uintptr_t)ptr && (uintptr_t)ptr < p.second.ceiling){
                // TODO : Mark as freed.
                return;
            }
        }
    }
    g_originalAllocator.free(nullptr, ptr);
}

static PyMemAllocatorEx PYJIT_ALLOC = {NULL, _PyJit_Malloc, _PyJit_Calloc, _PyJit_Realloc, _PyJit_Free};

void Pyjit_SetAllocatorContext(Pyjit_AllocatorProfile * profile) {
    PYJIT_ALLOC.ctx = profile;
    PyMem_SetAllocator(PYMEM_DOMAIN_OBJ, &PYJIT_ALLOC);
}

void Pyjit_ResetAllocatorContext() {
    PYJIT_ALLOC.ctx = nullptr;
    PyMem_SetAllocator(PYMEM_DOMAIN_OBJ, &PYJIT_ALLOC);
}

void Pyjit_AllocatorInit(){
    PyMem_GetAllocator(PYMEM_DOMAIN_OBJ, &g_originalAllocator);
    PyMem_SetAllocator(PYMEM_DOMAIN_OBJ, &PYJIT_ALLOC);
}

Pyjit_AllocatorProfile Pyjit_InitAllocator(PyjionCodeProfile* profile, size_t execCnt) {
    if (profile != nullptr && profile->status == Optimized) {
        // Allocate pools
        unordered_map<size_t, Pyjit_AllocatorPool> pools;

        for (auto& a: profile->getAllocations()){
            pools[a.first] = {
                    (uintptr_t )malloc(a.first * a.second),
                    0
            };
        }
        return {profile, execCnt, pools};
    }
    return {profile, execCnt};
}