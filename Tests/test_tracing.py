from pyjion.dis import print_il, dis
import pyjion
import unittest
import io
import contextlib


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
            dis(test_f)
        self.assertIn("ldarg.1", f.getvalue())
        self.assertIn("MethodTokens.METHOD_TRACE_FRAME_ENTRY", f.getvalue())
        self.assertIn("MethodTokens.METHOD_TRACE_LINE", f.getvalue())

if __name__ == "__main__":
    unittest.main()
