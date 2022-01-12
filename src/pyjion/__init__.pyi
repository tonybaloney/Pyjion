from typing import Dict, Any, Callable, Optional, Union

from pyjion import JitInfo, CompileMode


def enable() -> bool:
    """
    Enable the JIT.
    
    :returns: ``True`` on success, ``False`` on failure.
    """
    ...

def disable() -> bool:
    """
    Disable the JIT.
    
    :returns: ``True`` on success, ``False`` on failure.
    """
    ...

def info(f: Callable) -> JitInfo:
    """
    Pyjion JIT information on a compiled function.

    >>> pyjion.enable()
    >>> def f():
            a = 1
            b = 2
            c = 3
            d = 4
            return a + b + c + d
    >>> f()
    10
    >>> pyjion.info(f)

    
    :param f: The compiled function or code-object
    :returns: Information on the 
    """
    ...

def config(pgc: Optional[bool], level: Optional[int], debug: Optional[Union[bool, CompileMode]], graph: Optional[bool], threshold: Optional[int], ) -> Dict[str, Any]:
    ...

def offsets(f: Callable) -> tuple[tuple[int, int, int, int]]:
    ...

def graph(f: Callable) -> str:
    ...

def symbols(f: Callable) -> Dict[int, str]:
    ...

def il(f: Callable) -> bytearray:
    ...

def native(f: Callable) -> tuple[bytearray, int, int]:
    ...

class PyjionUnboxingError(ValueError):
    ...

__version__: str
