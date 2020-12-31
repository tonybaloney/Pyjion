.. _OPT-6:

OPT-6 Shortcut STORE_SUBSCR for known types
===========================================

Background
----------

CPython compiles a single opcode for expressions that assign an object to a mapping type (e.g. dict) or a sequence type (e.g. list).

.. code-block:: python

    l = [0, 1, 2, 3]
    l[2] = 3
    print(l)

This function compiles to the following opcodes:

.. code-block::

  2           0 BUILD_LIST               0
              2 LOAD_CONST               1 ((0, 1, 2, 3))
              4 LIST_EXTEND              1
              6 STORE_FAST               0 (l)

  3           8 LOAD_CONST               2 (3)
             10 LOAD_FAST                0 (l)
             12 LOAD_CONST               3 (2)
             14 STORE_SUBSCR

  4          16 LOAD_GLOBAL              0 (print)
             18 LOAD_FAST                0 (l)
             20 CALL_FUNCTION            1
             22 POP_TOP
             24 LOAD_CONST               0 (None)
             26 RETURN_VALUE

This opcode takes 3 values from the stack: Index(``2``), Container (``l``), Value(``3``).

The two most commonly used types for ``STORE_SUBSCR`` are ``dict`` and ``list``. The ``PyObject_SetItem`` function is optimized toward dict first, then list. It checks these two 'base' types:

For a mapping type:

 1. Determine it is a mapping type
 2. Check the type for a type slot for item assignment
 3. Call the type slots (e.g. ``PyDict_SetItem``)

For sequence type, the index needs to first be converted into a native integer. Even frame constants are stored as ``PyLongObject``, even if the value is small (e.g. 10).

 1. Determine it is a sequence type
 2. Convert the index value from a ``PyLongObject`` to an ``int``
 3. Check the range of the index with the maximum range of the list
 4. Check the type slot for item assignment
 5. Call the type slot (e.g. ``PyList_SetItem``)

The third scenario would be the implementation of a custom class, and the mapping or sequence protocol.

Because the type of the container can be determined at runtime, the ``PyObject_SetItem`` function is slow, but generic.

Solution
--------

This optimization will determine absolutely typed variables using the preprocess stage (e.g. frame constants, outputs from builtin functions like ``list()``, or return types from opcodes like ``BUILD_MAP``).

At compile time, it will optimize some common paths:

1. If the container type is a dict, emit a call to ``PyDict_SetItem`` inplace of ``PyObject_SetItem``
2. If the container type is a list,
    a. If the index is a frame constant ``PyLongObject`` within 4 digits, convert it to a native integer at runtime and emit a call to ``PyList_SetItem``
    b. If the index is a dynamic value, convert at runtime and call ``PyList_SetItem`` without the overhead of ``PyObject_SetItem``
3. If the container type cannot be determined (because it was set at runtime), but the index is an frame constant integer, call a custom ``PyObject_SetItem``, where the index value is already converted, shortcutting the conversion step

Gains
-----

- List item assignments are faster (unless the list is an argument to the function, in which case the type is unknown)
- Sequence item assignments are faster (as above)
- Dictionary item assignments are faster (as above)
- Item assignments to sequence types where the index is const value between -5 and 256 are much faster as they bypass multiple tests and conversions

Edge-cases
----------

None. Even if there is a fault in determining the type of the container, it will be checked at runtime using ``PyDict_Check``, etc., and then redirect back to ``PyObject_SetItem`` if it fails.

Configuration
-------------

This optimization is enabled at **level 1** by default. See :ref:`Optimizations <optimizations>` for help on changing runtime optimization settings.

+------------------------------+--------------------------------------+
| Compile-time flag            |  ``OPTIMIZE_KNOWN_STORE_SUBSCR=OFF`` |
+------------------------------+--------------------------------------+
| Default optimization level   |  ``1``                               |
+------------------------------+--------------------------------------+
