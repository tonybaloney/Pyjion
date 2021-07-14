"""Test the optimization of intern values (-5 - 256)"""
import unittest
import pyjion
import io
import pyjion.dis
import contextlib
from base import PyjionTestCase


class InternIntegerTestCase(PyjionTestCase):

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

    def test_const_from_builtin(self):
        def test_f():
            a = 2
            b = int("3")
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

        self.assertOptimized(test_f)

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

        self.assertOptimized(test_f)


class InternIntegerSubscrTestCase(PyjionTestCase):

    def test_dict_key(self):
        def test_f():
            a = {0: 'a'}
            a[0] = 'b'
            return a[0] == 'b'

        self.assertTrue(test_f())
        self.assertTrue(pyjion.info(test_f)['compiled'])
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            pyjion.dis.dis(test_f)
        self.assertIn("ldarg.1", f.getvalue())
        self.assertIn("MethodTokens.METHOD_STORE_SUBSCR_DICT", f.getvalue())

    def test_dict_key_invalid_index(self):
        def test_f_subscr():
            a = {0: 'a'}
            return a[1] == 'b'

        with self.assertRaises(KeyError):
            test_f_subscr()
        self.assertTrue(pyjion.info(test_f_subscr)['compiled'])
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            pyjion.dis.dis(test_f_subscr)
        self.assertIn("ldarg.1", f.getvalue())
        self.assertIn("MethodTokens.METHOD_SUBSCR_DICT_HASH", f.getvalue())

    def test_list_key(self):
        def test_f():
            a = ['a']
            a[0] = 'b'
            return a[0] == 'b'

        self.assertTrue(test_f())
        self.assertTrue(pyjion.info(test_f)['compiled'])
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            pyjion.dis.dis(test_f)
        self.assertIn("ldarg.1", f.getvalue())
        self.assertIn("MethodTokens.METHOD_STORE_SUBSCR_LIST_I", f.getvalue())

    def test_list_key_builtin(self):
        def test_f():
            a = list(('a',))
            a[0] = 'b'
            return a[0] == 'b'

        self.assertTrue(test_f())
        self.assertTrue(pyjion.info(test_f)['compiled'])
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            pyjion.dis.dis(test_f)
        self.assertIn("ldarg.1", f.getvalue())
        self.assertIn("MethodTokens.METHOD_STORE_SUBSCR_LIST_I", f.getvalue())

    def test_list_key_non_const(self):
        def test_f(b):
            a = ['a']
            a[b] = 'b'
            return a[b] == 'b'

        self.assertTrue(test_f(0))
        self.assertTrue(pyjion.info(test_f)['compiled'])
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            pyjion.dis.dis(test_f)
        self.assertIn("ldarg.1", f.getvalue())
        self.assertNotIn("MethodTokens.METHOD_STORE_SUBSCR_LIST_I", f.getvalue())
        self.assertIn("MethodTokens.METHOD_STORE_SUBSCR_LIST", f.getvalue())

    def test_list_from_builtin_key_non_const(self):
        def test_f(b):
            a = list(('a',))
            a[b] = 'b'
            return a[b] == 'b'

        self.assertTrue(test_f(0))
        self.assertTrue(pyjion.info(test_f)['compiled'])
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            pyjion.dis.dis(test_f)
        self.assertIn("ldarg.1", f.getvalue())
        self.assertNotIn("MethodTokens.METHOD_STORE_SUBSCR_LIST_I", f.getvalue())
        self.assertIn("MethodTokens.METHOD_STORE_SUBSCR_LIST", f.getvalue())

    def test_list_key_invalid_index(self):
        def test_f_subscr():
            l = [0, 1, 2]
            return l[4] == 'b'

        with self.assertRaises(IndexError):
            test_f_subscr()
        self.assertTrue(pyjion.info(test_f_subscr)['compiled'])
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            pyjion.dis.dis(test_f_subscr)
        self.assertIn("ldarg.1", f.getvalue())
        self.assertIn("MethodTokens.METHOD_SUBSCR_LIST_I", f.getvalue())

    def test_unknown_key_string_const(self):
        def test_f(x):
            x['y'] = 'b'
            return x['y'] == 'b'

        self.assertTrue(test_f({}))
        self.assertTrue(pyjion.info(test_f)['compiled'])
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            pyjion.dis.dis(test_f)
        self.assertIn("ldarg.1", f.getvalue())
        self.assertIn("MethodTokens.METHOD_STORE_SUBSCR_DICT_HASH", f.getvalue())

    def test_unknown_int_string_const(self):
        def test_f(x):
            x[10] = 'b'
            return x[10] == 'b'

        self.assertTrue(test_f({}))
        self.assertTrue(pyjion.info(test_f)['compiled'])
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            pyjion.dis.dis(test_f)
        self.assertIn("ldarg.1", f.getvalue())
        self.assertIn("MethodTokens.METHOD_SUBSCR_DICT_HASH", f.getvalue())


if __name__ == "__main__":
    unittest.main()
