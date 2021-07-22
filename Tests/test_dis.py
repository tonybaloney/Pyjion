from pyjion.dis import print_il, dis, dis_native
import pyjion
import unittest
import io
import contextlib
import sys
from base import DebugPyjionTestCase


class DisassemblerModuleTestCase(DebugPyjionTestCase):

    def test_offsets(self):
        def test_f(x):
            return x/2

        self.assertTrue(test_f(4) == 2.0)

        offsets = pyjion.get_offsets(test_f)
        self.assertTrue(len(offsets) > 10)

    def test_dis(self):
        def test_f():
            numbers = (1, 2, 3, 4)
            return sum(numbers)

        self.assertTrue(test_f() == 10)
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            dis(test_f)
        self.assertIn("ldarg.1", f.getvalue())

    def test_dis_with_offsets(self):
        def test_f():
            numbers = (1, 2, 3, 4)
            return sum(numbers)

        self.assertTrue(test_f() == 10)
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            dis(test_f, True)
        self.assertIn("ldarg.1", f.getvalue())
        self.assertIn("// 0 LOAD_CONST - 1 ((1, 2, 3, 4))", f.getvalue())

    def test_dis_with_no_pc(self):
        def test_f():
            numbers = (1, 2, 3, 4)
            return sum(numbers)

        self.assertTrue(test_f() == 10)
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            dis(test_f, False, False)
        self.assertIn("ldarg.1", f.getvalue())

    def test_fat_static(self):
        test_method = bytearray(b'\x03 h\x00\x00\x00\xd3X\n\x03(A\x00\x00\x00\x16\r!0\x19Rc\xd1\x7f\x00\x00\xd3% \x00\x00\x00\x00\xd3X%J\x17XT\x06\x18T\x13\n\x03 h\x01\x00\x00\xd3XM\x03 h\x01\x00\x00\xd3X\x11\n\xdf(\x10\x00\x00\x00!P\x19Rc\xd1\x7f\x00\x00\xd3% \x00\x00\x00\x00\xd3X%J\x17XT\x06\x1cT\x13\n\x03 p\x01\x00\x00\xd3XM\x03 p\x01\x00\x00\xd3X\x11\n\xdf(\x10\x00\x00\x00!p\x19Rc\xd1\x7f\x00\x00\xd3% \x00\x00\x00\x00\xd3X%J\x17XT\x06\x1f\nT\x13\n\x03 x\x01\x00\x00\xd3XM\x03 x\x01\x00\x00\xd3X\x11\n\xdf(\x10\x00\x00\x00!\x90\x19Rc\xd1\x7f\x00\x00\xd3% \x00\x00\x00\x00\xd3X%J\x17XT\x06\x1f\x0eT\x13\n\x03 \x80\x01\x00\x00\xd3XM\x03 \x80\x01\x00\x00\xd3X\x11\n\xdf(\x10\x00\x00\x00\x06\x1f\x10T\x03 h\x01\x00\x00\xd3XM%\x0c\x16\xd3@\x1a\x00\x00\x00!0 nc\xd1\x7f\x00\x00\xd3(:\x00\x00\x00\x03(8\x00\x00\x008G\x01\x00\x00\x08% \x00\x00\x00\x00\xd3X%J\x17XT\x06\x1f\x12T\x03 p\x01\x00\x00\xd3XM%\x0c\x16\xd3@\x1c\x00\x00\x00!\xf0\xbeac\xd1\x7f\x00\x00\xd3(:\x00\x00\x00\x03(8\x00\x00\x00\x13\x0b8\x07\x01\x00\x00\x08% \x00\x00\x00\x00\xd3X%J\x17XT\x06\x1f\x14T(\x00\x00\x00\x00%\x0c\x16\xd3@\x0b\x00\x00\x00\x03(8\x00\x00\x008\xdc\x00\x00\x00\x08\x06\x1f\x16T\x03 x\x01\x00\x00\xd3XM%\x0c\x16\xd3@\x1c\x00\x00\x00!\xb0\x8c]c\xd1\x7f\x00\x00\xd3(:\x00\x00\x00\x03(8\x00\x00\x00\x13\x0b8\xa9\x00\x00\x00\x08% \x00\x00\x00\x00\xd3X%J\x17XT\x06\x1f\x18T(\x00\x00\x00\x00%\x0c\x16\xd3@\x0b\x00\x00\x00\x03(8\x00\x00\x008~\x00\x00\x00\x08\x06\x1f\x1aT\x03 \x80\x01\x00\x00\xd3XM%\x0c\x16\xd3@\x1c\x00\x00\x00!\xb0\x8b]c\xd1\x7f\x00\x00\xd3(:\x00\x00\x00\x03(8\x00\x00\x00\x13\x0b8K\x00\x00\x00\x08% \x00\x00\x00\x00\xd3X%J\x17XT\x06\x1f\x1cT(\x00\x00\x00\x00%\x0c\x16\xd3@\x0b\x00\x00\x00\x03(8\x00\x00\x008 \x00\x00\x00\x08\x06\x1f\x1eT\x0b8\x1c\x00\x00\x00\t\x16>\t\x00\x00\x00&&&\t\x19\xda\r+\xf08\x00\x00\x00\x00\x16\xd38\x01\x00\x00\x00\x07\x03(B\x00\x00\x00*')
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            print_il(test_method, symbols={})
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
            print_il(test_method, symbols={})
        self.assertIn("ldarg.1", f.getvalue())

    @unittest.skipIf(sys.platform.startswith("win"), "no windows support yet")
    def test_dis_native(self):
        def test_f():
            numbers = (1, 2, 3, 4)
            return sum(numbers)

        self.assertTrue(test_f() == 10)
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            dis_native(test_f)
        self.assertIn("PUSH RBP", f.getvalue())

    @unittest.skipIf(sys.platform.startswith("win"), "no windows support yet")
    def test_dis_native_with_offsets(self):
        def test_f():
            numbers = (1, 2, 3, 4)
            return sum(numbers)

        self.assertTrue(test_f() == 10)
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            dis_native(test_f, True)
        print(f.getvalue())
        self.assertIn("PUSH RBP", f.getvalue())
        self.assertIn("; 10 RETURN_VALUE - None (None)", f.getvalue())
        self.assertIn("; METHOD_PYTUPLE_NEW", f.getvalue())

    def test_symbols(self):
        def test_f():
            numbers = (1, 2, 3, 4)
            return sum(numbers)
        self.assertTrue(test_f() == 10)
        symbols = pyjion.symbols(test_f)
        self.assertNotEqual(len(symbols), 0)
        names = list(symbols.values())
        self.assertIn("METHOD_SUBSCR_LIST_SLICE_REVERSED", names)


if __name__ == "__main__":
    unittest.main()
