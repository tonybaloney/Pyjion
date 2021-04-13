.. _OPT-13:

OPT-13 Pre-load functions for binary operations to known types
==============================================================

Background
----------

CPython will compile complex mathematical operations into opcodes based on operator precedence and stack.
All binary operations and inplace operations (Add, subtract, divide, floor divide, power), take 2 arguments, the left and right hand objects.

E.g.

.. code-block:: Python

   c = a + b

In this example, the value of a and b is loaded (from LOAD_FAST, LOAD_NAME, or LOAD_GLOBAL depending on the scoping) and the reference count is incremented.
The binary add operation then calculates a new value and decrements the reference to ``a`` and ``b``.

The API for working out the correct magic-method to call is in CPython's abstract API.

For example, the ``+`` operator will call ``PyNumber_Add(a, b)``.

This will resolve to:

1. IF ``b`` is a subtype of ``a`` and it implements that slot, use that
2. If ``a`` has that slot implemented, use that
3. If ``b`` has that slot implemented, use that

For each of these, the call can return a singleton string, ``Py_NotImplemented``.

There is also another branch for inplace operators, where if the inplace slot is not implemented, it will fall back to the regular slot.

Solution
--------

If the left-hand and right-hand operands are both known types at compile-time, Pyjion will work out the logic of which slot has preference at compile time and emit a method call to the implementation of that slot.

Gains
-----

* Add binary operations where left and right are known types are faster.

Edge-cases
----------

* None

Further Enhancements
--------------------

* Determine more types through profiling.

Configuration
-------------

This optimization is enabled at **level 1** by default. See :ref:`Optimizations <optimizations>` for help on changing runtime optimization settings.

+------------------------------+---------------------------------------+
| Compile-time flag            |  ``OPTIMIZE_TYPESLOT_LOOKUPS=OFF``    |
+------------------------------+---------------------------------------+
| Default optimization level   |  ``1``                                |
+------------------------------+---------------------------------------+
