import sys
import gc
import pyjion
import pyjion.dis
import dis
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
        initial_ref = sys.getrefcount(INF)
        initial_nan_ref = sys.getrefcount(NAN)

        def test(fmt, value, expected):
            # Test with both % and format().
            self.assertEqual(fmt % value, expected, fmt)
            fmt = fmt[1:] # strip off the %
            self.assertEqual(format(value, fmt), expected, fmt)

        for fmt in ['%e', '%f', '%g', '%.0e', '%.6f', '%.20g',
                    '%#e', '%#f', '%#g', '%#.20e', '%#.15f', '%#.3g']:
            fmt_ref = sys.getrefcount(fmt)

            pfmt = '%+' + fmt[1:]
            self.assertEqual(sys.getrefcount(fmt), fmt_ref, "format ref leak on slice")
            pfmt_ref = sys.getrefcount(pfmt)
            sfmt = '% ' + fmt[1:]
            sfmt_ref = sys.getrefcount(sfmt)
            self.assertEqual(sys.getrefcount(fmt), fmt_ref, "format ref leak on slice")
            test(fmt, INF, 'inf')
            self.assertEqual(sys.getrefcount(INF), initial_ref, "INF ref leak on first call")
            self.assertEqual(sys.getrefcount(fmt), fmt_ref, "format ref leak on first call")
            test(fmt, -INF, '-inf')
            self.assertEqual(sys.getrefcount(INF), initial_ref)
            self.assertEqual(sys.getrefcount(fmt), fmt_ref)
            test(fmt, NAN, 'nan')
            self.assertEqual(sys.getrefcount(NAN), initial_nan_ref)
            self.assertEqual(sys.getrefcount(fmt), fmt_ref)
            test(fmt, -NAN, 'nan')
            self.assertEqual(sys.getrefcount(NAN), initial_nan_ref)
            self.assertEqual(sys.getrefcount(fmt), fmt_ref)
            # When asking for a sign, it's always provided. nans are
            #  always positive.
            test(pfmt, INF, '+inf')
            self.assertEqual(sys.getrefcount(INF), initial_ref)
            self.assertEqual(sys.getrefcount(pfmt), pfmt_ref)
            test(pfmt, -INF, '-inf')
            self.assertEqual(sys.getrefcount(INF), initial_ref)
            self.assertEqual(sys.getrefcount(pfmt), pfmt_ref)
            test(pfmt, NAN, '+nan')
            self.assertEqual(sys.getrefcount(NAN), initial_nan_ref)
            self.assertEqual(sys.getrefcount(pfmt), pfmt_ref)
            test(pfmt, -NAN, '+nan')
            self.assertEqual(sys.getrefcount(NAN), initial_nan_ref)
            self.assertEqual(sys.getrefcount(pfmt), pfmt_ref)
            # When using ' ' for a sign code, only infs can be negative.
            #  Others have a space.
            test(sfmt, INF, ' inf')
            self.assertEqual(sys.getrefcount(INF), initial_ref)
            self.assertEqual(sys.getrefcount(sfmt), sfmt_ref)

            test(sfmt, -INF, '-inf')
            self.assertEqual(sys.getrefcount(INF), initial_ref)
            self.assertEqual(sys.getrefcount(sfmt), sfmt_ref)

            test(sfmt, NAN, ' nan')
            self.assertEqual(sys.getrefcount(NAN), initial_nan_ref)
            self.assertEqual(sys.getrefcount(sfmt), sfmt_ref)

            test(sfmt, -NAN, ' nan')
            self.assertEqual(sys.getrefcount(NAN), initial_nan_ref)
            self.assertEqual(sys.getrefcount(sfmt), sfmt_ref)
