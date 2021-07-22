import sys
import unittest
from base import NoPgcPyjionTestCase, PyjionTestCase


class StringFormattingTestCase(NoPgcPyjionTestCase):

    def test_perc_format(self):
        a = "Hello %s"
        before_ref = sys.getrefcount(a)
        c = a % ("world",)
        self.assertEqual(sys.getrefcount(a), before_ref, "a ref")
        self.assertEqual(c, "Hello world", "string match")
        b = "w0rld"
        before_ref_b = sys.getrefcount(b)
        before_ref_c = sys.getrefcount(c)
        c += a % (b,)
        self.assertEqual(sys.getrefcount(a), before_ref, "a leak")
        self.assertEqual(sys.getrefcount(b), before_ref_b, "b leak")
        self.assertEqual(sys.getrefcount(c), before_ref_c, "c leak")
        self.assertEqual(c, "Hello worldHello w0rld", "output fail")
        c += a % ("x", )
        self.assertEqual(sys.getrefcount(c), before_ref_c, "c leak")

    def test_add_inplace(self):
        c = "..."
        a = "Hello "
        b = "world!"
        before_ref = sys.getrefcount(a)
        before_ref_b = sys.getrefcount(b)
        before_ref_c = sys.getrefcount(c)
        c += a + b
        self.assertEqual(sys.getrefcount(a), before_ref)
        self.assertEqual(sys.getrefcount(b), before_ref_b)
        self.assertEqual(sys.getrefcount(c), before_ref_c - 1)
        self.assertEqual(c, "...Hello world!")


class FStringFormattingTestCase(NoPgcPyjionTestCase):

    def test_perc_format(self):
        place = "world"
        message = f"Hello {place}!"
        self.assertEqual(message, "Hello world!")


def _is_dunder(name):
    """Returns True if a __dunder__ name, False otherwise."""
    return (len(name) > 4 and
            name[:2] == name[-2:] == '__' and
            name[2] != '_' and
            name[-3] != '_')


def _is_sunder(name):
    """Returns True if a _sunder_ name, False otherwise."""
    return (len(name) > 2 and
            name[0] == name[-1] == '_' and
            name[1:2] != '_' and
            name[-2:-1] != '_')


class StringIfsTestCase(PyjionTestCase):

    def test_sunder(self):
        self.assertTrue(_is_sunder("_hello_"))
        self.assertFalse(_is_sunder("helloooo!!_"))
        self.assertTrue(_is_sunder("_hello_"))
        self.assertFalse(_is_sunder("helloooo!!_"))

    def test_dunder(self):
        self.assertTrue(_is_dunder("__hello__"))
        self.assertFalse(_is_dunder("_hello_"))
        self.assertTrue(_is_dunder("__hello__"))
        self.assertFalse(_is_dunder("_hello_"))


if __name__ == "__main__":
    unittest.main()
