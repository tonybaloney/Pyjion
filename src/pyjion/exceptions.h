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

#ifndef PYJION_EXCEPTIONS_H
#define PYJION_EXCEPTIONS_H

using namespace std;

class PyjionJitRuntimeException: public std::exception{};

class GuardStackException: public PyjionJitRuntimeException {
public:
    GuardStackException() : PyjionJitRuntimeException() {};
    const char * what () const noexcept override
    {
        return "Guard Stack error.";
    }
};

class IntegerOverflowException: public PyjionJitRuntimeException {
public:
    IntegerOverflowException() : PyjionJitRuntimeException() {};
    const char * what () const noexcept override
    {
        return "Compiled CIL function contains an integer overflow.";
    }
};

class RangeCheckException: public PyjionJitRuntimeException {
public:
    RangeCheckException() : PyjionJitRuntimeException() {};
    const char * what () const noexcept override
    {
        return "Range check validation failed.";
    }
};

class DivisionByZeroException: public PyjionJitRuntimeException {
public:
    DivisionByZeroException() : PyjionJitRuntimeException() {};
    const char * what () const noexcept override
    {
        return "Division by zero error.";
    }
};

class NullReferenceException: public PyjionJitRuntimeException {
public:
    NullReferenceException() : PyjionJitRuntimeException() {};
    const char * what () const noexcept override
    {
        return "Null reference exception within JIT.";
    }
};

class CilVerficationException: public PyjionJitRuntimeException {
public:
    CilVerficationException() : PyjionJitRuntimeException() {};
    const char * what () const noexcept override
    {
        return "CIL verification error.";
    }
};

class UnmanagedCodeSecurityException: public PyjionJitRuntimeException {
public:
    UnmanagedCodeSecurityException() : PyjionJitRuntimeException() {};
    const char * what () const noexcept override
    {
        return "Unmanaged code security exception.";
    }
};

class UnsupportedHelperException: public PyjionJitRuntimeException {
    int ftn;
public:
    UnsupportedHelperException(int ftnNum) : PyjionJitRuntimeException() {
        ftn = ftnNum;
    };
    const char * what () const noexcept override
    {
        return "Unsupported EE helper requested.";
    }
};

#endif // PYJION_EXCEPTIONS_H