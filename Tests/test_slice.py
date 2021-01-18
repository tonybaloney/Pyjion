import pyjion
import unittest
import gc


class SliceTestCase(unittest.TestCase):
    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_list_slicing(self):
        l = [0, 1, 2, 3]
        self.assertEqual(l[0:1], [0])
        self.assertEqual(l[:1], [0])
        self.assertEqual(l[1:], [1, 2, 3])
        self.assertEqual(l[:], [0, 1, 2, 3])
        self.assertEqual(l[-2:-1], [2])
        self.assertEqual(l[-1:-2], [])
        self.assertEqual(l[-1:], [3])
        self.assertEqual(l[:-1], [0, 1, 2])
        self.assertEqual(l[0:1:], [0])
        self.assertEqual(l[0:1:1], [0])
        self.assertEqual(l[::1], [0, 1, 2, 3])
        self.assertEqual(l[::-1], [3, 2, 1, 0])
        self.assertEqual(l[::-2], [3, 1])
        self.assertEqual(l[::2], [0, 2])
        self.assertEqual([0, 1, 2, 3][False:True], [0])

    def test_list_slicing_expressions(self):
        l = [0, 1, 2, 3]
        x = int(2)  # prevent const rolling
        self.assertEqual(l[x + 1:0], [])
        self.assertEqual(l[:x-1], [0])
        self.assertEqual(l[x:x+1], [2])
        self.assertEqual(l[:x-4], [0, 1])


def test_string_slicing(self):
        self.assertEqual('The train to Oxford leaves at 3pm'[-1:3:-2], 'm3t ealdox tnat')


if __name__ == "__main__":
    unittest.main()
