import pyjion.dis
import pyjion
import unittest
import io
import contextlib
import dis
import sys


class OptimizationsTestCase(unittest.TestCase):

    def test_decref_no_opt(self):

        pyjion.enable()
        pyjion.set_optimization_level(0)

        def test_f(a, b):
            return a is b

        self.assertFalse(test_f(1, 2))
        pyjion.disable()
        self.assertTrue(pyjion.info(test_f)['compiled'])
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            pyjion.dis.dis(test_f)
        self.assertIn("ldarg.1", f.getvalue())
        self.assertIn("MethodTokens.METHOD_DECREF_TOKEN", f.getvalue())
        self.assertNotIn("MethodTokens.METHOD_DEALLOC_OBJECT", f.getvalue())

    def test_decref_with_opt(self):
        pyjion.set_optimization_level(1)
        pyjion.enable()

        def test_f(a, b):
            return a is b

        self.assertFalse(test_f(1, 2))
        pyjion.disable()
        self.assertTrue(pyjion.info(test_f)['compiled'])

        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            pyjion.dis.dis(test_f)
        self.assertIn("ldarg.1", f.getvalue())
        self.assertNotIn("MethodTokens.METHOD_DECREF_TOKEN", f.getvalue())
        self.assertIn("MethodTokens.METHOD_DEALLOC_OBJECT", f.getvalue())


if __name__ == "__main__":
    unittest.main()
