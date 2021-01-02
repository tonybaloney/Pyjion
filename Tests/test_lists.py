import pyjion
import unittest



class ListTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()

    def test_list_init(self):
        l = []
        l.append(0)
        self.assertEqual(l, [0])

    def test_list_prepopulated(self):
        l = [0, 1, 2, 3, 4]
        l.append(5)
        self.assertEqual(l, [0, 1, 2, 3, 4, 5])

    def test_list_slice(self):
        l = [0, 1, 2, 3, 4]
        self.assertEqual(l[1:3], [1, 2])


if __name__ == "__main__":
    unittest.main()
