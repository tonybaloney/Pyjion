import pyjion
import unittest
from base import PyjionTestCase


class RecursionTestCase(PyjionTestCase):

    def test_import(self):
        def _f():
            import math
            return math.sqrt(4)

        self.assertEqual(_f(), 2)
        info = pyjion.info(_f)
        self.assertTrue(info['compiled'])

    def test_import_from(self):
        def _f():
            from math import sqrt
            return sqrt(4)

        self.assertEqual(_f(), 2)
        info = pyjion.info(_f)
        self.assertTrue(info['compiled'])

    def test_import_from_aliased(self):
        def _f():
            from math import sqrt as square_root
            return square_root(4)

        self.assertEqual(_f(), 2)
        info = pyjion.info(_f)
        self.assertTrue(info['compiled'])


if __name__ == "__main__":
    unittest.main()
