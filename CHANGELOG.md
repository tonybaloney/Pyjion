# Release notes

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