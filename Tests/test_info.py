import pyjion
import unittest
from base import PyjionTestCase


class JitInfoModuleTestCase(PyjionTestCase):

    def test_once(self):
        def test_f():
            a = 1
            b = 2
            c = 3
            d = 4
            return a+b+c+d

        self.assertTrue(test_f() == 10)
        info = pyjion.info(test_f)

        self.assertTrue(info['compiled'])
        self.assertFalse(info['failed'])
        self.assertEqual(info['run_count'], 1)

    def test_never(self):
        def test_f():
            a = 1
            b = 2
            c = 3
            d = 4
            return a+b+c+d
        info = pyjion.info(test_f)

        self.assertFalse(info['compiled'])
        self.assertFalse(info['failed'])
        self.assertEqual(info['run_count'], 0)

    def test_twice(self):
        def test_f():
            a = 1
            b = 2
            c = 3
            d = 4
            return a+b+c+d

        self.assertTrue(test_f() == 10)
        self.assertTrue(test_f() == 10)
        info = pyjion.info(test_f)

        self.assertTrue(info['compiled'])
        self.assertFalse(info['failed'])
        self.assertEqual(info['run_count'], 2)


if __name__ == "__main__":
    unittest.main()
