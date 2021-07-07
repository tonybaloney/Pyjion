import pyjion
import unittest
import gc
import sys


class DictTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_bad_key(self):
        # Dictionary lookups should fail if __eq__() raises an exception.
        class CustomException(Exception):
            pass

        class BadDictKey:
            def __hash__(self):
                return hash(self.__class__)

            def __eq__(self, other):
                if isinstance(other, self.__class__):
                    raise CustomException
                return other

        d = {}
        x1 = BadDictKey()
        x2 = BadDictKey()
        d[x1] = 1

        with self.assertRaises(CustomException):
            d[x2] = 2
        with self.assertRaises(CustomException):
            z = d[x2]
        with self.assertRaises(CustomException):
            x2 in d
        with self.assertRaises(CustomException):
            d.setdefault(x2, 42)
        with self.assertRaises(CustomException):
            d.pop(x2)
        with self.assertRaises(CustomException):
            d.update({x2: 2})

    def test_dict_refcount(self):
        a = 1
        b = 2
        c = 3
        d = 4
        e = 5
        f = 6
        before_a = sys.getrefcount(a)
        before_b = sys.getrefcount(b)
        before_c = sys.getrefcount(c)
        before_d = sys.getrefcount(d)
        before_e = sys.getrefcount(e)
        before_f = sys.getrefcount(f)
        di = {
            a: d,
            b: e,
            c: f,
        }
        del di
        self.assertEqual(before_a, sys.getrefcount(a))
        self.assertEqual(before_b, sys.getrefcount(b))
        self.assertEqual(before_c, sys.getrefcount(c))
        self.assertEqual(before_d, sys.getrefcount(d))
        self.assertEqual(before_e, sys.getrefcount(e))
        self.assertEqual(before_f, sys.getrefcount(f))

    def test_big_dict(self):
        import sysconfig
        self.assertEqual(sysconfig.get_config_var('PYTHON'), 'python')


if __name__ == "__main__":
    unittest.main()
