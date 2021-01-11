.. _OPT-8:

OPT-8 Combine sequential binary operators into a single function
================================================================

Background
----------



Solution
--------



Gains
-----

- Inplace operations for int, floats and strings where the assignment is the result of another binary operation are about 30% faster
- Nested operations for ints and floats are 30-40% faster

Edge-cases
----------

None.

Configuration
-------------

This optimization is enabled at **level 1** by default. See :ref:`Optimizations <optimizations>` for help on changing runtime optimization settings.

+------------------------------+---------------------------------------+
| Compile-time flag            |  ``OPTIMIZE_BINARY_FUNCTIONS=OFF``    |
+------------------------------+---------------------------------------+
| Default optimization level   |  ``1``                                |
+------------------------------+---------------------------------------+
