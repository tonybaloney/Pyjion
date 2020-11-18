Using Pyjion
============

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


You can see the machine code for the compiled function by disassembling it in the Python REPL.
Pyjion has essentially compiled your small Python function into a small, standalone application.
Install `distorm3` first to disassemble x86-64 assembly and run `pyjion.dis.dis_native(f)`:

.. code-block::

    >>> import pyjion.dis
    >>> pyjion.dis.dis_native(half)
    00000000: PUSH RBP
    00000001: MOV RBP, RSP
    00000004: PUSH R14
    00000006: PUSH RBX
    00000007: MOV RBX, RSI
    0000000a: MOV R14, [RDI+0x40]
    0000000e: CALL 0x1b34
    00000013: CMP DWORD [RAX+0x30], 0x0
    00000017: JZ 0x31
    00000019: CMP QWORD [RAX+0x40], 0x0
    0000001e: JZ 0x31
    00000020: MOV RDI, RAX
    00000023: MOV RSI, RBX
    00000026: XOR EDX, EDX
    00000028: POP RBX
    00000029: POP R14
    ...

The complex logic of converting a portable instruction set into low-level machine instructions is done by .NET's CLR JIT compiler.

All Python code executed after the JIT is enabled will be compiled into native machine code at runtime and cached on disk. For example, to enable the JIT on a simple `app.py` for a Flask web app:

.. code-block:: python

    import pyjion
    pyjion.enable()

    from flask import Flask
    app = Flask(__name__)

    @app.route('/')
    def hello_world():
        return 'Hello, World!'

    app.run()

