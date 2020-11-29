from pyjion.dis import print_il, dis
import pyjion
import unittest
import io
import contextlib


class DisassemblerModuleTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()

    def test_fat(self):
        def test_f():
            a = 1
            b = 2
            c = 3
            d = 4
            return a+b+c+d

        self.assertTrue(test_f() == 10)
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            dis(test_f)
        self.assertIn("ldarg.1", f.getvalue())

    def test_thin(self):
        test_method = bytearray(b'\x03 h\x00\x00\x00\xd3X\n\x03(A\x00\x00\x00\x16\r\x06 '
                                b'\x00\x00\x00\x00\xd3T\x03!\xb0\xc6V)\x91\x7f\x00\x00\xd3('
                                b'\x00\x00\x03\x00%\x0c\x16\xd3@\x0b\x00\x00\x00\x03('
                                b'8\x00\x00\x008\x91\x00\x00\x00\x08\x06 '
                                b'\x02\x00\x00\x00\xd3T!\xf0\xc3\x13*\x91\x7f\x00\x00\xd3% '
                                b'\x00\x00\x00\x00\xd3X%J\x17XT\x06 \x04\x00\x00\x00\xd3T('
                                b'\x01\x00\x01\x00%\x0c\x16\xd3@\x0b\x00\x00\x00\x03('
                                b'8\x00\x00\x008P\x00\x00\x00\x08\x06 \x06\x00\x00\x00\xd3T(\x10\x00\x00\x00\x06 '
                                b'\x08\x00\x00\x00\xd3T!\xe0\x1e\xda\x02\x01\x00\x00\x00\xd3% '
                                b'\x00\x00\x00\x00\xd3X%J\x17XT\x06 '
                                b'\n\x00\x00\x00\xd3T\x0b\xdd\x1c\x00\x00\x00\t\x16>\t\x00\x00\x00&&&\x19\tY\r+\xf08'
                                b'\x00\x00\x00\x00\x16\xd38\x01\x00\x00\x00\x07\x03(B\x00\x00\x00*')
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            print_il(test_method)
        self.assertIn("ldarg.1", f.getvalue())


if __name__ == "__main__":
    unittest.main()
