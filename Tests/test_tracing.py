import pyjion.dis
import pyjion
import unittest
import io
import contextlib
import dis
import sys


class TracingModuleTestCase(unittest.TestCase):

    @classmethod
    def setUpClass(cls) -> None:
        pyjion.enable_tracing()

    @classmethod
    def tearDownClass(cls) -> None:
        pyjion.disable_tracing()

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()

    def test_traces_in_code(self):
        def test_f():
            a = 1
            b = 2
            c = 3
            d = 4
            return a+b+c+d

        self.assertTrue(test_f() == 10)
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            pyjion.dis.dis(test_f)
        self.assertIn("ldarg.1", f.getvalue())
        self.assertIn("MethodTokens.METHOD_TRACE_FRAME_ENTRY", f.getvalue())
        self.assertIn("MethodTokens.METHOD_TRACE_LINE", f.getvalue())

    def test_custom_tracer(self):
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

        def test_f():
            a = 1
            b = 2
            c = 3
            d = 4
            return a+b+c+d

        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            sys.settrace(custom_trace)
            self.assertTrue(test_f() == 10)
            sys.settrace(None)

        self.assertIn("Calling <code object test_f", f.getvalue())
        self.assertIn("Changing line to ", f.getvalue())
        self.assertIn("Returning ", f.getvalue())
        self.assertIn("STORE_FAST", f.getvalue())

if __name__ == "__main__":
    unittest.main()
