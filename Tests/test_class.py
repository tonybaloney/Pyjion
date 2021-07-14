from test_base import PyjionTestCase


class MagicMethodsTestCase(PyjionTestCase):

    def test_add(self):
        class number:
            def __add__(self, other):
                return 4 + other

            def __radd__(self, other):
                return other + 4

        a = number()
        self.assertEqual(3 + a, 7)
        self.assertEqual(a + 3, 7)
