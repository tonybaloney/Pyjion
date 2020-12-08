.. Pyjion documentation master file, created by
   sphinx-quickstart on Thu May 19 09:26:14 2016.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

Pyjion
======

Pyjion is an implementation and technical proof of concept for a JIT API for CPython. 

Installing Pyjion
-----------------

To install from PyPi, use pip to install from a virtual environment:

.. code-block:: console

   $ python -m pip install Pyjion

Using Pyjion
------------

After following the installation steps, pyjion is a python module that you can import a Python 3.9 environment.

To get started, you need to have .NET 5 installed, with Python 3.9 and the Pyjion package (I also recommend using a virtual environment).

After importing pyjion, enable it by calling `pyjion.enable()` which sets a compilation threshold to 0 (the code only needs to be run once to be compiled by the JIT):

.. code-block::

    >>> import pyjion
    >>> pyjion.enable()


Any Python code you define or import after enabling pyjion will be JIT compiled. You don't need to execute functions in any special API, its completely transparent:

.. code-block::

    >>> def half(x):
    ...    return x/2
    >>> half(2)
    1.0

Pyjion will have compiled the `half` function into machine code on-the-fly and stored a cached version of that compiled function inside the function object.
You can see some basic stats by running `pyjion.info(f)`, where `f` is the function object:

.. code-block::

    >>> pyjion.info(half)
    {'failed': False, 'compiled': True, 'run_count': 1}

Debugging
---------

To enable debugging of the compiled code, or tracing using a tracer (like a code coverage tool), you need to enable tracing before compiling the functions:

.. code-block::

    >>> pyjion.enable_tracing()

Because debugging adds an overhead to every line, statement and function call, it is disabled by default. You can disable it manually with ``disable_tracing()``:

.. code-block::

    >>> pyjion.disable_tracing()


Documentation
=============

Main
----

.. toctree::
    :glob:
    :maxdepth: 3

    gettingstarted
    building
    using
    api
    wsgi
