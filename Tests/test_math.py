import pyjion
import unittest
import gc
import statistics
from fractions import Fraction


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


class StatisticsTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()
        pyjion.enable_pgc()
        pyjion.enable_graphs()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_mean(self):
        answer = statistics.mean([1, 2, 3, 4, 4])
        self.assertEqual(answer, 2.8)
        answer = statistics.mean([1, 2, 3, 4, 4])
        self.assertEqual(answer, 2.8)

    def test_variance(self):
        data = [0, 0, 1]
        result = statistics.variance(data)
        self.assertEqual(result, 0.33333333333333337)

    def test_variance2(self):
        data = [0, 1]
        result = statistics.variance(data)
        self.assertEqual(result, 0.5)

    def test_variance_slow(self):
        data = [0, 0, 1]
        c = statistics.mean(data)
        self.assertEqual(c, 0.3333333333333333)
        T, total, count = statistics._sum((x-c)**2 for x in data)
        self.assertEqual(T, float)
        self.assertEqual(total, Fraction(3002399751580331, 4503599627370496))

    def test_fraction_operations(self):
        result1 = Fraction(2, 3) + Fraction(7, 5)
        self.assertEqual(result1, Fraction(31, 15))
        result2 = Fraction(2, 3) - Fraction(7, 5)
        self.assertEqual(result2, Fraction(-11, 15))
        result3 = Fraction(2, 3) * Fraction(7, 5)
        self.assertEqual(result3, Fraction(14, 15))
        result4 = Fraction(2, 3) ** Fraction(7, 5)
        self.assertEqual(result4, 0.5668553336114626)


if __name__ == "__main__":
    unittest.main()
