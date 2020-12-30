.. _OPT-4:

OPT-4 Optimize frame locals as native EE/CIL locals
===================================================

Background
----------

Python has 3 opcodes for handling frame locals. Frame locals are local variables which are known on compilation and defined within the scope of a function.
Frame's are constructed with this list of frame locals and they are stored, loaded and deleted using separate opcodes to other scopes (e.g. globals, name).

For example:

.. code-block:: Python

   >>> def f():
   ...   a=1
   ...   return a == c
   ...
   >>> import dis
   >>> dis.dis(f)
     2           0 LOAD_CONST               1 (1)
                 2 STORE_FAST               0 (a)

     3           4 LOAD_FAST                0 (a)
                 6 LOAD_GLOBAL              0 (c)
                 8 COMPARE_OP               2 (==)
                10 RETURN_VALUE

CPython knows when compiling this function that ``a`` and ``b`` are defined in the scope of the function and reserve's 2 ``PyObject*`` slots in the frame locals.

The ``LOAD_FAST(i)`` opcode will load the frame local in ``frame->f_localsplus[i]``, increment the reference and push the pointer on the value stack.
The value will be checked to see if it is null, in which case an ``UnboundLocalError`` is raised.

For Pyjion, the behaviour is mimicked (without this optimization) to calculate the offset of ``frame->f_localsplus[i]`` and push the pointer on the EE evaluation stack.

This is a relatively simple calculation,

.. code-block:: cpp

    load_frame();
    m_il.ld_i(offsetof(PyFrameObject, f_localsplus) + oparg * sizeof(size_t));
    m_il.add();
    m_il.ld_ind_i();

However, this is inefficient for a piece of code that refers to lots of frame locals (pretty much any useful code uses many variables).

Solution
--------

Instead, this optimization will allocate a native ECMA CIL local and use the ``ldloc.x`` opcode for ``LOAD_FAST`` and store a hashmap of which local indexes
relate to which frame locals.

.. code-block::

    ld.i
    add
    ldind.i

Becomes

.. code-block::

    ldloc.s

Likewise, storing variables is far more efficient.

Gains
-----

- loading and storing frame locals is 3x faster
- optimizations can be done by the .NET compiler

Potential Improvements
----------------------

The unbound local check (a zero comparison of a 64-bit value) could be skipped for certain values. It is not in the CPython ceval loop for these scopes, but could be done in Pyjion.

Edge-cases
----------

None.

Configuration
-------------

This optimization is enabled at **level 1** by default. See :ref:`Optimizations <optimizations>` for help on changing runtime optimization settings.

+------------------------------+---------------------------------+
| Compile-time flag            |  ``OPTIMIZE_NATIVE_LOCALS=OFF`` |
+------------------------------+---------------------------------+
| Default optimization level   |  ``1``                          |
+------------------------------+---------------------------------+
