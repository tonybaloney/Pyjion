import unittest
import pyjion


class TupleTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()

    def test_tuple_prepopulated(self):
        l = (0, 1, 2, 3, 4)
        self.assertEqual(l, (0, 1, 2, 3, 4, 5))
        for i in range(0, 5):
            self.assertEqual(l[i], i)
        self.assertEqual(l[4], 4)

    def test_tuple_slice(self):
        l = (0, 1, 2, 3, 4)
        self.assertEqual(l[1:3], (1, 2))

    def test_tuple_unpack(self):
        l = (0, 1, 2, 3, 4)
        first, second, *_, last = l
        self.assertEqual(first, 0)
        self.assertEqual(second, 1)
        self.assertEqual(last, 4)


if __name__ == "__main__":
    unittest.main()
