import ctypes
import pathlib
import os
import platform
from enum import IntFlag, IntEnum
from dataclasses import dataclass

__version__ = '1.2.1'


def _no_dotnet(path):
    raise ImportError(f"Can't find a .NET 6 installation in {path}, "
                      "provide the DOTNET_ROOT environment variable "
                      "if it's installed somewhere unusual")


def _which_dotnet() -> str:
    """
    Locate the clrjit library path
    """
    _dotnet_root = None
    if 'DOTNET_ROOT' in os.environ:
        _dotnet_root = pathlib.Path(os.environ['DOTNET_ROOT'])
        if not _dotnet_root.exists():
            _no_dotnet(_dotnet_root)
    if 'DOTNET_LIB_PATH' in os.environ:
        ctypes.cdll.LoadLibrary(os.environ['DOTNET_LIB_PATH'])
        return os.environ['DOTNET_LIB_PATH']
    if platform.system() == "Darwin":
        if not _dotnet_root:
            _dotnet_root = pathlib.Path('/usr/local/share/dotnet/')
            if not _dotnet_root.exists():
                _no_dotnet(_dotnet_root)
        lib_path = list(_dotnet_root.glob('shared/Microsoft.NETCore.App*/6.0.*/libclrjit.dylib'))
        if len(lib_path) > 0:
            clrjitlib = str(lib_path[0])
            ctypes.cdll.LoadLibrary(clrjitlib)
            return clrjitlib
        else:
            _no_dotnet(_dotnet_root)
    elif platform.system() == "Linux":
        if not _dotnet_root:
            search_paths = [pathlib.Path('/usr/local/share/dotnet/'), pathlib.Path('/usr/share/dotnet/')]
            for path in search_paths:
                if not path.exists():
                    continue
                else:
                    _dotnet_root = path
        if not _dotnet_root:
            _no_dotnet(_dotnet_root)
        lib_path = list(_dotnet_root.glob('shared/Microsoft.NETCore.App*/6.0.*/libclrjit.so'))
        if len(lib_path) > 0:
            clrjitlib = str(lib_path[0])
            ctypes.cdll.LoadLibrary(clrjitlib)
            return clrjitlib
        else:
            _no_dotnet(_dotnet_root)
    elif platform.system() == "Windows":
        if not _dotnet_root:
            _dotnet_root = pathlib.WindowsPath(os.path.expandvars(r'%ProgramFiles%\dotnet'))
            if not _dotnet_root.exists():
                _no_dotnet(_dotnet_root)
        lib_path = list(_dotnet_root.glob('shared/Microsoft.NETCore.App*/6.0.*/clrjit.dll'))
        if len(lib_path) > 0:
            clrjitlib = str(lib_path[0])
            ctypes.cdll.LoadLibrary(clrjitlib)
            return clrjitlib
        else:
            _no_dotnet(_dotnet_root)
    else:
        raise ValueError("Operating System not Supported")


lib_path = _which_dotnet()

try:
    from ._pyjion import enable, disable, info as _info, il, native, offsets, \
        graph, init as _init, symbols, config, PyjionUnboxingError

    _init(lib_path)
except ImportError:
    raise ImportError(
        """
Failed to import the compiled Pyjion module. This normally means something went wrong during pip install
and the binaries weren't compiled. Make sure you update pip before installing to get the right wheel.
If that doesn't work, run pip in verbose mode, or file an issue at https://github.com/tonybaloney/pyjion/.
"""
    )


class OptimizationFlags(IntFlag):
    InlineIs = 1
    InlineDecref = 2
    InternRichCompare = 4
    InlineFramePushPop = 8
    KnownStoreSubscr = 16
    KnownBinarySubscr = 32
    InlineIterators = 64
    HashedNames = 128
    BuiltinMethods = 256
    TypeSlotLookups = 512
    FunctionCalls = 1024
    LoadAttr = 2048
    Unboxing = 4096
    IsNone = 8192
    IntegerUnboxingMultiply = 16384
    OptimisticIntegers = 32768


class CompilationResult(IntEnum):
    NoResult = 0,
    Success = 1,
    CompilationException = 10
    CompilationJitFailure = 11
    CompilationStackEffectFault = 12
    IncompatibleCompilerFlags = 100
    IncompatibleSize = 101
    IncompatibleOpcode_Yield = 102
    IncompatibleOpcode_WithExcept = 103
    IncompatibleOpcode_With = 104
    IncompatibleOpcode_Unknown = 110
    IncompatibleFrameGlobal = 120


class PgcStatus(IntEnum):
    Uncompiled = 0
    CompiledWithProbes = 1
    Optimized = 2


@dataclass()
class JitInfo:
    failed: bool
    compile_result: CompilationResult
    compiled: bool
    optimizations: OptimizationFlags
    pgc: PgcStatus
    run_count: int
    tracing: bool
    profiling: bool


def info(f) -> JitInfo:
    d = _info(f)
    return JitInfo(d['failed'],
                   CompilationResult(d['compile_result']),
                   d['compiled'],
                   OptimizationFlags(d['optimizations']),
                   PgcStatus(d['pgc']),
                   d['run_count'],
                   d['tracing'],
                   d['profiling'])
