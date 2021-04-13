import gc
import unittest

import pyjion


class MagicMethodsTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_add(self):
        class number:
            def __add__(self, other):
                return 4 + other

            def __radd__(self, other):
                return other + 4

        a = number()
        self.assertEqual(3 + a, 7)
        self.assertEqual(a + 3, 7)
