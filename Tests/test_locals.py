"""Test the optimization of frame locals"""
import unittest
from base import PyjionTestCase


class LocalsTestCase(PyjionTestCase):

    def test_simple_compare(self):
        def test_f():
            a = 1
            b = 2
            return a == b

        self.assertFalse(test_f())

    def test_simple_delete(self):
        def test_f():
            a = 1
            b = 2
            del a
            del b
            return False

        self.assertFalse(test_f())


if __name__ == "__main__":
    unittest.main()
