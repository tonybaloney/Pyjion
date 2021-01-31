![](https://github.com/tonybaloney/Pyjion/raw/master/Docs/source/_static/pyjion_logo.png)

Pyjion, a JIT extension for CPython that compiles your Python code into native CIL and executes it using the .NET 5 CLR.

[![Documentation Status](https://readthedocs.org/projects/pyjion/badge/?version=latest)](https://pyjion.readthedocs.io/en/latest/?badge=latest)
[![PyPI](https://img.shields.io/pypi/v/pyjion?label=pypi%20package)](https://pypi.org/p/pyjion)

## Installing 

```console
$ pip install pyjion
```

## Compiling from source

Prerequisites: 

- CPython 3.9.0
- CMake 3.2 + 
- .NET 5

```console
 $ git clone git@github.com:tonybaloney/pyjion --recurse-submodules
 $ cd pyjion
 $ python -m pip install .
```

## Using Pyjion

To get started, you need to have .NET 5 installed, with Python 3.9 and the Pyjion package (I also recommend using a virtual environment).

After importing pyjion, enable it by calling `pyjion.enable()` which sets a compilation threshold to 0 (the code only needs to be run once to be compiled by the JIT):

```pycon
>>> import pyjion
>>> pyjion.enable()
```

Any Python code you define or import after enabling pyjion will be JIT compiled. You don't need to execute functions in any special API, its completely transparent:

```pycon
>>> def half(x):
...    return x/2
>>> half(2)
1.0
```

Pyjion will have compiled the `half` function into machine code on-the-fly and stored a cached version of that compiled function inside the function object.
You can see some basic stats by running `pyjion.info(f)`, where `f` is the function object:

```pycon
>>> pyjion.info(half)
{'failed': False, 'compiled': True, 'run_count': 1}
```

You can see the machine code for the compiled function by disassembling it in the Python REPL.
Pyjion has essentially compiled your small Python function into a small, standalone application.
Install `distorm3` and `rich` first to disassemble x86-64 assembly and run `pyjion.dis.dis_native(f)`:

```pycon
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
```

The complex logic of converting a portable instruction set into low-level machine instructions is done by .NET's CLR JIT compiler.

All Python code executed after the JIT is enabled will be compiled into native machine code at runtime and cached on disk. For example, to enable the JIT on a simple `app.py` for a Flask web app:

```python

from src import pyjion
pyjion.enable()

from flask import Flask
app = Flask(__name__)

@app.route('/')
def hello_world():
    return 'Hello, World!'

app.run()
```

## FAQ

### How do you pronounce "Pyjion"?
Like the word "pigeon". @DinoV wanted a name that had something with "Python"
-- the "Py" part -- and something with "JIT" -- the "JI" part -- and have it be
pronounceable.

### How do this compare to ...

#### [PyPy](http://pypy.org/)?
[PyPy](http://pypy.org/) is an implementation of Python with its own JIT. The
biggest difference compared to Pyjion is that PyPy doesn't support all C extension
modules without modification unless they use [CFFI](https://cffi.readthedocs.org)
or work with the select subset of CPython's C API that PyPy does support.
Pyjion also aims to support many JIT compilers while PyPy only supports their
custom JIT compiler.

#### [Pyston](http://pyston.org)?
[Pyston](http://pyston.org) is an implementation of Python using
[LLVM](http://llvm.org/) as a JIT compiler. Compared to Pyjion, Pyston has
partial CPython C API support but not complete support. Pyston also only
supports LLVM as a JIT compiler.

#### [Numba](http://numba.pydata.org/)?
[Numba](http://numba.pydata.org/) is a JIT compiler for "array-oriented and
math-heavy Python code". This means that Numba is focused on scientific
computing while Pyjion tries to optimize all Python code. Numba also only
supports LLVM.

#### [IronPython](http://ironpython.net/)?
[IronPython](http://ironpython.net/) is an implementation of Python that is
implemented using [.NET](http://microsoft.com/NET). While IronPython tries to
be usable from within .NET, Pyjion does not have a compatibility story with .NET.
This also means IronPython cannot use C extension modules while Pyjion can.

#### [Psyco](http://psyco.sourceforge.net/)?
[Psyco](http://psyco.sourceforge.net/) was a module that monkeypatched CPython
to add a custom JIT compiler. Pyjion wants to introduce a proper C API for
adding a JIT compiler to CPython instead of monkeypatching it. It should be
noted the creator of Psyco went on to be one of the co-founders of PyPy.

#### [Unladen Swallow](https://en.wikipedia.org/wiki/Unladen_Swallow)?
[Unladen Swallow](https://en.wikipedia.org/wiki/Unladen_Swallow) was an attempt
to make LLVM be a JIT compiler for CPython. Unfortunately the project lost
funding before finishing their work after having to spend a large amount of
time fixing issues in LLVM's JIT compiler (which has greatly improved over the
subsequent years).

#### [Nuitka](http://nuitka.net/) and [Shedskin](https://github.com/shedskin/shedskin)?
Both [Nuitka](http://nuitka.net/) and
[Shedskin](https://github.com/shedskin/shedskin) are Python-to-C++ transpilers,
which means they translate Python code into equivalent C++ code. Being a JIT,
Pyjion is not a transpiler.


### Will this ever ship with CPython?
Goal #1 is explicitly to add a C API to CPython to support JIT compilers. There
is no expectation, though, to ship a JIT compiler with CPython. This is because
CPython compiles with nothing more than a C89 compiler, which allows it to run
on many platforms. But adding a JIT compiler to CPython would immediately limit
it to only the platforms that the JIT supports.

### Does this help with using CPython w/ .NET or UWP?
No.

## Code of Conduct
This project has adopted the
[Microsoft Open Source Code of Conduct](https://opensource.microsoft.com/codeofconduct/).
For more information see the
[Code of Conduct FAQ](https://opensource.microsoft.com/codeofconduct/faq/)
or contact [opencode@microsoft.com](mailto:opencode@microsoft.com)
with any additional questions or comments.
