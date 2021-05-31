.. _OPT-15:

OPT-15 Optimize the LOAD_ATTR opcode
====================================

Background
----------

Python has a generic opcode for loading attributes of names (objects, variables, etc). This is NOT the same as LOAD_METHOD, which is a
specialized opcode for loading method attributes.

``PyObject_LoadAttr()`` is the C-API interface for loading attributes.

This will inspect the object to see if (in order of preference):

* It has a type slot ``tp_getattr``
* It has a type slot ``tp_getattro``

For type objects, it would call the Python function for ``__getattr__``.

Solution
--------

At compile-time, Pyjion will use type inference (including PGC stacks) to inline the call to the correct type-slot method using
method slot symbols. (see CALL_FUNCTION optimizations).

This is done by resolving the address of the typeslot call at compile-time and compiling a trampoline pointer to that method in the IL.

Gains
-----

* Attribute loading is faster for native types

Edge-cases
----------

* None?

Further Enhancements
--------------------

* Process can be applied to any function. Expand this through the use of profiling

Configuration
-------------

This optimization is enabled at **level 1** by default. See :ref:`Optimizations <optimizations>` for help on changing runtime optimization settings.

+------------------------------+---------------------------------------+
| Compile-time flag            |  ``OPTIMIZE_LOAD_ATTR=OFF``           |
+------------------------------+---------------------------------------+
| Default optimization level   |  ``1``                                |
+------------------------------+---------------------------------------+
