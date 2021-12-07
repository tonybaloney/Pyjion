.. _API:

API Reference
=============

Command-line
------------

You can run ``pyjion`` as a standalone command-line program, similar to how you would use ``python`` to run scripts and modules.

.. code-block::

   usage: pyjion [-m MODULE] | [script]

   Python JIT Compiler

   positional arguments:
   script                script file

   optional arguments:
   -m MODULE             Execute module

You can enable PGC by setting the ``PYJION_PGC`` environment variable to 1.
You can override the optimization level (default 1) by setting the ``PYJION_LEVEL`` environment variable.

Core module
-----------

.. module:: pyjion

.. function:: enable()

   Enable the JIT

.. function:: disable()

   Disable the JIT

.. function:: config(pgc: Optional[bool], level: Optional[int], debug: Optional[bool], graph: Optional[bool], threshold: Optional[int], ) -> Dict[str, Any]:

   Get the configuration of Pyjion and change any of the settings.

.. function:: il(f)

   Return the ECMA CIL bytecode as a bytearray

.. function:: native(f)

   Return the compiled machine-code as a bytearray

.. function:: offsets(f: Callable) -> tuple[tuple[int, int, int]]:
   
   Get sequence points for a compiled function (used by the :ref:`Disassembler`)

.. function::  graph(f: Callable) -> str:
   
   Get graph for compiled function, see :ref:`Graphing`

.. function::  symbols(f: Callable) -> Dict[int, str]:

   Get method symbol table (used by the :ref:`Disassembler`).

Disassembly module
------------------

.. module:: pyjion.dis

.. function:: dis(f, include_offsets=False, print_pc=True)

   Print the ECMA CIL bytecode in a disassembly table.
   Set ``include_offsets=True`` to print the Python opcodes inline with the IL.

.. function:: dis_native(f, include_offsets=False, print_pc=True)

   Print the x86_64 assembly instructions in a disassembly table (requires distorm3 and rich)
   Set ``include_offsets=True`` to print the Python opcodes inline with the assembly.

WSGI middleware
---------------

.. module:: pyjion.wsgi

.. class:: PyjionWsgiMiddleware(application)

   Provides a WSGI middleware interface that enables the JIT for requests