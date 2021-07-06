import ctypes
import pathlib
import os
import platform

__version__ = '1.0.0b4'


def _no_dotnet(path):
    raise ImportError(f"Can't find a .NET 6 installation in {path}, "
                      "provide the DOTNET_ROOT environment variable "
                      "if its installed somewhere unusual")

# try and locate .NET
def _which_dotnet():
    _dotnet_root = None
    if 'DOTNET_ROOT' in os.environ:
        _dotnet_root = pathlib.Path(os.environ['DOTNET_ROOT'])
        if not _dotnet_root.exists():
            _no_dotnet(_dotnet_root)
    if 'DOTNET_LIB_PATH' in os.environ:
        ctypes.cdll.LoadLibrary(os.environ['DOTNET_LIB_PATH'])
        return
    if platform.system() == "Darwin":
        if not _dotnet_root:
            _dotnet_root = pathlib.Path('/usr/local/share/dotnet/')
            if not _dotnet_root.exists():
                _no_dotnet(_dotnet_root)
        lib_path = list(_dotnet_root.glob('shared/Microsoft.NETCore.App*/6.0.*/libclrjit.dylib'))
        if len(lib_path) > 0:
            clrjitlib = str(lib_path[0])
            ctypes.cdll.LoadLibrary(clrjitlib)
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
        else:
            _no_dotnet(_dotnet_root)
    else:
        raise ValueError("Operating System not Supported")


_which_dotnet()

try:
    from ._pyjion import *  # NOQA
except ImportError:
    raise ImportError(
"""
Failed to import the compiled Pyjion module. This normally means something went wrong during pip install
and the binaries weren't compiled. Make sure you update pip before installing to get the right wheel.
If that doesn't work, run pip in verbose mode, or file an issue at https://github.com/tonybaloney/pyjion/.
"""
    )
