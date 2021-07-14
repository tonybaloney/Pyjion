from base import PyjionTestCase


class NumpyTestCase(PyjionTestCase):

    def test_array_math(self):
        import numpy as np
        t = np.array([250., 300., 350., 400.])
        v = 275.4
        x = 324.5

        lte = (t <= v)
        self.assertIsInstance(lte, np.ndarray)
        j = (t <= v) & (t >= x)
        self.assertIsInstance(j, np.ndarray)
        comp1 = j == np.array([False, False, False, False])
        self.assertIsInstance(comp1, np.ndarray)
        self.assertTrue(comp1.all())

        i = t <= v
        self.assertIsInstance(i, np.ndarray)
        comp2 = i == np.array([True, False, False, False])
        self.assertIsInstance(comp2, np.ndarray)
        self.assertTrue(comp2.all())
