import pyjion
import unittest
import gc


class MathTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_floats(self):
        a = 2.0
        b = 3.0
        c = 4.0
        c += a * b
        self.assertEqual(c, 10.0)
        c /= a + b
        self.assertEqual(c, 2.0)
        c %= a % b
        self.assertEqual(c, 0.0)

    def test_ints(self):
        a = 2
        b = 3
        c = 4
        c += a * b
        self.assertEqual(c, 10)
        c /= a + b
        self.assertEqual(c, 2.0)
        c //= a + b
        self.assertEqual(c, 0)
        c = 4
        c %= a % b
        self.assertEqual(c, 0)

    def test_mixed(self):
        a = 2
        b = 3.0
        c = 4
        c += a * b
        self.assertEqual(c, 10.0)
        c /= a + b
        self.assertEqual(c, 2.0)
        c = 4
        c %= a % b
        self.assertEqual(c, 0.0)

    def test_mixed2(self):
        a = 2.0
        b = 3
        c = 4
        c += a * b
        self.assertEqual(c, 10.0)
        c /= a + b
        self.assertEqual(c, 2.0)
        c = 4
        c %= a % b
        self.assertEqual(c, 0.0)

    def test_mixed3(self):
        a = 2
        b = 3
        c = 4.0
        c += a * b
        self.assertEqual(c, 10.0)
        c /= a + b
        self.assertEqual(c, 2.0)
        c = 4.0
        c %= a % b
        self.assertEqual(c, 0.0)

    def test_mixed4(self):
        a = 2
        b = 3.0
        c = 4.0
        c += a * b
        self.assertEqual(c, 10.0)
        c /= a + b
        self.assertEqual(c, 2.0)
        c = 4.0
        c %= a % b
        self.assertEqual(c, 0.0)

        i = -10
        x = 1234567890.0 * (10.0 ** i)
        self.assertEqual(x, 0.12345678900000001)
        i = 0
        x = 1234567890.0 * (10.0 ** i)
        self.assertEqual(x, 1234567890.0)
        i = 10
        x = 1234567890.0 * (10.0 ** i)
        self.assertEqual(x, 1.23456789e+19)

    def test_mixed5(self):
        a = 2.0
        b = 3
        c = 4.0
        c += a * b
        self.assertEqual(c, 10.0)
        c /= a + b
        self.assertEqual(c, 2.0)
        c %= a + b
        self.assertEqual(c, 2)

    def test_mixed6(self):
        a = 2.0
        b = 3.0
        c = 4
        c += a * b
        self.assertEqual(c, 10.0)
        c /= a + b
        self.assertEqual(c, 2.0)
        c %= a % b
        self.assertEqual(c, 0.0)

    def test_modulo(self):
        a = 1
        b = 2
        c = "boo %s"
        x = c % (a + b)
        self.assertEqual(x, "boo 3")


if __name__ == "__main__":
    unittest.main()
