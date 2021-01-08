import sys
import gc
import pyjion
import pyjion.dis
import unittest

INF = float("inf")
NAN = float("nan")


class FloatFormattingTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_format_specials(self):
        # Test formatting of nans and infs.

        def test(fmt, value, expected):
            # Test with both % and format().
            self.assertEqual(fmt % value, expected, fmt)
            fmt = fmt[1:] # strip off the %
            self.assertEqual(format(value, fmt), expected, fmt)

        for fmt in ['%e', '%f', '%g', '%.0e', '%.6f', '%.20g',
                    '%#e', '%#f', '%#g', '%#.20e', '%#.15f', '%#.3g']:
            pfmt = '%+' + fmt[1:]
            sfmt = '% ' + fmt[1:]
            test(fmt, INF, 'inf')
            test(fmt, -INF, '-inf')
            test(fmt, NAN, 'nan')
            test(fmt, -NAN, 'nan')
            # When asking for a sign, it's always provided. nans are
            #  always positive.
            test(pfmt, INF, '+inf')
            test(pfmt, -INF, '-inf')
            test(pfmt, NAN, '+nan')
            test(pfmt, -NAN, '+nan')
            # When using ' ' for a sign code, only infs can be negative.
            #  Others have a space.
            test(sfmt, INF, ' inf')
            test(sfmt, -INF, '-inf')
            test(sfmt, NAN, ' nan')
            test(sfmt, -NAN, ' nan')