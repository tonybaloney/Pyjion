.. _OPT-3:

OPT-3 Optimize == and != comparisons for short-integers to pointer comparisons
==============================================================================


Background
----------


Gains
-----

Edge-cases
----------

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