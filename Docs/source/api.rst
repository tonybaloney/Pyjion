Pyjion Python API
=================

Core module
-----------

.. module:: pyjion

.. function:: enable()

   Enable the JIT

.. function:: disable()

   Disable the JIT

.. function:: enable_tracing()

   Enable tracing hooks (e.g. used by debuggers) to be emitted in the compiled CIL. Required for debugging.

.. function:: disable_tracing()

   Disable tracing hooks.

.. function:: enable_profiling()

   Enable profiling hooks (e.g. used by performance profilers) to be emitted in the compiled CIL.

.. function:: disable()

   Disable profiling hooks.

.. function:: set_threshold(to)

   Set the threshold to JIT compile a function to the number of times it is executed.

.. function:: dump_il(f)

   Return the ECMA CIL bytecode as a bytearray

.. function:: dump_native(f)

   Return the compiled machine-code as a bytearray

Disassembly module
------------------

.. module:: pyjion.dis

.. function:: dis(f)

   Print the ECMA CIL bytecode in a disassembly table

.. function:: dis_native(f)

   Print the x86 assembly instructions in a disassembly table (required distorm3)

WSGI middleware
---------------

.. module:: pyjion.wsgi

.. class:: PyjionWsgiMiddleware(application)

   Provides a WSGI middleware interface that enables the JIT for requests
