Pyjion Python API
=================

.. module:: pyjion

.. function:: enable()

   Enable the JIT

.. function:: disable()

   Disable the JIT

.. function:: set_threshold(to)

   Set the threshold to JIT compile a function to the number of times it is executed.

.. function:: dump_il(f)

   Return the ECMA CIL bytecode as a bytearray

.. function:: dump_native(f)

   Return the compiled machine-code as a bytearray

.. module:: pyjion.dis

.. function:: dis(f)

   Print the ECMA CIL bytecode in a disassembly table

.. function:: dump_native(f)

   Print the x86 assembly instructions in a disassembly table (required distorm3)