.. _OPT-12:

OPT-12 Pre-load methods for builtin types and bypass LOAD_METHOD
================================================================

Background
----------

CPython splits method calls into two opcode, ``LOAD_METHOD`` and ``CALL_METHOD``. Method descriptors are handled as objects passed onto the value stack.

For ``LOAD_METHOD`` CPython will take two arguments, object and name then locate which method is to be called using the ``_PyObject_LoadMethod`` function. This function will use
the slots API as well as the method-resolution-order algorithm for subclasses.

This example will load the method ``append()`` for the object ``n``:

.. code-block:: python

    n = []
    while len(n) < 100:
       n.append(12)

It will load the method on every iteration of the ``while`` loop even though it hasn't changed.

Solution
--------

For builtin types, where the abstract type is absolute (like this example, where ``n`` has to be a ``list``, Pyjion will create a method descriptor at compile-time and
emit the address of the method descriptor instead of calling ``_PyObject_LoadMethod``. Therefore, for every iteration, it uses the same method descriptor (which contains the address of the CFunction).

Gains
-----

- Method calls on determinate methods of builtin types are faster

Edge-cases
----------

If a method is mocked or patched on a call in the same opcode position (e.g a loop) it will call the original method.

Further Enhancements
--------------------

The ``CALL_METHOD`` opcode could be bundled and further optimized to either call the method location directly (like PEP590 but faster).

Configuration
-------------

This optimization is enabled at **level 1** by default. See :ref:`Optimizations <optimizations>` for help on changing runtime optimization settings.

+------------------------------+---------------------------------------+
| Compile-time flag            |  ``OPTIMIZE_BUILTIN_METHOD=OFF``      |
+------------------------------+---------------------------------------+
| Default optimization level   |  ``1``                                |
+------------------------------+---------------------------------------+
