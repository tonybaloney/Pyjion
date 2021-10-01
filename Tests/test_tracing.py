import pyjion.dis
import pyjion
import io
import dis
import sys
import pytest


def test_traces_in_code(capsys):
    def _f():
        a = 1
        b = 2
        c = 3
        d = 4
        return a + b + c + d

    assert _f() == 10
    pyjion.dis.dis(_f)
    captured = capsys.readouterr()
    assert "ldarg.1" in captured.out
    assert "METHOD_TRACE_FRAME_ENTRY" not in captured.out
    assert "METHOD_TRACE_LINE" not in captured.out
    assert "METHOD_TRACE_FRAME_EXIT" not in captured.out


def test_custom_tracer(capsys):
    def custom_trace(frame, event, args):
        frame.f_trace_opcodes = True
        if event == 'opcode':
            out = io.StringIO()
            dis.disco(frame.f_code, frame.f_lasti, file=out)
            lines = out.getvalue().split('\\n')
            [print(f"{l}") for l in lines]
            out.close()
        elif event == 'call':
            print(f"Calling {frame.f_code}")
        elif event == 'return':
            print(f"Returning {args}")
        elif event == 'line':
            print(f"Changing line to {frame.f_lineno}")
        else:
            print(f"{frame} ({event} - {args})")
        return custom_trace

    def test_f():
        a = 1
        b = 2
        c = 3
        d = 4
        return a + b + c + d

    sys.settrace(custom_trace)
    assert test_f() == 10
    sys.settrace(None)
    captured = capsys.readouterr()
    assert "Calling <code object _f" in captured.out
    assert "Changing line to " in captured.out
    assert "Returning " in captured.out
    assert "STORE_FAST" in captured.out


def test_eh_tracer(capsys):
    def custom_trace(frame, event, args):
        frame.f_trace_opcodes = True
        if event == 'exception':
            print("Hit exception")
            print(*args)
        return custom_trace

    def test_f():
        a = 1 / 0
        return a

    sys.settrace(custom_trace)
    pytest.raises(ZeroDivisionError, test_f)
    sys.settrace(None)
    captured = capsys.readouterr()
    assert "Hit exception" in captured.out


def test_profile_hooks_in_code(capsys):
    def _f():
        a = 1
        b = 2
        c = 3
        d = 4
        return a + b + c + d

    assert _f() == 10
    pyjion.dis.dis(_f)
    captured = capsys.readouterr()
    assert "ldarg.1" in captured.out
    assert "METHOD_PROFILE_FRAME_ENTRY" not in captured.out
    assert "METHOD_PROFILE_FRAME_EXIT" not in captured.out


def test_custom_profiler(capsys):
    def custom_trace(frame, event, args):
        frame.f_trace_opcodes = True
        if event == 'opcode':
            with io.StringIO() as out:
                dis.disco(frame.f_code, frame.f_lasti, file=out)
                lines = out.getvalue().split('\\n')
                [print(f"{l}") for l in lines]
        elif event == 'call':
            print(f"Calling {frame.f_code}")
        elif event == 'return':
            print(f"Returning {args}")
        elif event == 'line':
            print(f"Changing line to {frame.f_lineno}")
        else:
            print(f"{frame} ({event} - {args})")
        return custom_trace

    def _f():
        a = 1
        b = 2
        c = 3
        d = 4
        return a + b + c + d

    sys.setprofile(custom_trace)
    assert _f() == 10
    sys.setprofile(None)
    captured = capsys.readouterr()
    assert "Calling <code object _f" in captured.out
    assert "Returning " in captured.out
