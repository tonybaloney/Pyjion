"""Test the optimization of intern values (-5 - 256)"""
import unittest
import pyjion
import io
import pyjion.dis
import contextlib

class InternIntegerTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()

    def assertNotOptimized(self, func) -> None:
        self.assertFalse(func())
        self.assertTrue(pyjion.info(func)['compiled'])
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            pyjion.dis.dis(func)
        self.assertIn("ldarg.1", f.getvalue())
        self.assertIn("MethodTokens.METHOD_RICHCMP_TOKEN", f.getvalue())

    def assertOptimized(self, func) -> None:
        self.assertFalse(func())
        self.assertTrue(pyjion.info(func)['compiled'])
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            pyjion.dis.dis(func)
        self.assertIn("ldarg.1", f.getvalue())
        self.assertNotIn("MethodTokens.METHOD_RICHCMP_TOKEN", f.getvalue())

    def test_const_compare(self):
        def test_f():
            a = 1
            b = 2
            return a == b
        self.assertOptimized(test_f)


    def test_const_compare_big_left(self):
        def test_f():
            a = 1000
            b = 2
            return a == b

        self.assertOptimized(test_f)

    def test_const_compare_big_right(self):
        def test_f():
            a = 1
            b = 2000
            return a == b

        self.assertOptimized(test_f)

    def test_const_compare_big_both(self):
        def test_f():
            a = 1000
            b = 2000
            return a == b

        self.assertNotOptimized(test_f)

    def test_const_not_integer(self):
        def test_f():
            a = 2
            b = "2"
            return a == b

        self.assertNotOptimized(test_f)

    def test_float_compare(self):
        def test_f():
            a = 2
            b = 1.0
            return a == b

        self.assertNotOptimized(test_f)


class InternIntegerSubscrTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()

    def assertNotOptimized(self, func) -> None:
        self.assertFalse(func())
        self.assertTrue(pyjion.info(func)['compiled'])
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            pyjion.dis.dis(func)
        self.assertIn("ldarg.1", f.getvalue())
        self.assertIn("MethodTokens.METHOD_STORE", f.getvalue())

    def assertOptimized(self, func) -> None:
        self.assertFalse(func())
        self.assertTrue(pyjion.info(func)['compiled'])
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            pyjion.dis.dis(func)
        self.assertIn("ldarg.1", f.getvalue())
        self.assertNotIn("MethodTokens.METHOD_RICHCMP_TOKEN", f.getvalue())

    def test_const_compare(self):
        def test_f():
            a = 1
            b = 2
            return a == b
        self.assertOptimized(test_f)


    def test_const_compare_big_left(self):
        def test_f():
            a = 1000
            b = 2
            return a == b

        self.assertOptimized(test_f)




if __name__ == "__main__":
    unittest.main()
