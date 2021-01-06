# Release notes

## 0.6.0

* Added OPT-6 optimization. Frame constants are now used to speed up assignments to lists and dictionaries. STORE_SUBSCR will assert if something is a list, or dict and shortcut
the assignment logic.
* Added OPT-7 optimization. The binary subscript operator is compiled to faster path under a set of circumstances, especially if the index/key is a frame constant. Hashes are precomputed
 and indexes for integer constants are converted to native numbers at compile-time.
* The native machine-code disassembler will show the actual position of the JITed code in memory, instead of starting the offset at 0
* The `pyjion.dump_native()` function returns a tuple with bytes, length and position
* Type inferencing has been improved for all inplace and binary operations
* Windows builds from source are fixed for when the user wants to compile against a checkout of .NET
* Implemented FAST_DISPATCH for additional opcodes
* Added a test runner for the CPython regression suite that tests the JIT in isolation
* Fixed a reference leak of (self) for the LOAD_METHOD opcode
* Fixed a reference leak of non C functions being called via Call<N> (CALL_FUNCTION)
* Fixed a bug where (very) large tuples being created via the BUILD_TUPLE opcode would cause an overflow error
* Fixed a bug on BUILD_MAP being called with very large dictionaries caused a fatal error


## 0.5.0

* Added OPT-4 optimization. Frame locals (named variables known at compilation) using the LOAD_FAST, STORE_FAST and DELETE_FAST
 opcodes will use native .NET locals instead of using the frame's f_localsplus array.
* Improved performance in LOAD_FAST and STORE_FAST through OPT-4
* Added OPT-5 optimization. Frame push/pop on entry/exit are now inline CIL instructions.
* LOAD_FAST skips unbound local checks when proceeded by a STORE_FAST (i.e. slot is definitely assigned)

## 0.4.0

* Fixed a crash bug where CPython checks recursion depth from ceval state, which may not be set
* Implemented a faster check for recursion depth
* Fixed a bug on LOAD_CLOSURE operator not being set
* Fixed OPT-2 on Windows and Linux
* Fixed a bug where the wrong CIL opcode was being used to subtract values, would throw an overflow error and fail back into EFD.
* Implemented the .NET EE exception handlers for guard stack canaries, overflow errors, and null reference exceptions
* Implemented a more efficient case of ld_i(1)
* Corrected cases of ob_refcnt to use 64-bit signed integers
* No longer print error messages on release code for unimplemented .NET EE methods
* Fixed a bug on the incorrect vtable relative field being set
* Fixed a bug where tracing and profiling would be emitted even when not explicitly enabled
* .NET Exceptions are transferred into Python exceptions at runtime

## 0.3.0

* Added an optimization (OPT-1/OPTIMIZE_IS) to inline the "is"/ "is not" statement into a simple pointer comparison with jump statement. Compiles to inline machine code instead of a method call
* Added an optimization (OPT-2/OPTIMIZE_DECREF) to decrement the refcount without a method call, when the object refcount is >1 and then call _Py_dealloc if the ref count becomes 0. Replaces the previous method call
* Windows now uses the system page size instead of the default value of 1MB

## 0.2.1

* Added support for .NET 5.0.1
* Implemented a CIL modulus emitter

## 0.2.0

* Added support for profiling compiled functions by enabling profiling (`pyjion.enable_profiling()`)
* Added support for profiling C function calls, returns and exceptions
* Implemented a faster call path for functions and methods for 5-10 arguments
* Fixed a bug where the page size defaulted to 0 in the .NET EE, which caused a failed assertion (and fails to compile the function), would fix a large % of functions that previously failed to compile

## 0.1.0

* Added support for debugging compiled functions and modules by enabling tracing (`pyjion.enable_tracing()`)
* Added support for debugging to catch unhandled/handled exceptions at runtime when tracing is enabled
* Added support for opcode-level tracing
* Fixed a bug on executing Pyjion with pydevd (VScode/PyCharm debugger) would cause the Python process to crash because of a doubly-freed code object (#7)

## 0.0.7

* Added a WSGI middleware function to enable Pyjion for Flask and Django (#67)
* Fix a bug on dictionary merging for mapping types incorrectly raising a type error (#66)

## 0.0.6

* Implemented supported for disassembling "large" methods into CIL (#27)
* Added type stubs for the pyjion C extension
* Fix a bug where merging or updating a subclassed dictionary would fail with a type error. (#28)

## 0.0.5

* Fixed a critical bug where method calls with large numbers of arguments, and the argument was a tuple could cause a segmentation fault on GC collection.
* Tested support for IPython REPL
* Fixed a bug where importing pyjion.dis after enabling the JIT would cause a stack overflow
* Has around 50% chance of working and not causing your computer to explode, or worse, segmentation fault

## 0.0.4

* Added a stack probe helper for Linux (will use JIT in more scenarios)
* Enabled support for running unit tests in Linux
* Fixed a bug where JIT would crash when a method call failed because of a bad-lookup
* Implemented helper method redirection for Linux to support PIC compiled symbols
* Has around 35% chance of working and not causing your computer to explode, or worse, segmentation fault
* Improved discovery of .NET libraries on Linux
* Fixed a bug where a garble-named log file would be generated (should be JIT timings log)

## 0.0.3

* Installable bdist_wheel for Ubuntu, Debian, macOS 10.15, 11 (10.16) and Windows x64
* Installable manylinux2014 wheel with clrjit.so bundled in
* Added multithreading/multiprocessing support
* Fixed a bug where the wheel would be broken if there are two distributions of Python 3.9 on the system
* Has around 30% chance of working and not causing your computer to explode, or worse, segmentation fault.

## 0.0.2

* Installable source distribution support for macOS, Windows and (barely) Linux.

## 0.0.1

* It compiles on my machine 