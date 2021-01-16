import gc
import sys
import unittest

import pyjion


class BinaryOperationTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_addition(self):
        a = 987654
        b = 123456
        c = 192837
        before_ref_a = sys.getrefcount(a)
        before_ref_b = sys.getrefcount(b)
        before_ref_c = sys.getrefcount(c)
        c = a + b
        self.assertEqual(sys.getrefcount(a), before_ref_a)
        self.assertEqual(sys.getrefcount(b), before_ref_b)
        self.assertEqual(sys.getrefcount(c), before_ref_c - 2)
        self.assertEqual(c, 1111110)

    def test_subtraction(self):
        a = 987654
        b = 123456
        c = 192837
        before_ref_a = sys.getrefcount(a)
        before_ref_b = sys.getrefcount(b)
        before_ref_c = sys.getrefcount(c)
        c = a - b
        self.assertEqual(sys.getrefcount(a), before_ref_a)
        self.assertEqual(sys.getrefcount(b), before_ref_b)
        self.assertEqual(sys.getrefcount(c), before_ref_c - 2)
        self.assertEqual(c, 864198)

    def test_multiplication(self):
        a = 987
        b = 1001
        c = 1234321
        before_ref_a = sys.getrefcount(a)
        before_ref_b = sys.getrefcount(b)
        before_ref_c = sys.getrefcount(c)
        c = a * b
        self.assertEqual(sys.getrefcount(a), before_ref_a)
        self.assertEqual(sys.getrefcount(b), before_ref_b)
        self.assertEqual(sys.getrefcount(c), before_ref_c - 1)
        self.assertEqual(c, 987987)

    def test_division(self):
        a = 12341234
        b = 10001
        c = 98789
        before_ref_a = sys.getrefcount(a)
        before_ref_b = sys.getrefcount(b)
        before_ref_c = sys.getrefcount(c)
        c = a / b
        self.assertEqual(sys.getrefcount(a), before_ref_a)
        self.assertEqual(sys.getrefcount(b), before_ref_b)
        self.assertEqual(sys.getrefcount(c), before_ref_c - 1)
        self.assertEqual(c, 1234)
