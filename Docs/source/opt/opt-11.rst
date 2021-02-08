.. _OPT-11:

OPT-11 Slicing binary subscript for list types is reduced to a single call
==========================================================================

Background
----------

CPython has a special type for slicing lists, tuples or any other container type. Slices can also be used for objects implementing the `__index__()` method.



.. code-block:: python

    l = [0,1,2,3]
    l[2:3]

This function compiles to the following opcodes:

.. code-block::

  2           0 LOAD_CONST               1 (0)
              2 LOAD_CONST               2 (1)
              4 LOAD_CONST               3 (2)
              6 LOAD_CONST               4 (3)
              8 BUILD_LIST               4
             10 STORE_FAST               0 (l)

  3          12 LOAD_FAST                0 (l)
             14 LOAD_CONST               3 (2)
             16 LOAD_CONST               4 (3)
             18 BUILD_SLICE              2
             20 BINARY_SUBSCR


The ``BINARY_SUBSCR`` opcode takes 2 values from the stack: Index and Container (``l``).
``BUILD_SLICE`` takes either 2 or 3 values depending on whether it was using ``[start:stop]`` or ``[start:stop:step]``.

Omitting either start, stop or step will trigger the compiler to emit an implicit ``None`` value in place of the blank entry.

The CPython compiler would, for the 3rd line:

- Emit the ``l`` list to the value stack
- Emit the const variable ``2`` to the stack
- Emit the const vairable ``3`` to the stack
- Call ``PySlice_New()`` by popping the first and second values (2 and 3) and setting ``step`` to ``None``
- Call ``PyObject_GetItem`` with the container (``l``) and the new slice object
- Decrement the slice object, which had a reference count of 1, which triggers it to be deallocated

Once ``PyObject_GetItem`` is called, because the type is a list, it will (eventually, through layers of abstraction), call ``PyList_GetSlice()``.

Solution
--------

These two operations (``BUILD_SLICE`` and ``BINARY_SUBSCR``) can be combined into a single, simple operation under certain circumstances:

- When the start, stop or step values are constant integers (hardcoded numbers) and/or None values
- The container type is definitely a list

In either case, the numeric value of ``start``, ``stop`` is omitted and the const value loaded from ``LOAD_CONST`` is decref'ed

Therefore, the following optimizations can be made:

- ``x[0:2]`` would call ``PyList_GetSlice(x, 0, 2)``
- ``x[:2]`` would call ``PyList_GetSlice(x, 0, 2)``

In the case where the ``None`` value is omitted, ``PY_SSIZE_T_MIN`` is used as a special value to indicate the minimum index and ``PY_SSIZE_T_MAX`` as the upper bound. e.g.,

- ``x[0:]`` would call ``PyList_GetSlice(x, 0, len(x))``

Negative numbers are calculated to the correct positions using the standard CPython API, but only when required.

For slices including a step, a specialized function is used.

A special method has been written for the ``[::-1]]`` slice, as this occurs so frequently. It just reverses the list into a new list using a preallocated list generator.

In those cases, the number of operations removed by shortcutting between the slice and the subscript operation is massive.

Gains
-----

- Reverse slices [::-1] are faster
- Straight slices for positive literals are faster [1:2]
- Negative slices are faster

Edge-cases
----------

None.

Further Enhancements
--------------------

This could be expanded to tuples and sets. Lists were picked because of how frequently they are sliced.

Configuration
-------------

This optimization is enabled at **level 1** by default. See :ref:`Optimizations <optimizations>` for help on changing runtime optimization settings.

+------------------------------+---------------------------------------+
| Compile-time flag            |  ``OPTIMIZE_BINARY_SLICE=OFF``        |
+------------------------------+---------------------------------------+
| Default optimization level   |  ``1``                                |
+------------------------------+---------------------------------------+
