import unittest
import sys
from base import NoPgcPyjionTestCase


# A class with a few attributes for testing the `getattr` and `setattr` builtins.
class F:
    a = 1
    b = 2
    c = 3
    d = 4


class GetAttrTestCase(NoPgcPyjionTestCase):

    def test_existing_attr(self):
        f = F()
        before = sys.getrefcount(f)
        self.assertIsNotNone(getattr(f, "a"))
        self.assertEqual(sys.getrefcount(f), before)

    def test_missing_attr(self):
        f = F()
        before = sys.getrefcount(f)
        with self.assertRaises(AttributeError):
            getattr(f, "e")
        self.assertEqual(sys.getrefcount(f), before)


class SetAttrTestCase(NoPgcPyjionTestCase):

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
