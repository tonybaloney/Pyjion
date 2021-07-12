import pyjion
import unittest
import gc


class NumpyTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.set_optimization_level(0)
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_array_math(self):
        import numpy as np
        t = np.array([250., 300., 350., 400.])
        v = 275.4
        x = 324.5

        j = (t <= v) & (t >= x)
        self.assertTrue((j == np.array([False, False, False, False])).all())

        i = t <= v
        self.assertTrue((i == np.array([True, False, False, False])).all())
