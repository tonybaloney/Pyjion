.. _OPT-1:

OPT-1 Inline DECREF operations when the reference count is >1
=============================================================

Background
----------

CPython's reference counter uses two C macros, ``Py_INCREF`` and ``Py_DECREF``. ``Py_INCREF`` is simple, because it increments the ``ob_refcnt`` field of ``PyObject*`` by 1.
For Pyjion, any opcodes which use Py_INCREF, instead fetch the current refcnt by calculating the offset of the ``ob_refcnt`` field to the ``PyObject*`` address. This is then incremented by 1:

.. code-block:: c

    LD_FIELDA(PyObject, ob_refcnt);
    m_il.dup();
    m_il.ld_ind_i4();
    m_il.ld_i4(1);
    m_il.add();
    m_il.st_ind_i4();

The machine-code for this function is efficient, as it requires a small operation on a qword (calculate offset), an increment on an I4 (add 1) and storing back to the address.

The INCREF emitter is used by both ``LOAD_CONST`` (load frame const value) and ``LOAD_FAST`` (load name).

``Py_DECREF`` however, needs to call the object's dealloc function (``tp_dealloc`` slot) to free any fields, properties, etc. before free'ing the memory allocated for the object.
For example, the dealloc for a tuple will decrement the refcounts for all items within the tuple (and potentially dealloc any of the items).

Decref is more significant, because it is used by many opcodes:

 - ``DELETE_FAST``
 - ``IS_OP``
 - ``POP_TOP``
 - ``UNPACK_SEQUENCE``
 - ``STORE_FAST``

CPython optimizes this by inlining the decref method:

.. code-block:: c

    static inline void _Py_DECREF(
    #ifdef Py_REF_DEBUG
        const char *filename, int lineno,
    #endif
        PyObject *op)
    {
    #ifdef Py_REF_DEBUG
        _Py_RefTotal--;
    #endif
        if (--op->ob_refcnt != 0) {
    #ifdef Py_REF_DEBUG
            if (op->ob_refcnt < 0) {
                _Py_NegativeRefcount(filename, lineno, op);
            }
    #endif
        }
        else {
            _Py_Dealloc(op);
        }
    }

Without this optimization, Pyjion will call a method, ``PyJit_Decref()`` which calls the ``Py_XDECREF`` macro (same as ``Py_DECREF`` but checks ``null``).

This operation is slow because:

- It requires a global method call (requires a jump pointer on Windows and Linux as well)
- x86_64 preamble/ABI adds overhead

Solution
--------

The OPT-1 optimization will instead implement a simple CIL bytecode sequence, similar to the incref emitter, equivalent to this C code:

.. code-block:: c

    if (obj == NULL) return;
    --op->ob_refcnt;
    if (op->ob_refcnt <= 0)
    {
        _Py_Dealloc(op);
    }

By reusing the reference to ``obj`` and dup'ing the value, the entire operation can be handled by CIL evaluation stack (and therefore fit within registers):

.. code-block:: c++

    Label done = emit_define_label();
    Label popAndGo = emit_define_label();
                                    // -- EE Stack Effect --
    m_il.dup();                     // obj, obj
    emit_null();                    // obj, obj, null
    emit_branch(BranchEqual, popAndGo);

    m_il.dup(); m_il.dup();         // obj, obj, obj
    LD_FIELDA(PyObject, ob_refcnt); // obj, obj, refcnt
    m_il.dup();                     // obj, obj, refcnt, refcnt
    m_il.ld_ind_i4();               // obj, obj, refcnt, *refcnt
    m_il.ld_i4(1);                  // obj, obj, refcnt,  *refcnt, 1
    m_il.sub();                     // obj, obj, refcnt, (*refcnt - 1)
    m_il.st_ind_i4();               // obj, obj

    LD_FIELD(PyObject, ob_refcnt); // obj, refcnt
    m_il.ld_i4(0);                 // obj, refcnt, 0
    emit_branch(BranchGreaterThan, popAndGo);

    m_il.emit_call(METHOD_DEALLOC_OBJECT); // _Py_Dealloc
    emit_branch(BranchAlways, done);
    emit_mark_label(popAndGo);
    emit_pop();

    emit_mark_label(done);

Gains
-----

- The ``POP_TOP`` opcode is now ultra-efficient. In particular this would speed up breaking from a block and an exception handling region.
- The ``IS_OP`` opcode (``a is b`` or ``a is not b``) combined with :ref:`OPT-2` is much more efficient that the CPython equivalent.

Edge-cases
----------

This optimization would remove support for ``Py_REF_DEBUG``, the reference counting debug option. This option isn't compiled into the release binaries of CPython and requires ``--with-pydebug`` support (only used for testing).
In those scenarios, this optimization should be disabled.

Configuration
-------------

This optimization is enabled at **level 1** by default. See :ref:`Optimizations <optimizations>` for help on changing runtime optimization settings.

+------------------------------+-------------------------------+
| Compile-time flag            |  ``OPTIMIZE_DECREF=OFF``      |
+------------------------------+-------------------------------+
| Default optimization level   |  ``1``                        |
+------------------------------+-------------------------------+
