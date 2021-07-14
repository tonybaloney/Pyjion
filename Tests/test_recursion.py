import pyjion
import unittest
from base import PyjionTestCase


class RecursionTestCase(PyjionTestCase):

    def test_basic(self):
        def _f():
            def add_a(z):
                if len(z) < 5:
                    z.append('a')
                    return add_a(z)
                return z
            return add_a([])

        self.assertEqual(_f(), ['a', 'a', 'a', 'a', 'a'])
        info = pyjion.info(_f)
        self.assertTrue(info['compiled'])

    def test_recursive_listcomp(self):
        def _f(t):
            print("-")
            if isinstance(t, list):
                return [_f(e) for e in t]
            else:
                return t

        self.assertEqual(_f([[0,2], 2, 3]), [[0, 2], 2, 3])
        info = pyjion.info(_f)
        self.assertTrue(info['compiled'])


if __name__ == "__main__":
    unittest.main()
