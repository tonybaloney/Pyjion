.. _OPT-9:

OPT-9 Inline list iterators into assembly instructions
======================================================

Background
----------

Python objects can implement the iterator protocol.

This protocol is used in a few places:

 - A for loop
 - The ``iter()`` builtin
 - While loops

The iterator protocol specifies that iterator objects should be generated for a container object, like a list, tuple, or dict.

You can implement custom iterators for classes by implementing the ``__iter__`` method.

A Python for loop will get the iterator of the object using the ``GET_ITER`` opcode and then keep an iterator object on the value stack for each
cycle in a loop.

The next value in the iterator object is yielded by the ``FOR_ITER`` opcode. Most iterator objects just store an index and a pointer to the container. When ``iter_next()`` is called
for any iterator object, the index is incremented and a pointer to the next object in the container is returned.

Solution
--------

A new source type has been introduced, ``IteratorSource``, which is generated at the process stage for compilation. This is generated for
the ``GET_ITER`` opcode to mark the type of iterator object that will be pushed the stack on execution of the frame. This will be determined for known types.

At the compile stage, if the abstract value kind at the top of the stack for ``FOR_ITER`` is an ``IterableValue``, then the abstract kind for the ``IteratorSource`` is inspected.

If it is an optimized type (only List for this optimization), then the IL is compiled into the function to get the next item in a listiterator, increment the index and increment the reference count
of the next item.
These inline IL codes are modelled on the ``listiter_next`` function in CPython.

Gains
-----

- Loops for lists are faster, IF the container type can be determined as a list at compile-time

Edge-cases
----------

None.

Potential Improvements
----------------------

- The branch logic could be improved. .NET doesn't optimize the compiled assembly
- Instead of copying ``iterxx_next``, a more efficient model could be used, e.g. stacking items through onto a queue and just popping them for each iteration

Configuration
-------------

This optimization is enabled at **level 1** by default. See :ref:`Optimizations <optimizations>` for help on changing runtime optimization settings.

+------------------------------+---------------------------------------+
| Compile-time flag            |  ``OPTIMIZE_ITERATORS=OFF``           |
+------------------------------+---------------------------------------+
| Default optimization level   |  ``1``                                |
+------------------------------+---------------------------------------+
