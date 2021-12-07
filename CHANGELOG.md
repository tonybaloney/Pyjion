# Release notes

## 1.2.0

* PGC unboxing errors are avoided when functions are called with different argument types to those it was optimized with. 
* PGC types will now be inferred across jump statements, for loops and other small scopes, improving performance
* LOAD_METHOD will use cached pointers for builtin types like `dict`, `list`, etc. meaning LOAD_METHOD is faster in many cases
* Dictionary merge operators, `|` and `|=` will assert the return type as dict
* Fixes a crash on Windows when referencing call points or sequence points for a free'd module
* Fixes an issue running `pyjion -m module` with arguments

## 1.1.1

* Fixed a critical bug where recursive functions that use a mutable container type (e.g. list) causes a decref to the wrong object and subsequent crash.
* Fixes a bug on graph generation for recursive functions causing a crash in some situations
* Fixes a bug on method calls, which called the wrong method when the class was copied using `copy.copy()`
* Reduced memory consumption for method calls

## 1.1.0

* Added unboxed integer operations for BINARY_LSHIFT, BINARY_RSHIFT, BINARY_AND, BINARY_OR, BINARY_XOR
* BINARY_MULTIPLY and BINARY_POWER will stay unboxed if the right-hand operator is a constant that won't overflow (e.g. x ** 2)
* Added unboxed UNARY_NOT,UNARY_POSITIVE and UNARY_NEGATIVE operation for float, bool and int types
* Added unboxed UNARY_INVERT for int and bool types
* Added unboxed STORE_SUBSCR for bytearrays
* The types of global variables are profiled at compile-time
* Improved performance of bytearrays, by adding an unboxed bytearray type and unboxed slice operations for bytearrays, yielding unboxed integers
* Fixed a reference count bug with unboxed range iterators
* PGC will now allow an int of value 0 or 1 to be unboxed into a bool
* Unboxing of integers is now more efficient and allows for True to be unboxed into 1 and False into 0

## 1.0.0

* Fix a bug on `pyjion.config()` crashing when called with invalid arguments (#401)
* Update to .NET 6 GA
* LOAD_GLOBAL is faster when neither globals or builtins have changed since compiling, by caching the object address.
* Calls to Python functions are now routed through Vector Call (faster)
* Improved performance of calling builtin types, like int(), str(), etc.
* Calls to C Functions are faster
* Added an OptimisticIntegers optimization to level 2 to inspect integer values and unbox them if they're small
* Abstract value profiler will assert tuples of constant values (e.g. float, int) based on value
* LOAD_ATTR will infer the attribute type of classes using a cached attribute lookup table when optimization `AttrTypeTable` (level 1) is enabled
* Fix a bug that would reset the optimization level to 1 whenever `pyjion.enable()` was called
* Fix a bug that would cause an invalid jump operation and a failed compilation for EXTENDED_ARG jumps which occur in big functions

## 1.0.0 (rc4)

* Added ARM64 for Apple macOS (M1 Silicon). The JIT will emit native ARM64 assembly. Disassembler not supported.
* Added Linux/ARM64 support and a manylinux wheel
* Added Windows/ARM64 support
* All JIT configuration is now set using the `pyjion.config()` function instead of `enable_feature()`/`disable_feature()`
* Fixed a bug where `pyjion.graph()` would cause an exception if graphing was not enabled when the function was compiled
* Added support for the `SETUP_ANNOTATIONS` opcode
* Added support for generators in 3.10
* Fixed a bug in the DUP_TOP_TWO opcode with escaped variables
* UNPACK_SEQUENCE will emit types for constant tuples of string, int and float
* STORE_FAST is now profiled during the probe stage of PGC, leading to drastically improved performance in many cases where unboxed ints and floats can be used
* Added a simple benchmark suite script (`Tests/benchmarks/suite.py`)
* Upgraded scikit-build to 0.12 for native VS2019 support

## 1.0.0 (rc3)

* Updated to .NET 6 RC1
* Added Python 3.10 support
* Experimental `try..except` statements, stacked exception handlers and exception filtering. Disabled by default
* Fixed a bug on LOAD_METHOD not raising an exception immediately if the method could not be resolved
* Remove `enable_tracing()` and `enable_profiling` functions in 3.10 builds. Profiling and tracing is detected on compilation and available via the `.info()` method
* Added support for PEP626 (fast line tracing)
* Extended profiling support for CFunction calls
* PyTrace_RETURN now correctly sends the return value as an argument as per CPython spec
* Fixed a bug causing SEG when range iterators are escaped 

## 1.0.0 (rc2)

* BINARY_MULTIPLY and BINARY_POWER will assume the resulting integer is a big integer (not unboxed)
* Introduced two optimizations IntegerUnboxingMultiply and IntegerUnboxingPower which are applied at optimization level 2. Try level two if you work with integers, but at smaller values to see better performance.
* Pyjion will infer that `range(n)` generates integers in iterator to improve unboxing
* LOAD_BUILD_CLASS will infer a function type instead of Any (#42)
* Instruction graphs will include the name of fast locals
* Instruction graph const values are capped to 40 characters 
* Added abstract types for all builtins (#339)
* `pyjion.info()` will now return a JitInfo object
* Optimization flags that were applied to a function during compilation are available in `JitInfo.optimizations`
* All optimizations are now runtime flags instead of compile-time features
* Unboxing PGC errors will raise `pyjion.PyjionUnboxingError` (ValueError) instead of ValueError
* Instruction graphs will show conditional branches (in orange)
* Fixed a bug in generators where mixed unboxed/boxed fast locals would yield the wrong values with PGC between the first and second compilation stages
* Fixed a de-optimization that happened in rc1 due to PGC asserting the abstract kind on the profiling phase and then always asserting that integers were big-integers
* Fixed a bug where unboxed locals were decrefing frame locals on yield
* Generators will not unbox fast locals for stability reasons
* Fixed a regression on unboxed integers, caused by PGC values being set as Any
* Assert return types for float object methods (as_integer_ratio, conjugate, is_integer, hex)

## 1.0.0 (rc1)

* Added `pyjion` command-line script to complement the `python -m pyjion` command
* The pyjion CLI has flags for enabling profiling, tracing, optimization level, graphs and debugging
* Unboxing integers that don't fit into `long long` will raise a ValueError.
* Pyjion will mark any values above 1 billion as "big integers" and not escape them to reduce the chance of overflows.
* Floating point `__pow__` with negative values matches all behaviour of CPython
* Raising `0` to a negative power will raise a `ZeroDivisionError`
* PGC no longer uses a reference to probed values, dramatically reducing memory consumption between the first and second compile cycles
* Fixed a bug where `statistics.variance([0, 0, 1])` would raise an assertion error because of an overflow raised in Fraction arithmetic (#326)
* Fixed a bug on calling `sys.settrace(None)` would cause a segmentation fault (#330)
* Fixed a bug on optimized calls to custom types would crash on the 3rd execution because of the way PGC held and released references.
* Refactored Pyjion's test suite to Pytest
* Rewrote the documentation site
* Fixed a bug in the native disassembler printing empty comment lines
* Corrected the type signature of `pyjion.get_offsets()`
* Fixed a bug on changed methods for object (like a global) causing crashes because of the way load_method was optimized (#335)

## 1.0.0 (beta7)

* Added `pyjion.symbols(callable)` API to fetch the dictionary of external call tokens
* Extended the `dis()` and `dis_native()` methods with a flag to not print the program counter (`print_pc=False`)
* Improved the `dis_native()` method to print the name of the method call after the `call` instructions as a line comment
* Fixed a bug in `dis_native()` which showed unresolved sequence points at the top of the printout  
* Fixed a bug where `in` (CONTAINS_OP) result wasn't being checked for exceptions and the next operation would segfault if the `in` operation returned an error result.
* The IL in `dis()` is closer in syntax to ILDasm and easier to read
* Added a `pyjion.status()` method to get runtime data on the JIT
* Windows will now observe the `DOTNET_ROOT` and `DOTNET_LIB_PATH` environment variables

## 1.0.0 (beta6)

* Updated to .NET 6 preview 6
* Fixed a bug where `ord()` builtin would return the wrong type (#315)
* `pyjion.dis.dis()` and `pyjion.dis.dis_native()` will show sequence points as comments
* The BINARY_POWER and INPLACE_POWER opcodes will always return a native python long instead of an escaped integer, to avoid overflows

## 1.0.0 (beta5)

* Fixed a bug on large dictionary literals (>100 keys)
* Improved the efficiency of the BUILD_TUPLE, BUILD_LIST, BUILD_CONST_KEY_MAP, and BUILD_SET opcodes
* Fixed a bug with comparison of numpy arrays being unboxed into a boolean instead of staying as an array (#310)

## 1.0.0 (beta4)

* Support for the `yield` keyword and Python generators

## 1.0.0 (beta3)

* Instruction graphs can be enabled with `pyjion.enable_graphs()` and then exported by `pyjion.get_graph(f)`
* Pyjion will raise an ImportError if .NET is missing instead of a system exit

## 1.0.0 (beta2)

* Fast locals can store unboxed values
* Method calls are optimized for known types by asserting the return type, e.g. `str.upper()` returns a string
* Updated to .NET 6 preview 5
* Pyjion can be run using `python -m pyjion <command>`, e.g. `python -m pyjion script.py` or `python -m pyjion -m unittest`

## 1.0.0 (beta1)

* Added unboxing for integers (OPT-16)
* Added unboxing for bool
* Fixed a bug with interned hash maps on Windows

## 1.0.0 (alpha4)

* Added unboxing and escape analysis for floating point objects (OPT-16)
* Removed OPT-8 as it is superseded by OPT-16

## 1.0.0 (alpha3)

* Updated to .NET 6 preview 4

## 1.0.0 (alpha2)

* Debuggable JIT methods can be toggled at runtime using `pyjion.enable_debug()`/`pyjion.disable_debug()`
* Added option for including Python bytecode disassembly in Pyjion disassemble printouts on `pyjion.dis.dis()` and `pyjion.dis.dis_native()`
* Added API `pyjion.get_offsets(callable)` to get the offsets of Python Opcode <> IL offset <> native offset.
* Moved internal representations to fixed width standard types.

## 1.0.0 (alpha1)

* Pyjion uses .NET 6 Preview 3 as the compiler, for Linux and macOS make sure you have installed it first
* Rich comparisons (==, <, >) of floating point numbers are significantly faster (OPT-17)
* All method calls are faster by enforcing vectorcall protocol and inlining anything below 10 arguments (OPT-16)

## 0.15.0

* PGC now observes and optimizes heap-allocated (user-defined) types
* Fixed a crash on certain recursive functions with PGC enabled

## 0.14.1 

* Fixed macOS wheel name

## 0.14.0

* LOAD_ATTR is now optimized by for types that implement the tp_getattr by prehashing the names (OPT-15)
* JIT will emit a direct call to LOAD_ATTR tp_getattro/tp_getattr slots for builtin types
* macOS wheels are now compiled with Clang PGO
* PGC will only profile non heap-allocated types (ie not user specified types) as type objects could be deallocated between compilation cycles
* Reduced stack effect during frame calls/function calls
* Improved performance on function calls
* Py_MakePendingCalls will be called every 100 instructions (previously 10), configurable at compile-time through the `EMIT_PENDING_CALL_COUNTER` definition
* Updated to .NET 5.0.5 (5.0.202)
* Fixed a bug in PGC for large functions meaning they wouldn't be optimized
* Implemented PGC for BINARY_SUBSCR (OPT-5)
* Implemented PGC for STORE_SUBSCR (OPT-6)
* Implemented PGC for all inplace and regular binary operators (+, -, / etc) see OPT-13

## 0.13.0

* The compiler will now fail (and default back to CPython) if .NET emits a FAST_FAIL helper
* UNPACK_SEQUENCE is rewritten to be more efficient and use optimized  paths for LIST and TUPLE types
* f-string (BUILD_STRING) is rewritten to be more efficient
* UNPACK_EX is rewritten to remove the requirement for dynamic heap allocation (and the stack canary) and leverage .NET compiler's dynamic eval stack
* PGC implemented for UNPACK_SEQUENCE
* PGC implemented for BINARY_SUBSCR
* PGC implemented for CALL_FUNCTION/OPT-14

## 0.12.0

* Added PGC emitter to first compile pass
* Drastically simplified the compilation process, resulting in a smaller call stack and allowing for more recursion (and better performance)
* Added a field to the pyjion.info() dictinary, `compile_result`, indicating cause of compilation failure (if failed), see `AbstractInterpreterResult` for enumerations
* Fixed a bug in pyjion.dump_native/pyjion.dis.dis_native disassembling the wrapper function
* Incompatible functions (those with async, yield keyword) are marked as incompatible early in the compilation process
* Fixed a bug in OPT-13 if the type changed under certain circumstances
* Arguments to a frame are now marked as volatile and requiring type guards for certain optimizations
* Any Python type passed as an argument is now available to be optimized by OPT-13, OPT-12
* Fixed a bug occuring on Linux and Windows in sre_parse._compile which caused a GuardStackException when doing an inline decref operation.
* Added an environment variable DOTNET_LIB_PATH to allow specifying the exact path to libclrjit

## 0.11.0

* Added OPT-13 (OPTIMIZE_TYPESLOT_LOOKUPS) to optimize the type slots for all binary operators and resolve the precedence at compile-time (only for known types)
* Added OPT-14 (OPTIMIZE_FUNCTION_CALLS) to optimize calls to builtin functions
* Optimize all frame locals by determining abstract types on compilation
* Bugfix: Fixed a crash on f-strings with lots (>255) arguments
* Bugfix: Will now skip all functions containing the use of `exec()` as it contains frame globals which are not supported
* Updated to .NET 5.0.3
* Updated the containers to Ubuntu 20
* Added fileobject abstract type
* Added enumerator abstract type
* Added code object abstract type
* Added integration tests for reference leaks for all binary operations (thanks @amaeckelberghe)
* Added module type (thanks @vacowboy75)

## 0.10.0

* Added OPT-12 (OPTIMIZE_BUILTIN_METHOD) to pre-lookup methods for builtin types and bypass LOAD_METHOD (PyObject_GetMethod)
* Optimized LOAD_METHOD to recycle lookups for the same object
* Expanded OPT-8, OPT-9, OPT-11, OPT-12 for nested stacks (e.g. inside expressions)
* Added a frozen set abstract type

## 0.9.0

* Added OPT-11 (OPTIMIZE_BINARY_SLICE) to optimize the BUILD_SLICE and BINARY_SUBSCR operations into a single function when the slice start, stop and step is None or a const number.
* Fixed a bug in the set_optimization_level() being reset (thanks @tetsuo-cpp)
* Added a bytearray abstract value kind (thanks @tetsuo-cpp)
* Added a type abstract value kind  (thanks @tetsuo-cpp)
* Optimized the compiled instructions to only update the frame last instruction field on error/exit branch
* Removed the "periodic work" method which was called for every for/while loop and put a function to call Py_MakePendingCalls for every 10th loop
* Added an improvement to the process stage to infer the abstract types of return values to methods of builtin types, e.g. str.encode
* Added a check in dis_native for when the compiled function wasn't compiled (thanks @tetsuo-cpp)
* dis_native will now pretty print the assembly code when the `rich` package is installed (thanks @C4ptainCrunch)
* `pyjion[dis]` is a new package bundled with pystorm3 and rich (thanks @C4ptainCrunch)

## 0.8.0

* Enhanced the process stage of the compiler with new abstract types, iterable, bytearray, codeobject, frozenset, enumerator, file, type and module
* Process stage will assert the abstract return type of any call to a builtin function (e.g. list(), tuple()), which will kick in the optimizations for a broader set of scenarios
* Added OPT-8 (OPTIMIZE_BINARY_FUNCTIONS) to combine 2 sequential binary operations into a single operation. Adds about 15-20% performance gain on PyFloat operations.
* Added OPT-9 (OPTIMIZE_ITERATORS) to inline the FOR_ITER opcode of a listiter (List iterator) into native assembly instructions.
* Added OPT-10 (OPTIMIZE_HASHED_NAMES) to precompute the hashes for LOAD_NAME and LOAD_GLOBAL dictionary lookups
* Fixed a bug where looking up a known hash for a dictionary object (optimized BINARY_SUBSCR) wouldn't raise a KeyError. Seen in #157

## 0.7.0

* Fixed a bug in JUMP_IF_FALSE_OR_POP/JUMP_IF_TRUE_OR_POP opcodes emitting a stack growth, which would cause a stack underflow on subsequent branch checks. JIT will compile a broader range of functions now
* Implemented PEP590 vector calls for methods with 10+ arguments (thanks @tetsuo-cpp)
* Implemented PEP590 vector calls for functions with 10+ arguments
* Fixed a reference leak on method calls with large number of arguments
* Support for tracing of function calls with 10+ arguments
* Disabled OPT-4 as it is causing reference leaks

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
