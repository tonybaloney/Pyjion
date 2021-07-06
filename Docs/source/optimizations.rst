.. _Optimizations:

Built-in Optimizations
======================

.. toctree::
    :caption: Index:

    opt/opt-1
    opt/opt-2
    opt/opt-3
    opt/opt-5
    opt/opt-6
    opt/opt-7
    opt/opt-9
    opt/opt-10
    opt/opt-12
    opt/opt-13
    opt/opt-14
    opt/opt-15
    opt/opt-16

Overview
--------

Pyjion is a JIT compiler. It compiles native CPython bytecode into machine code. Without Pyjion, CPython uses a master evaluation loop (called the frame evaluation loop) to iterate over opcodes

The Pyjion compiler has 3 main stages:

1. Build a "stack table" of the abstract types at each opcode position
2. Compile CPython opcodes into CIL opcodes
3. Emit the CIL opcodes into the .NET EE compiler to convert to native machine code/assembly

Guiding principles
~~~~~~~~~~~~~~~~~~

Optimizations must follow these principles:

- They must support custom objects (remember that everything can be overidden in Python!)
- They must pass the CPython test suite
- They must be isolated and testable
- They must be atomic and simple

Compiler design
~~~~~~~~~~~~~~~

The rough design of the compile stage is to emit isolated CIL opcodes for each bytecode in the compiled frame.

Because of the flexibility of Python's type system, the opcodes will mimic CPython's eval loop:

- Take/put references onto a value stack
- Call a C API method
- Take/put results onto a value stack
- Check for errors
- Go to next

There is some added complexity, because .NET CIL requires that the value stack is a LIFO stack, whereas CPython implements a value "stack" but actually uses it like an array.

The calling of the C API method is wrapped by the .NET ``CEE_CALL`` opcode, which calls a wrapper method implemented in the ``intrins.cpp`` file.

Without further optimizations, Pyjion would perform roughly the same as CPython (as fast or slow as the C API methods its calling).

The benefits of Pyjion come from changing the emitted machine code by deterministic observations about the inputs, constants or environment.

Machine code optimizations
~~~~~~~~~~~~~~~~~~~~~~~~~~

CIL to machine code/assembly optimizations are done by the .NET/EE compiler. They are configured by ``CorJitInfo::getJitFlags()``.

By default, Pyjion will flag the EE compiler to use the ``CORJIT_FLAG_SPEED_OPT`` profile. If you want to compile "debuggable" JIT code, use the ``EE_DEBUG_CODE`` option in CMake.

Boxing and unboxing of variables
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Work was done in early versions of Pyjion to box/unbox Python integers and floats into native long/float types. This allows for native CIL opcodes like ADD, SUB, IMUL, etc. to be used
on native integers instead of having to use the complex (and slow) ``PyLongObject``/``PyFloatObject`` operations.
I've found this initial prototype to be unstable so the boxing and unboxing has been completely rewritten.

Pyjion's escape analysis is acheived using an instruction graph to traverse supported unboxed types and opcodes then to tag transition stack variables as unboxed.
This functionality complements PGC.

Tracing/profiling
~~~~~~~~~~~~~~~~~

Neither tracing or profiling callbacks will be emitted in the compiled code by default. This is advantageous over CPython, which would otherwise check the state of the tracing/profiling flag for every opcode.

References
----------

- Checkout `EMCA CIL reference <https://github.com/tonybaloney/ecma-335/tree/master/docs>`_ for a list of what is possible in the CIL.
- See my book for a comprehensive guide to the CPython compiler and design `CPython Internals (ISBN 9781775093350) <https://realpython.com/products/cpython-internals-book/>`_
- Checkout `Discussions <https://github.com/tonybaloney/Pyjion/discussions/90>`_ for any discussion about potential optimizations.

Optimization Matrix
-------------------

.. list-table:: Default Optimization Levels
   :widths: 40 20 20 20
   :header-rows: 1

   * - Optimization
     - Level 0
     - Level 1
     - Level 2
   * - :ref:`OPT-1`
     - Off
     - On
     - On
   * - :ref:`OPT-2`
     - Off
     - On
     - On
   * - :ref:`OPT-3`
     - Off
     - On
     - On
   * - :ref:`OPT-5`
     - Off
     - On
     - On
   * - :ref:`OPT-6`
     - Off
     - On
     - On
   * - :ref:`OPT-7`
     - Off
     - On
     - On
   * - :ref:`OPT-9`
     - Off
     - On
     - On
   * - :ref:`OPT-10`
     - Off
     - On
     - On
   * - :ref:`OPT-12`
     - Off
     - On
     - On
   * - :ref:`OPT-13`
     - Off
     - On
     - On
   * - :ref:`OPT-14`
     - Off
     - On
     - On
   * - :ref:`OPT-15`
     - Off
     - On
     - On
   * - :ref:`OPT-16`
     - Off
     - On
     - On

Configuring Optimizations
-------------------------

Python API
~~~~~~~~~~

The optimization level can be set using the ``set_optimization_level(level: int)`` method:

.. code-block:: Python

    import pyjion
    pyjion.set_optimization_level(0)

The default level is 1.
Setting to level 2 will enable **all** optimizations.

Level 0 disables all optimizations.

Compile-time configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~

Optimizations are set in the ``CMakeOptimizations.txt`` file and can be individually set.

For example, to disable the OPTIMIZE_DECREF (OPT-1) at level 1 (default):

``cmake -OPTIMIZE_DECREF=OFF``
