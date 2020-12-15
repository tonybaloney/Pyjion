.. _Optimizations:

Built-in Optimizations
======================

.. toctree::
    :caption: Index:

    opt/opt-1
    opt/opt-2
    opt/opt-3

Optimization Matrix
-------------------

.. list-table:: Default Optimization Levels
   :widths: 40 20 20 20
   :header-rows: 1

   * - Optimization
     - Level 0
     - Level 1
     - Level 2
   * - :ref:`OPT-1`
     - Off
     - On
     - On
   * - :ref:`OPT-2`
     - Off
     - On
     - On
   * - :ref:`OPT-3`
     - Off
     - On
     - On

Configuring Optimizations
-------------------------

Python API
~~~~~~~~~~

The optimization level can be set using the ``set_optimization_level(level: int)`` method:

.. code-block:: Python

    import pyjion
    pyjion.set_optimization_level(0)

The default level is 1.
Setting to level 2 will enable **all** optimizations.

Level 0 disables all optimizations.

Compile-time configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~

Optimizations are set in the ``CMakeOptimizations.txt`` file and can be individually set.

For example, to disable the OPTIMIZE_DECREF (OPT-1) at level 1 (default):

``cmake -OPTIMIZE_DECREF=OFF``