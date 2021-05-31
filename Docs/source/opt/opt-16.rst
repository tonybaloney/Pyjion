.. _OPT-16:

OPT-16 Optimize floating point arithmetic by unboxing float values
==================================================================

Background
----------

Work was done in early versions of Pyjion to box/unbox Python integers and floats into native long/float types. This allows for native CIL opcodes like ADD, SUB, IMUL, etc. to be used
on native integers instead of having to use the complex (and slow) ``PyLongObject``/``PyFloatObject`` operations.
I've found this initial prototype to be unstable so the boxing and unboxing has been completely rewritten.

Pyjion's escape analysis is acheived using an instruction graph to traverse supported unboxed types and opcodes then to tag transition stack variables as unboxed.
This functionality complements PGC.

Solution
--------

Take this code sample:

.. code-block:: Python

    def f():
      text = [0.1,1.32,2.4,3.55,4.5]
      n = 2.00
      text[0] += 2. ** n
      return text

The first compilation of this method doesn't know what type ``text[0]`` is, but with PGC it will observe the type and recompile into a more efficient tree by using an unboxed ``ADD`` opcode.

.. image:: ../_static/inplace_subscr.png

The n-body benchmark has an extreme version of this:

.. code-block:: Python

    mag = dt * ((dx * dx + dy * dy + dz * dz) ** (-1.5))

The operations will form a graph of instructions, for which Pyjion will mark the edges as escaped. All of the binary operations in the escaped types can use native assembly
to add and multiply floating point values.
Also, the values are not kept within Python objects, so the code is faster to execute as it doesn't create temporary objects between opcodes.

.. image:: ../_static/complex_math.png


Gains
-----

* Floating point arithmetic is significantly faster (2-10x)

Edge-cases
----------

* Pyjion will throw a ``ValueError`` saying that the PGC guard failed when an assertion was made from type data and the type changed at runtime.

Further Enhancements
--------------------

* Instead of raising a runtime exception, Pyjion could compile a parallel call graph for which to execute the "generic" sequence of instructions.
* Support other types

Configuration
-------------

This optimization is enabled at **level 1** by default. See :ref:`Optimizations <optimizations>` for help on changing runtime optimization settings.

+------------------------------+---------------------------------------+
| Compile-time flag            |  ``OPTIMIZE_UNBOXING=OFF``            |
+------------------------------+---------------------------------------+
| Default optimization level   |  ``1``                                |
+------------------------------+---------------------------------------+
