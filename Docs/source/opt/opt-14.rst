.. _OPT-14:

OPT-14 Optimize function calls which use the CALL_FUNCTION opcode
=================================================================

Background
----------

There are a few different opcodes for calling functions. One such opcode is ``CALL_FUNCTION``. This opcode is reserved for non-keyword argument calls.

Each time a function is called, one of these opcodes will run and the way to call function is determined by inspecting it's type and some flags.

There are two main paths for execution of functions. The main is ``PyObject_Call()``, used for any callable object, including native Python functions.
The second is ``PyVector_Call()``, used for types which support PEP590 (vector call) https://www.python.org/dev/peps/pep-0590/.

Builtin functions (those in the ``__builtins__`` module), like ``print``, ``repr``, ``int()`` are all implemented in C and all support PEP590.

Because Pyjion knows which function calls are to builtins at compile-time and what flags those function support, some optimizations can be made to calling builtin functions.

Solution
--------

At compile-time, Pyjion will look at the builtin function and determine which is the most optimal execution path, either:

* ``PyObject_Call`` (slowest)
* ``PyVector_Call`` (faster)
* Direct method call (fastest)

This uses the feature added in OPT-13 where the method descriptors for C function can be specified at compile-time and the JIT will compile in the correct ABI/calling convention for that method through
the .NET JIT.

Gains
-----

* Calls to builtin functions where the code does not use keyword arguments are faster

Edge-cases
----------

* None?

Further Enhancements
--------------------

* Process can be applied to any function. Expand this through the use of profiling

Configuration
-------------

This optimization is enabled at **level 1** by default. See :ref:`Optimizations <optimizations>` for help on changing runtime optimization settings.

+------------------------------+---------------------------------------+
| Compile-time flag            |  ``OPTIMIZE_FUNCTION_CALLS=OFF``      |
+------------------------------+---------------------------------------+
| Default optimization level   |  ``1``                                |
+------------------------------+---------------------------------------+
