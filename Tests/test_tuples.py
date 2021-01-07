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

    def test_identical_tuples(self):
        l = ('0', '1')
        m = ('0', '1')
        self.assertEqual(sys.getrefcount(l), 4)
        self.assertEqual(sys.getrefcount(m), 4)

    def test_tuple_prepopulated(self):
        l = ('0', '1', '2', '3', '4')
        r = sys.getrefcount(l)
        self.assertEqual(sys.getrefcount(l), r)
        self.assertEqual(l, ('0', '1', '2', '3', '4'))
        self.assertEqual(sys.getrefcount(l), r)
        for i in range(0, 5):
            self.assertEqual(l[i], str(i))
        self.assertEqual(sys.getrefcount(l), r)
        self.assertEqual(l[4], '4')
        self.assertEqual(sys.getrefcount(l), r)

    def test_tuple_slice(self):
        l = ('0', '1', '2', '3', '4')
        r = sys.getrefcount(l)
        self.assertEqual(sys.getrefcount(l), r)
        self.assertEqual(l[1:3], ('1', '2'))
        self.assertEqual(sys.getrefcount(l), r)

    def test_tuple_unpack(self):
        l = ('0', '1', '2', '3', '4')
        r = sys.getrefcount(l)
        self.assertEqual(sys.getrefcount(l), r)
        first, second, *_, last = l
        self.assertEqual(sys.getrefcount(l), r)
        self.assertEqual(first, '0')
        self.assertEqual(second, '1')
        self.assertEqual(last, '4')
        self.assertEqual(sys.getrefcount(l), r)

    def test_unknown_type(self):
        def get_vals():
            return (0, 1, 2, 3)
        res = get_vals()
        r = sys.getrefcount(res)
        self.assertEqual(sys.getrefcount(res), r)
        self.assertEqual(res[0], 0)
        self.assertEqual(res[1], 1)
        self.assertEqual(res[2], 2)
        self.assertEqual(sys.getrefcount(res), r)

    def test_tuple_non_const(self):
        zero = str(0)
        r_zero = sys.getrefcount(zero)
        one = str(1)
        two = str(2)
        three = str(3)
        four = str(4)
        l = (zero, one, two, three, four)
        r = sys.getrefcount(l)
        self.assertEqual(sys.getrefcount(l), r)
        self.assertEqual(sys.getrefcount(zero), r_zero+1)
        first, second, *_, last = l
        self.assertEqual(sys.getrefcount(l), r)
        self.assertEqual(sys.getrefcount(zero), r_zero+2)
        self.assertEqual(first, '0')
        self.assertEqual(second, '1')
        self.assertEqual(last, '4')
        self.assertEqual(sys.getrefcount(l), r)
        self.assertEqual(sys.getrefcount(zero), r_zero+2)


if __name__ == "__main__":
    unittest.main()
