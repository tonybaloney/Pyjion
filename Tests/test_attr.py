import gc
import pyjion
import unittest
import sys


# A class with a few attributes for testing the `getattr` and `setattr` builtins.
class F:
    a = 1
    b = 2
    c = 3
    d = 4


class GetAttrTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_existing_attr(self):
        f = F()
        before = sys.getrefcount(f)
        self.assertIsNotNone(getattr(f, "a"))
        self.assertEqual(before, sys.getrefcount(f))

    def test_missing_attr(self):
        f = F()
        before = sys.getrefcount(f)
        with self.assertRaises(AttributeError):
            getattr(f, "e")
        self.assertEqual(before, sys.getrefcount(f))


class SetAttrTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_existing_attr(self):
        f = F()
        before = sys.getrefcount(f)
        self.assertIsNone(setattr(f, "a", 10))
        self.assertEqual(f.a, 10)
        self.assertEqual(before, sys.getrefcount(f))

    def test_new_attr(self):
        f = F()
        before = sys.getrefcount(f)
        self.assertIsNone(setattr(f, "e", 5))
        self.assertEqual(f.e, 5)
        self.assertEqual(before, sys.getrefcount(f))


if __name__ == "__main__":
    unittest.main()
