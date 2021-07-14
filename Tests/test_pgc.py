import pyjion
import pyjion.dis
import unittest
import gc


class UnpackSequenceTest(unittest.TestCase):
    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_static_sequence(self):
        def f(x):
            a, b = x
            return a + b

        f([0, 1])
        print(pyjion.dis.dis(f))
        self.assertEqual(pyjion.info(f)['pgc'], 1)

        f([0, 1])
        print(pyjion.dis.dis(f))
        self.assertEqual(pyjion.info(f)['pgc'], 2)

    def test_changed_sequence(self):
        def f(x):
            a, b = x
            return a + b

        r = f([1, 2])
        self.assertEqual(r, 3)
        self.assertEqual(pyjion.info(f)['pgc'], 1)
        r = f((3, 4))
        self.assertEqual(r, 7)
        self.assertEqual(pyjion.info(f)['pgc'], 2)
        r = f([3, 4])
        self.assertEqual(r, 7)
        self.assertEqual(pyjion.info(f)['pgc'], 2)

    def test_recursive_sequence(self):
        def f(x):
            a, b = x
            if isinstance(x, list):
                f(tuple(x))
            a, b = x
            return a, b

        r = f([1, 2])
        self.assertEqual(r, (1, 2))
        self.assertEqual(pyjion.info(f)['pgc'], 2)
        r = f([3, 4])
        self.assertEqual(r, (3, 4))
        self.assertEqual(pyjion.info(f)['pgc'], 2)
        r = f((3, 4))
        self.assertEqual(r, (3, 4))
        self.assertEqual(pyjion.info(f)['pgc'], 2)