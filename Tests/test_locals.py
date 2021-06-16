"""Test the optimization of frame locals"""
import unittest
import pyjion
import io
import pyjion.dis
import contextlib
import gc

class LocalsTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def assertOptimized(self, func) -> None:
        self.assertFalse(func())
        self.assertTrue(pyjion.info(func)['compiled'])
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            pyjion.dis.dis(func)
        self.assertIn("ldloc.s", f.getvalue())

    def test_simple_compare(self):
        def test_f():
            a = 1
            b = 2
            return a == b
        self.assertOptimized(test_f)

    def test_simple_delete(self):
        def test_f():
            a = 1
            b = 2
            del a
            del b
            return False
        self.assertOptimized(test_f)


if __name__ == "__main__":
    unittest.main()
