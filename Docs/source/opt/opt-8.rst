.. _OPT-8:

OPT-8 Combine sequential binary operators into a single function
================================================================

Background
----------

CPython will compile complex mathematical operations into opcodes based on operator precedence and stack.
All binary operations and inplace operations (Add, subtract, divide, floor divide, power), take 2 arguments, the left and right hand objects.

E.g.

.. code-block:: Python

   c = a + b

In this example, the value of a and b is loaded (from LOAD_FAST, LOAD_NAME, or LOAD_GLOBAL depending on the scoping) and the reference count is incremented.
The binary add operation then calculates a new value and decrements the reference to ``a`` and ``b``.

This new number becomes the value in ``c``.

Some mathematical operations create a new value, but the object is immediately discarded. E.g. inplace operations which are the function of two other objects.

This test demonstrates the behaviour:

.. code-block:: python

    def test_mixed():
        a = 2.0
        b = 3.0
        c = 4
        c += a * b

This produces the following opcode sequence:

.. code-block::

      2           0 LOAD_CONST               1 (2.0)
                  2 STORE_FAST               0 (a)

      3           4 LOAD_CONST               2 (3.0)
                  6 STORE_FAST               1 (b)

      4           8 LOAD_CONST               3 (4)
                 10 STORE_FAST               2 (c)

      5          12 LOAD_FAST                2 (c)
                 14 LOAD_FAST                0 (a)
                 16 LOAD_FAST                1 (b)
                 18 BINARY_MULTIPLY
                 20 INPLACE_ADD
                 22 STORE_FAST               2 (c)
                 24 LOAD_CONST               0 (None)
                 26 RETURN_VALUE

The inefficiency here is the 18th and 20th position opcodes (``BINARY_MULTIPLY`` and ``INPLACE_ADD``).

``BINARY_MULTIPLY`` will:

 1. Convert ``a`` to a double
 2. Convert ``b`` to a double
 3. Multiply the result
 4. Create a new (unnamed) ``PyFloatObject`` with the resulting double value and reference count of 1

The ``INPLACE_ADD`` will:

 5. Convert ``c`` to a double
 6. Load the value of the unnamed value and convert to a double
 7. Calculate the multiple of ``c`` and the unnamed value
 8. Assign that value to ``c``
 9. Decrement the reference to the unnamed value and deallocate the object

If binary operations are immediately followed by another binary operations, it can be assumed that the result is a temporary value.
Therefore, half of those steps are unnecessary.

Solution
--------

Replace any two sequential calls to a binary and then another binary (or inplace) opcode with a single function call.

A set of specialized math functions have been created to keep the temporary value for ``PyFloat`` as a double and not create or allocate a PyObject.

For binary operations involving two ``PyLong`` where the operator is a true division, because the result is a float (and therefore the result of the second operation is a float even if the value is a long) they will also be optimized.

Gains
-----

- Inplace operations for int, floats and strings where the assignment is the result of another binary operation are about 30% faster
- Nested operations for floats are 30-40% faster
- Divisions of ints and subsequent operations (returning a float) are about 30% faster

Edge-cases
----------

After testing, converting PyLong to a ``long long`` and doing native operations for add, subtract, divide, etc. is slower than using the native operations. This is due to the overhead of
Also, the native add, subtract, mul functions are relatively efficient.

For that reason, PyNumber_XXX is used instead

Configuration
-------------

This optimization is enabled at **level 1** by default. See :ref:`Optimizations <optimizations>` for help on changing runtime optimization settings.

+------------------------------+---------------------------------------+
| Compile-time flag            |  ``OPTIMIZE_BINARY_FUNCTIONS=OFF``    |
+------------------------------+---------------------------------------+
| Default optimization level   |  ``1``                                |
+------------------------------+---------------------------------------+
