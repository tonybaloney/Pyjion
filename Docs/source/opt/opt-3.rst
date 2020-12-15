.. _OPT-3:

OPT-3 Optimize == and != comparisons for short-integers to pointer comparisons
==============================================================================


Background
----------

Because of the frequency of use within an application, CPython generates the int objects for the integers -5 to 256 at startup and caches them in a property of the interpreter
states (``small_ints``).
When creating an integer from either a const value in a function, as a property of a class, or via the C-API, the integer will be checked if it is in the range within ``small_ints`` using a macro ``IS_SMALL_INT``.
CPython uses this to improve performance when creating new integers from a C long (or ``long long``) and shortcuts the value to the short_ints reference.

There are also performance gains within CPython to this optimization when comparing numbers. For example,

.. code-block:: Python

   def is_three(x):
      return x == 3

This function will generate the bytecode sequence ``LOAD_FAST``, ``LOAD_CONST``, ``COMPARE_OP``, ``RETURN``.

For ``COMPARE_OP``, the CPython evaluation loop will call ``PyObject_RichCompare()``, which calls the object's ``tp_richcompare`` type slot method. For a long object, this is:

.. code-block:: c

    static PyObject *
    long_richcompare(PyObject *self, PyObject *other, int op)
    {
        Py_ssize_t result;
        CHECK_BINOP(self, other);
        if (self == other)
            result = 0;
        else
            result = long_compare((PyLongObject*)self, (PyLongObject*)other);
        Py_RETURN_RICHCOMPARE(result, 0, op);
    }

CPython is already reasonably efficient at detecting equal numbers as it will check ``if (self == other)`` pointer equivalence. However, this is still relatively slow:

1. It consumes 3 call stack levels to get to this short comparison
2. It is a common occurrence to compare short integers
3. It still requires a comparison of the operator at runtime (``Py_EQ``/``Py_NE``)

Solution
--------

The Pyjion compiler has 3 main stages:

1. Build a "stack table" of the abstract types at each opcode position
2. Compile CPython opcodes into CIL opcodes
3. Emit the CIL opcodes into the .NET EE compiler to convert to native machine code/assembly

This optimization is split into two components.

a. At the preprocess stage, detect const values which are small integers and mark them as ``InternInteger`` (a subclass of ``Integer``)
b. At the compile stage, detect the comparison of an ``InternInteger`` with an ``Integer`` (``AVK_Integer``)

If this comparison is detected, the compiler will emit the equivalent operation of ``x is y``. This requires that:

1. The left or right operand is definitely an interned integer (likely a const value)
2. The other operand is a absolutely an integer

Gains
-----

- Comparison of small integers would be significantly faster.
- Adds no overhead/branching to scenarios that do not match this optimization

Potential Improvements
----------------------

To build on this assertion, if the type of the opposite operand to the interned integer could be quickly determined to by a ``PyLongObject``, the same optimization would work.
This would allow for runtime values and hit a far broader set of scenarios.

Edge-cases
----------

If the user compiled a custom CPython runtime with a smaller range of ``NSMALLINTS``, this would fire results when comparing integers.
To circumvent this, Pyjion could inspect the interpreter state and cache the size of ``small_ints``. However this scenario is very, very unlikely (but anything is possible).

Configuration
-------------

This optimization is enabled at **level 1** by default. See :ref:`Optimizations <optimizations>` for help on changing runtime optimization settings.

+------------------------------+-------------------------------+
| Compile-time flag            |  ``OPTIMIZE_INTERN=OFF``      |
+------------------------------+-------------------------------+
| Default optimization level   |  ``1``                        |
+------------------------------+-------------------------------+


Examples
--------

The following test cases document what should and should-not be optimized.

.. code-block:: Python

    def test_const_compare(self):
        def test_f():
            a = 1
            b = 2
            return a == b
        self.assertOptimized(test_f)


    def test_const_compare_big_left(self):
        def test_f():
            a = 1000
            b = 2
            return a == b

        self.assertOptimized(test_f)

    def test_const_compare_big_right(self):
        def test_f():
            a = 1
            b = 2000
            return a == b

        self.assertOptimized(test_f)

    def test_const_compare_big_both(self):
        def test_f():
            a = 1000
            b = 2000
            return a == b

        self.assertNotOptimized(test_f)

    def test_const_not_integer(self):
        def test_f():
            a = 2
            b = "2"
            return a == b

        self.assertNotOptimized(test_f)

    def test_float_compare(self):
        def test_f():
            a = 2
            b = 1.0
            return a == b

        self.assertNotOptimized(test_f)