.. _OPT-2:

OPT-2 Optimize "is" operations to pointer comparisons
=====================================================

Background
----------

Python has two mechanisms for equality, the ``is`` keyword, and rich comparisons (``==``, ``!=``, etc.).

For the ``is`` or ``is not`` keyword, the ``IS_OP`` opcode is emitted by the CPython compiler.

For this statement:

.. code-block:: Python

   a is b

This should have the following effect:

1. Compare the pointers to objects ``a`` and ``b``
2. Decrement the references of ``a`` and ``b``
3. If the pointers are equal return a pointer to ``Py_True`` and increment the reference count to ``Py_True``.
4. If the pointers are not equal return a pointer ``Py_False`` and increment the reference count to ``Py_False``.
5. If this is ``is not`` reverse the logic of 3 and 4

Solution
--------

This optimization will emit CIL opcodes (and native machine code) to the equivalent of:

- Load the left-hand pointer from the evaluation stack
- Load the right-hand pointer from the evaluation stack
- Compare the pointer addresses (qword comp)
- Branch to return true if match by returning a cached address of ``Py_True``, or
- Branch to return false if not matched by returning a cached address of ``Py_False``
- Decrement both the left-hand and right-hand objects

Without :ref:`OPT-1`, this optimization gains are minimal because it would call a global method to decrement left-hand and right-hand objects.

Gains
-----

- Use of ``IS_OP`` via ``is`` and ``is not`` keywords is very efficient (more so than CPython)

Edge-cases
----------

None.

Configuration
-------------

This optimization is enabled at **level 1** by default. See :ref:`Optimizations <optimizations>` for help on changing runtime optimization settings.

+------------------------------+-------------------------------+
| Compile-time flag            |  ``OPTIMIZE_IS=OFF``          |
+------------------------------+-------------------------------+
| Default optimization level   |  ``1``                        |
+------------------------------+-------------------------------+
