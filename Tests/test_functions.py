import pyjion
import unittest


class FunctionCallsTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()

    def test_arg0(self):
        def arg0() -> int:
            a = 1
            b = 2
            c = 3
            d = 4
            return a + b + c + d

        self.assertEqual(arg0(), 10)

    def test_arg1(self):
        def arg1(e):
            a = 1
            b = 2
            c = 3
            d = 4
            return a + b + c + d + e

        self.assertEqual(arg1(5), 15)

    def test_arg2(self):
        def arg2(e, f):
            a = 1
            b = 2
            c = 3
            d = 4
            return a + b + c + d + e + f

        self.assertEqual(arg2(5, 6), 21)

    def test_arg3(self):
        def arg3(e, f, g):
            a = 1
            b = 2
            c = 3
            d = 4
            return a + b + c + d + e + f + g

        self.assertEqual(arg3(5, 6, 7), 28)

    def test_arg4(self):
        def arg4(e, f, g, h):
            a = 1
            b = 2
            c = 3
            d = 4
            return a + b + c + d + e + f + g + h

        self.assertEqual(arg4(5, 6, 7, 8), 36)

    def test_arg5(self):
        def arg5(e, f, g, h, i):
            a = 1
            b = 2
            c = 3
            d = 4
            return a + b + c + d + e + f + g + h + i

        self.assertEqual(arg5(5, 6, 7, 8, 9), 45)

    def test_arg6(self):
        def arg6(e, f, g, h, i, j):
            a = 1
            b = 2
            c = 3
            d = 4
            return a + b + c + d + e + f + g + h + i + j

        self.assertEqual(arg6(5, 6, 7, 8, 9, 10), 55)

    def test_arg7(self):
        def arg7(e, f, g, h, i, j, k):
            a = 1
            b = 2
            c = 3
            d = 4
            return a + b + c + d + e + f + g + h + i + j + k

        self.assertEqual(arg7(5, 6, 7, 8, 9, 10, 11), 66)

    def test_arg8(self):
        def arg8(e, f, g, h, i, j, k, l):
            a = 1
            b = 2
            c = 3
            d = 4
            return a + b + c + d + e + f + g + h + i + j + k + l

        self.assertEqual(arg8(5, 6, 7, 8, 9, 10, 11, 12), 78)

    def test_arg9(self):
        def arg9(e, f, g, h, i, j, k, l, m):
            a = 1
            b = 2
            c = 3
            d = 4
            return a + b + c + d + e + f + g + h + i + j + k + l + m

        self.assertEqual(arg9(5, 6, 7, 8, 9, 10, 11, 12, 13), 91)

    def test_arg10(self):
        def arg10(e, f, g, h, i, j, k, l, m, n):
            a = 1
            b = 2
            c = 3
            d = 4
            return a + b + c + d + e + f + g + h + i + j + k + l + m + n

        self.assertEqual(arg10(5, 6, 7, 8, 9, 10, 11, 12, 13, 14), 105)

    def test_arg11(self):
        def arg11(e, f, g, h, i, j, k, l, m, n, o):
            a = 1
            b = 2
            c = 3
            d = 4
            return a + b + c + d + e + f + g + h + i + j + k + l + m + n + o

        self.assertEqual(arg11(5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15), 120)


if __name__ == "__main__":
    unittest.main()
