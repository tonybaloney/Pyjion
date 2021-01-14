import locale
import sys
sys.path.append('/Users/anthonyshaw/CLionProjects/pyjion/src')
import pyjion
import unittest
import os
import gc
from test.support import run_with_locale


class DictTypeTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_subclassdict(self):
        """Test that a subclass of dict can be merged into a static dict"""
        out = {**os.environ, 'foo': 'bar'}

        self.assertTrue('foo' in out)


class FormatTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    @run_with_locale('LC_NUMERIC', 'en_US.UTF8')
    def test_float__format__locale(self):
        # test locale support for __format__ code 'n'

        for i in range(-10, 10):
            x = 1234567890.0 * (10.0 ** i)
            self.assertEqual(locale.format_string('%g', x, grouping=True), format(x, 'n'))
            self.assertEqual(locale.format_string('%.10g', x, grouping=True), format(x, '.10n'))

    @run_with_locale('LC_NUMERIC', 'en_US.UTF8')
    def test_int__format__locale(self):
        # test locale support for __format__ code 'n' for integers

        x = 123456789012345678901234567890
        for i in range(0, 30):
            self.assertEqual(locale.format_string('%d', x, grouping=True), format(x, 'n'))

            # move to the next integer to test
            x = x // 10


if __name__ == "__main__":
    unittest.main()
