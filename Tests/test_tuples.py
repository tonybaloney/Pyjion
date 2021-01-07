import unittest
import pyjion
import gc
import sys


class TupleTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_tuple_prepopulated(self):
        l = (0, 1, 2, 3, 4)
        self.assertEqual(sys.getrefcount(l), 5)
        self.assertEqual(l, (0, 1, 2, 3, 4))
        self.assertEqual(sys.getrefcount(l), 5)
        for i in range(0, 5):
            self.assertEqual(l[i], i)
        self.assertEqual(sys.getrefcount(l), 5)
        self.assertEqual(l[4], 4)
        self.assertEqual(sys.getrefcount(l), 5)

    def test_tuple_slice(self):
        l = (0, 1, 2, 3, 4)
        self.assertEqual(sys.getrefcount(l), 5)
        self.assertEqual(l[1:3], (1, 2))
        self.assertEqual(sys.getrefcount(l), 5)

    def test_tuple_unpack(self):
        l = (0, 1, 2, 3, 4)
        self.assertEqual(sys.getrefcount(l), 5)
        first, second, *_, last = l
        self.assertEqual(sys.getrefcount(l), 5)
        self.assertEqual(first, 0)
        self.assertEqual(second, 1)
        self.assertEqual(last, 4)
        self.assertEqual(sys.getrefcount(l), 5)

    def test_unknown_type(self):
        def get_vals():
            return (0, 1, 2, 3)
        res = get_vals()
        self.assertEqual(sys.getrefcount(res), 3)
        self.assertEqual(res[0], 0)
        self.assertEqual(res[1], 1)
        self.assertEqual(res[2], 2)
        self.assertEqual(sys.getrefcount(res), 3)


if __name__ == "__main__":
    unittest.main()
