from base import NoPgcPyjionTestCase, PyjionTestCase


class BinaryOperationTestCase(PyjionTestCase):

    def test_addition(self):
        a = 987654
        b = 123456
        c = a + b
        self.assertEqual(c, 1111110)

    def test_subtraction(self):
        a = 987654
        b = 123456
        c = a - b
        self.assertEqual(c, 864198)

    def test_multiplication(self):
        a = 987
        b = 1001
        c = a * b
        self.assertEqual(c, 987987)

    def test_division(self):
        a = 12341234
        b = 10001
        c = a / b
        self.assertEqual(c, 1234)

    def test_floor_division(self):
        a = 7777777
        b = 55555
        c = a // b
        self.assertEqual(c, 140)

    def test_power(self):
        a = 0.5
        b = -8
        c = a ** b
        self.assertEqual(c, 256)

    def test_or(self):
        a = 1999
        b = 2999
        c = a | b
        self.assertEqual(c, 4095)
    
    def test_and(self):
        a = 1999
        b = 2999
        c = a & b
        self.assertEqual(c, 903)

