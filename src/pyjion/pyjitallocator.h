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

#ifndef SRC_PYJION_PYJITALLOCATOR_H
#define SRC_PYJION_PYJITALLOCATOR_H

#include <Python.h>
#include "pgocodeprofile.h"

static void * _PyJit_Malloc(void *ctx, size_t size);
static void * _PyJit_Calloc(void *ctx, size_t nelem, size_t elsize);
static void * _PyJit_Realloc(void *ctx, void *ptr, size_t size);

static void _PyJit_Free(void *ctx, void *ptr);

void Pyjit_SetAllocatorProfile(PyjionCodeProfile* profile);
void Pyjit_UnsetAllocatorProfile();

void Pyjit_AllocatorInit();

#endif //SRC_PYJION_PYJITALLOCATOR_H
