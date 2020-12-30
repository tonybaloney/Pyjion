.. _OPT-5:

OPT-5 Inline frame push/pop instructions
========================================

Background
----------

Upon entry to a frame, the thread state is updated with a pointer to the frame. Before a frame exits, this pointer is reassigned to the previous frame.

All frames are linked by a singly-linked-list via the ``f_back`` field in the ``PyFrameObject`` type.

Pyjion will, by default, call a method on entry and exit to get the thread state and update these values.
This carries overhead for a method call and ABI overheads.

Solution
--------

This optimization will calculate the offset of the thread state (the third argument to the generated JIT function),
the offset of the frame and assign it to the current frame (the second argument).
It will also "pop" the frame by calculating the offset of ``f_back`` of the frame upon entry and restoring it to the thread state.

Frame entry/exit become inline CIL operations.

Gains
-----

- frame entry/exit is slightly faster

Edge-cases
----------

None.

Configuration
-------------

This optimization is enabled at **level 1** by default. See :ref:`Optimizations <optimizations>` for help on changing runtime optimization settings.

+------------------------------+---------------------------------+
| Compile-time flag            |  ``OPTIMIZE_PUSH_FRAME=OFF``    |
+------------------------------+---------------------------------+
| Default optimization level   |  ``1``                          |
+------------------------------+---------------------------------+
