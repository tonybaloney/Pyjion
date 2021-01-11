import sys
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
        self.assertEqual(sys.getrefcount(a), 3)
        self.assertEqual(sys.getrefcount(b), 3)
        self.assertEqual(sys.getrefcount(c), 2)
        self.assertEqual(c, 10.0)
        c /= a + b
        self.assertEqual(sys.getrefcount(a), 3)
        self.assertEqual(sys.getrefcount(b), 3)
        self.assertEqual(sys.getrefcount(c), 2)
        self.assertEqual(c, 2.0)


if __name__ == "__main__":
    unittest.main()
