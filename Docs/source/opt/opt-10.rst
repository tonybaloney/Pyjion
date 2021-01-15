.. _OPT-10:

OPT-10 Precompute the hashes for LOAD_NAME and LOAD_GLOBAL dictionary lookups
=============================================================================

Background
----------

Python has two opcodes for variables which are not defined in the scope of the local function/frame, ``LOAD_GLOBAL`` and ``LOAD_NAME``.

Both opcodes will do a lookup of the variable name. The frame globals, or frame locals attributes are normally dictionaries, so the name will be hashed _every_ time it's called.

Solution
--------

The hashes for variable names are precomputed and emitted along with the opcode calls, a shortcut function is used ``_PyDict_GetItem_KnownHash`` to bypass the calculation of the hash each time.

Gains
-----

- Calling builtins is faster
- Reading out of scope variables is faster

Edge-cases
----------

None.

Configuration
-------------

This optimization is enabled at **level 1** by default. See :ref:`Optimizations <optimizations>` for help on changing runtime optimization settings.

+------------------------------+---------------------------------------+
| Compile-time flag            |  ``OPTIMIZE_HASHED_NAMES=OFF``        |
+------------------------------+---------------------------------------+
| Default optimization level   |  ``1``                                |
+------------------------------+---------------------------------------+
