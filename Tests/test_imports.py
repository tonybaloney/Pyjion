import pyjion
import unittest


class RecursionTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()

    def test_import(self):
        def _f():
            import math
            return math.sqrt(4)

        self.assertEquals(_f(), 2)
        info = pyjion.info(_f)
        self.assertTrue(info['compiled'])

    def test_import_from(self):
        def _f():
            from math import sqrt
            return sqrt(4)

        self.assertEquals(_f(), 2)
        info = pyjion.info(_f)
        self.assertTrue(info['compiled'])

    def test_import_from_aliased(self):
        def _f():
            from math import sqrt as square_root
            return square_root(4)

        self.assertEquals(_f(), 2)
        info = pyjion.info(_f)
        self.assertTrue(info['compiled'])

if __name__ == "__main__":
    unittest.main()
