import pyjion
import unittest
import gc
import sys


class SliceTestCase(unittest.TestCase):
    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_list_slicing(self):
        l = [0, 1, 2, 3]
        initial_ref = sys.getrefcount(l)
        self.assertEqual(l[0:1], [0])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual(l[:1], [0])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual(l[1:], [1, 2, 3])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual(l[:], [0, 1, 2, 3])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual(l[-2:-1], [2])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual(l[-1:-2], [])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual(l[-1:], [3])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual(l[:-1], [0, 1, 2])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual(l[0:1:], [0])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual(l[0:1:1], [0])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual(l[::1], [0, 1, 2, 3])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual(l[::-1], [3, 2, 1, 0])
        self.assertEqual(initial_ref, sys.getrefcount(l))
        n = l[::-1]
        self.assertEqual(sys.getrefcount(n), 2)

        m = l[:]
        self.assertEqual(sys.getrefcount(m), 2)

        self.assertEqual(l[::-2], [3, 1])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual(l[::2], [0, 2])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual([0, 1, 2, 3][False:True], [0])
        self.assertEqual(initial_ref, sys.getrefcount(l))
        pass

    def test_list_slicing_expressions(self):
        l = [0, 1, 2, 3]
        x = int(2)  # prevent const rolling
        initial_ref = sys.getrefcount(l)
        initial_ref_x = sys.getrefcount(x)
        self.assertEqual(l[x + 1:0], [])
        self.assertEqual(initial_ref, sys.getrefcount(l))
        self.assertEqual(initial_ref_x, sys.getrefcount(x))
        self.assertEqual(l[:x-1], [0])
        self.assertEqual(initial_ref, sys.getrefcount(l))
        self.assertEqual(initial_ref_x, sys.getrefcount(x))
        self.assertEqual(l[x:x+1], [2])
        self.assertEqual(initial_ref, sys.getrefcount(l))
        self.assertEqual(initial_ref_x, sys.getrefcount(x))
        self.assertEqual(l[:x-4], [0, 1])
        self.assertEqual(initial_ref, sys.getrefcount(l))
        self.assertEqual(initial_ref_x, sys.getrefcount(x))

    def test_string_slicing(self):
        self.assertEqual('The train to Oxford leaves at 3pm'[-1:3:-2], 'm3t ealdox tnat')


if __name__ == "__main__":
    unittest.main()
