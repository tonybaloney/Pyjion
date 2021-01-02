.. _OPT-7:

OPT-7 Shortcut BINARY_SUBSCR for known types and indexes
========================================================

Background
----------

CPython compiles a single opcode for expressions that read an object at an index (e.g. dict, list, tuple).

.. code-block:: python

    l = [0, 1, 2, 3]
    print(l[2])

This function compiles to the following opcodes:

.. code-block::

  2           0 LOAD_CONST               1 (0)
              2 LOAD_CONST               2 (1)
              4 LOAD_CONST               3 (2)
              6 LOAD_CONST               4 (3)
              8 BUILD_LIST               4
             10 STORE_FAST               0 (l)

  3          12 LOAD_GLOBAL              0 (print)
             14 LOAD_FAST                0 (l)
             16 LOAD_CONST               3 (2)
             18 BINARY_SUBSCR
             20 CALL_FUNCTION            1
             22 POP_TOP
             24 LOAD_CONST               0 (None)
             26 RETURN_VALUE

The ``BINARY_SUBSCR`` opcode takes 2 values from the stack: Index(``2``), Container (``l``).

The three most commonly used types for ``BINARY_SUBSCR`` are ``dict``, ``list`` and ``tuple``. The ``PyObject_GetItem`` function is optimized toward dict first, then sequence types. It checks these two 'base' types:

For a mapping type:

 1. Determine it is a mapping type
 2. Check the type for a type slot for item assignment
 3. Call the type slots (e.g. ``PyDict_GetItem``)

For sequence type, the index needs to first be converted into a native integer. Even frame constants are stored as ``PyLongObject``, even if the value is small (e.g. 10).

 1. Determine it is a sequence type
 2. Convert the index value from a ``PyLongObject`` to an ``int``
 3. Check the range of the index with the maximum range of the list
 4. Check the type slot for item assignment
 5. Call the type slot (e.g. ``PyList_GetItem``), via another function ``sq_subscr_item``

The third scenario would be the implementation of a custom class, and the mapping or sequence protocol.

Because the type of the container can be determined at runtime, the ``PyObject_GetItem`` function is slow, but generic.

Solution
--------

This optimization will determine absolutely typed variables using the preprocess stage (e.g. frame constants, outputs from builtin functions like ``list()``, or return types from opcodes like ``BUILD_MAP``).

At compile time, it will optimize some common paths:

1. If the container type is a dict:
   a. If the index is a const value, compute the hash at compile time and emit a call to ``PyDict_GetItemKnownHash``
   b. If the index is a dynamic value, emit a call to ``PyDict_GetItem``
2. If the container type is a list,
    a. If the index is a frame constant ``PyLongObject`` within the range of ``Py_ssize_t``, convert it to a native 64-bit integer at runtime and emit a call to ``PyList_GetItem()`` using the ``CEE_LDC_I8`` opcode for 64-bit constant integers
    b. If the index is a dynamic value, convert at runtime and call ``PyList_GetItem`` without the overhead of ``PyObject_GetItem``
3. If the container type is a tuple, use the same process as above but, for
4. If the index is a constant value and the type cannot be determined at compile time:
    a. If the index is a number, emit a call to try it as a direct index (``PySequence_GetItem``) with a precomputer integer constant
    b. If the index is a const value, e.g. a string, compute the hash in advance and assume the item is a dict, if not, redirect to ``PyObject_GetItem``
    c. If the index is both a hashable value and an integer, emit both the hash and index value. Try the dict first, then try the sequence type. Skipping both the hash compute and the numeric conversion
    d. If the index is a negative number, redirect to ``PySequence_GetItem``, which supports reverse indexing
3. If the container type and the index type cannot be determined, emit a regular call to ``PyObject_GetItem``

Gains
-----

- List item reads are faster when the index is a constant value
- Sequence item reads are faster (as above)
- Dictionary item reads are faster when the index is a frame constant

Edge-cases
----------

None. Even if there is a fault in determining the type of the container, it will be checked at runtime using ``PyDict_Check``, etc., and then redirect back to ``PyObject_GetItem`` if it fails.

Configuration
-------------

This optimization is enabled at **level 1** by default. See :ref:`Optimizations <optimizations>` for help on changing runtime optimization settings.

+------------------------------+---------------------------------------+
| Compile-time flag            |  ``OPTIMIZE_KNOWN_BINARY_SUBSCR=OFF`` |
+------------------------------+---------------------------------------+
| Default optimization level   |  ``1``                                |
+------------------------------+---------------------------------------+
