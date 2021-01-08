import gc
import sys
import pyjion
import pyjion.dis
import unittest
import math
import time


class ScopeLeaksTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_slice(self):
        a = "12345"

        def x(a):
            a = a[1:]
        before = sys.getrefcount(a)
        x(a)
        self.assertEqual(before, sys.getrefcount(a))

    def test_inplace_operator(self):
        a = "12345"

        def x(a):
            a += a
        before = sys.getrefcount(a)
        x(a)
        self.assertEqual(before, sys.getrefcount(a), pyjion.dis.dis(x))

class FunctionCallsTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_arg0(self):
        def arg0() -> int:
            a = 1
            b = 2
            c = 3
            d = 4
            return a + b + c + d

        self.assertEqual(sys.getrefcount(arg0), 2)
        self.assertEqual(arg0(), 10)
        self.assertEqual(sys.getrefcount(arg0), 2)
        info = pyjion.info(arg0)
        self.assertTrue(info['compiled'])

    def test_arg0_cfunction(self):
        target = time.time
        pre_ref_cnt = sys.getrefcount(target)
        self.assertIsNotNone(target())
        self.assertEqual(sys.getrefcount(target), pre_ref_cnt)
        info = pyjion.info(self.test_arg0_cfunction.__code__)
        self.assertTrue(info['compiled'])

    def test_arg0_exc(self):
        def arg0() -> int:
            raise ValueError

        self.assertEqual(sys.getrefcount(arg0), 2)
        with self.assertRaises(ValueError):
            arg0()
        self.assertEqual(sys.getrefcount(arg0), 2)
        info = pyjion.info(arg0)
        self.assertTrue(info['compiled'])

    def test_arg0_cfunction_exc(self):
        target = math.sqrt
        pre_ref_cnt = sys.getrefcount(target)
        with self.assertRaises(TypeError):
            target()
        self.assertEqual(sys.getrefcount(target), pre_ref_cnt)
        info = pyjion.info(self.test_arg0_cfunction_exc.__code__)
        self.assertTrue(info['compiled'])

    def test_arg1(self):
        def arg1(e):
            a = '1'
            b = '2'
            c = '3'
            d = '4'
            return a + b + c + d + e

        a = '5'
        pre_ref = sys.getrefcount(a)
        self.assertEqual(sys.getrefcount(arg1), 2)
        self.assertEqual(arg1(a), '12345')
        self.assertEqual(sys.getrefcount(arg1), 2)
        self.assertEqual(sys.getrefcount(a), pre_ref)

        info = pyjion.info(arg1)
        self.assertTrue(info['compiled'])

    def test_arg1_cfunction(self):
        target = math.sqrt
        four = 4
        pre_ref_cnt = sys.getrefcount(target)
        arg1_pre_ref_cnt = sys.getrefcount(four)
        self.assertEqual(target(four), 2)
        self.assertEqual(sys.getrefcount(target), pre_ref_cnt)
        self.assertEqual(sys.getrefcount(four), arg1_pre_ref_cnt)
        info = pyjion.info(self.test_arg1_cfunction.__code__)
        self.assertTrue(info['compiled'])

    def test_arg1_exc(self):
        def arg1(e):
            raise ValueError

        a = '5'
        pre_ref = sys.getrefcount(a)
        self.assertEqual(sys.getrefcount(arg1), 2)
        with self.assertRaises(ValueError):
            arg1(a)
        self.assertEqual(sys.getrefcount(arg1), 2)
        self.assertEqual(sys.getrefcount(a), pre_ref)

        info = pyjion.info(arg1)
        self.assertTrue(info['compiled'])

    def test_arg1_cfunction_exc(self):
        target = math.sqrt
        four = 'four'
        pre_ref_cnt = sys.getrefcount(target)
        arg1_pre_ref_cnt = sys.getrefcount(four)
        with self.assertRaises(TypeError):
            target(four)
        self.assertEqual(sys.getrefcount(target), pre_ref_cnt)
        self.assertEqual(sys.getrefcount(four), arg1_pre_ref_cnt)
        info = pyjion.info(self.test_arg1_cfunction_exc.__code__)
        self.assertTrue(info['compiled'])

    def test_arg2(self):
        def arg2(e, f):
            a = '1'
            b = '2'
            c = '3'
            d = '4'
            return a + b + c + d + e + f

        a = '5'
        b = '6'
        pre_ref_a = sys.getrefcount(a)
        pre_ref_b = sys.getrefcount(b)
        self.assertEqual(sys.getrefcount(arg2), 2)
        self.assertEqual(arg2(a, b), '123456')
        self.assertEqual(sys.getrefcount(arg2), 2)
        self.assertEqual(sys.getrefcount(a), pre_ref_a)
        self.assertEqual(sys.getrefcount(b), pre_ref_b)

        info = pyjion.info(arg2)
        self.assertTrue(info['compiled'])

    def test_arg3(self):
        def arg3(e, f, g):
            a = '1'
            b = '2'
            c = '3'
            d = '4'
            return a + b + c + d + e + f + g

        self.assertEqual(arg3('5', '6', '7'), '1234567')
        info = pyjion.info(arg3)
        self.assertTrue(info['compiled'])

    def test_arg4(self):
        def arg4(e, f, g, h):
            a = 1
            b = 2
            c = 3
            d = 4
            return a + b + c + d + e + f + g + h

        self.assertEqual(arg4(5, 6, 7, 8), 36)
        info = pyjion.info(arg4)
        self.assertTrue(info['compiled'])

    def test_arg5(self):
        def arg5(e, f, g, h, i):
            a = 1
            b = 2
            c = 3
            d = 4
            return a + b + c + d + e + f + g + h + i

        self.assertEqual(arg5(5, 6, 7, 8, 9), 45)
        info = pyjion.info(arg5)
        self.assertTrue(info['compiled'])

    def test_arg6(self):
        def arg6(e, f, g, h, i, j):
            a = 1
            b = 2
            c = 3
            d = 4
            return a + b + c + d + e + f + g + h + i + j

        self.assertEqual(arg6(5, 6, 7, 8, 9, 10), 55)
        info = pyjion.info(arg6)
        self.assertTrue(info['compiled'])

    def test_arg7(self):
        def arg7(e, f, g, h, i, j, k):
            a = 1
            b = 2
            c = 3
            d = 4
            return a + b + c + d + e + f + g + h + i + j + k

        self.assertEqual(arg7(5, 6, 7, 8, 9, 10, 11), 66)
        info = pyjion.info(arg7)
        self.assertTrue(info['compiled'])

    def test_arg8(self):
        def arg8(e, f, g, h, i, j, k, l):
            a = 1
            b = 2
            c = 3
            d = 4
            return a + b + c + d + e + f + g + h + i + j + k + l

        self.assertEqual(arg8(5, 6, 7, 8, 9, 10, 11, 12), 78)
        info = pyjion.info(arg8)
        self.assertTrue(info['compiled'])

    def test_arg9(self):
        def arg9(e, f, g, h, i, j, k, l, m):
            a = 1
            b = 2
            c = 3
            d = 4
            return a + b + c + d + e + f + g + h + i + j + k + l + m

        self.assertEqual(arg9(5, 6, 7, 8, 9, 10, 11, 12, 13), 91)
        info = pyjion.info(arg9)
        self.assertTrue(info['compiled'])

    def test_arg10(self):
        def arg10(e, f, g, h, i, j, k, l, m, n):
            a = 1
            b = 2
            c = 3
            d = 4
            return a + b + c + d + e + f + g + h + i + j + k + l + m + n

        self.assertEqual(arg10(5, 6, 7, 8, 9, 10, 11, 12, 13, 14), 105)
        info = pyjion.info(arg10)
        self.assertTrue(info['compiled'])

    def test_arg11(self):
        def arg11(e, f, g, h, i, j, k, l, m, n, o):
            a = 1
            b = 2
            c = 3
            d = 4
            return a + b + c + d + e + f + g + h + i + j + k + l + m + n + o

        self.assertEqual(arg11(5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15), 120)
        info = pyjion.info(arg11)
        self.assertTrue(info['compiled'])

    def test_arg15(self):
        def arg15(e, f, g, h, i, j, k, l, m, n, o, p, q, r, s):
            a = '1'
            b = '2'
            c = '3'
            d = '4'
            return a + b + c + d + e + f + g + h + i + j + k + l + m + n + o

        a = '5'
        b = '6'
        pre_ref_a = sys.getrefcount(a)
        pre_ref_b = sys.getrefcount(b)
        self.assertEqual(sys.getrefcount(arg15), 2)
        self.assertEqual(arg15(a, b, '7', '8', '9', '10', '11', '12', '13', '14', '15', '16', '17', '18', '19'), '123456789101112131415')
        self.assertEqual(sys.getrefcount(arg15), 2)
        self.assertEqual(sys.getrefcount(a), pre_ref_a)
        self.assertEqual(sys.getrefcount(b), pre_ref_b)
        info = pyjion.info(arg15)
        self.assertTrue(info['compiled'])

    def test_arg15_cfunction(self):
        a = 500
        b = 600
        target = math.hypot
        pre_ref_target = sys.getrefcount(target)
        pre_ref_a = sys.getrefcount(a)
        pre_ref_b = sys.getrefcount(b)
        self.assertEqual(target(a, b, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19), 782.54648424231)
        self.assertEqual(sys.getrefcount(target), pre_ref_target)
        self.assertEqual(sys.getrefcount(a), pre_ref_a)
        self.assertEqual(sys.getrefcount(b), pre_ref_b)
        info = pyjion.info(self.test_arg15_cfunction.__code__)
        self.assertTrue(info['compiled'])

    def test_arg15_exc(self):
        def arg15(e, f, g, h, i, j, k, l, m, n, o, p, q, r, s):
            raise ValueError

        a = '5'
        b = '6'
        pre_ref_a = sys.getrefcount(a)
        pre_ref_b = sys.getrefcount(b)
        self.assertEqual(sys.getrefcount(arg15), 2)
        with self.assertRaises(ValueError):
            arg15(a, b, '7', '8', '9', '10', '11', '12', '13', '14', '15', '16', '17', '18', '19')
        self.assertEqual(sys.getrefcount(arg15), 2)
        self.assertEqual(sys.getrefcount(a), pre_ref_a)
        self.assertEqual(sys.getrefcount(b), pre_ref_b)
        info = pyjion.info(arg15)
        self.assertTrue(info['compiled'])

    def test_arg15_cfunction_exc(self):
        a = '5'
        b = '6'
        target = any
        pre_ref_a = sys.getrefcount(a)
        pre_ref_b = sys.getrefcount(b)
        pre_ref_target = sys.getrefcount(target)
        with self.assertRaises(TypeError):
            target(a, b, '7', '8', '9', '10', '11', '12', '13', '14', '15', '16', '17', '18', '19')
        self.assertEqual(sys.getrefcount(target), pre_ref_target)
        self.assertEqual(sys.getrefcount(a), pre_ref_a)
        self.assertEqual(sys.getrefcount(b), pre_ref_b)
        info = pyjion.info(self.test_arg15_cfunction_exc.__code__)
        self.assertTrue(info['compiled'])


class ClassMethodCallsTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()

    def test_arg0(self):
        class F:
            @classmethod
            def arg0(cls) -> int:
                a = 1
                b = 2
                c = 3
                d = 4
                return a + b + c + d

        self.assertEqual(sys.getrefcount(F.arg0), 1)
        self.assertEqual(F.arg0(), 10)
        self.assertEqual(sys.getrefcount(F.arg0), 1)
        info = pyjion.info(F.arg0.__code__)
        self.assertTrue(info['compiled'])

    def test_arg0_exc(self):
        class F:
            @classmethod
            def arg0(cls) -> int:
                raise ValueError

        self.assertEqual(sys.getrefcount(F.arg0), 1)
        with self.assertRaises(ValueError):
            F.arg0()
        self.assertEqual(sys.getrefcount(F.arg0), 1)
        info = pyjion.info(F.arg0.__code__)
        self.assertTrue(info['compiled'])

    def test_arg1(self):
        class F:
            @classmethod
            def arg1(cls, e):
                a = '1'
                b = '2'
                c = '3'
                d = '4'
                return a + b + c + d + e

        arg_a = 'e'
        pre_ref_cnt = sys.getrefcount(arg_a)
        self.assertEqual(sys.getrefcount(F), 5)
        self.assertEqual(sys.getrefcount(F.arg1), 1)
        self.assertEqual(F.arg1(arg_a), '1234e')
        self.assertEqual(sys.getrefcount(arg_a), pre_ref_cnt)
        self.assertEqual(sys.getrefcount(F), 5)
        self.assertEqual(sys.getrefcount(F.arg1), 1)

        info = pyjion.info(F.arg1.__code__)
        self.assertTrue(info['compiled'])

    def test_arg1_cfunction(self):
        arg_a = 'jeremy'
        target = str.title
        arg_a_pre_ref = sys.getrefcount(arg_a)
        target_pre_ref = sys.getrefcount(target)
        target(arg_a)
        self.assertEqual(sys.getrefcount(target), target_pre_ref)
        self.assertEqual(sys.getrefcount(arg_a), arg_a_pre_ref)
        info = pyjion.info(self.test_arg1_cfunction.__code__)
        self.assertTrue(info['compiled'])

    def test_arg1_cfunction_exc(self):
        arg_a = 50000
        target = str.title
        arg_a_pre_ref = sys.getrefcount(arg_a)
        target_pre_ref = sys.getrefcount(target)
        with self.assertRaises(TypeError):
            target(arg_a)
        self.assertEqual(sys.getrefcount(target), target_pre_ref)
        self.assertEqual(sys.getrefcount(arg_a), arg_a_pre_ref)
        info = pyjion.info(self.test_arg1_cfunction_exc.__code__)
        self.assertTrue(info['compiled'])

    def test_arg1_exc(self):
        class F:
            @classmethod
            def arg1(cls, e):
                raise ValueError

        arg_a = 'e'
        pre_ref_cnt = sys.getrefcount(arg_a)
        self.assertEqual(sys.getrefcount(F), 5)
        self.assertEqual(sys.getrefcount(F.arg1), 1)
        with self.assertRaises(ValueError):
            F.arg1(arg_a)
        self.assertEqual(sys.getrefcount(arg_a), pre_ref_cnt)
        self.assertEqual(sys.getrefcount(F), 5)
        self.assertEqual(sys.getrefcount(F.arg1), 1)

        info = pyjion.info(F.arg1.__code__)
        self.assertTrue(info['compiled'])

    def test_arg2(self):
        class F:
            @classmethod
            def arg2(cls, e, f):
                a = 1
                b = 2
                c = 3
                d = 4
                return a + b + c + d + e + f

        self.assertEqual(F.arg2(5, 6), 21)
        info = pyjion.info(F.arg2.__code__)
        self.assertTrue(info['compiled'])

    def test_arg3(self):
        class F:
            @classmethod
            def arg3(cls, e, f, g):
                a = 1
                b = 2
                c = 3
                d = 4
                return a + b + c + d + e + f + g

        self.assertEqual(F.arg3(5, 6, 7), 28)
        info = pyjion.info(F.arg3.__code__)
        self.assertTrue(info['compiled'])

    def test_arg4(self):
        class F:
            @classmethod
            def arg4(cls, e, f, g, h):
                a = 1
                b = 2
                c = 3
                d = 4
                return a + b + c + d + e + f + g + h

        self.assertEqual(F.arg4(5, 6, 7, 8), 36)
        info = pyjion.info(F.arg4.__code__)
        self.assertTrue(info['compiled'])

    def test_arg5(self):
        class F:
            @classmethod
            def arg5(cls, e, f, g, h, i):
                a = 1
                b = 2
                c = 3
                d = 4
                return a + b + c + d + e + f + g + h + i

        self.assertEqual(F.arg5(5, 6, 7, 8, 9), 45)
        info = pyjion.info(F.arg5.__code__)
        self.assertTrue(info['compiled'])

    def test_arg6(self):
        class F:
            @classmethod
            def arg6(cls, e, f, g, h, i, j):
                a = 1
                b = 2
                c = 3
                d = 4
                return a + b + c + d + e + f + g + h + i + j

        self.assertEqual(F.arg6(5, 6, 7, 8, 9, 10), 55)
        info = pyjion.info(F.arg6.__code__)
        self.assertTrue(info['compiled'])

    def test_arg7(self):
        class F:
            @classmethod
            def arg7(cls, e, f, g, h, i, j, k):
                a = 1
                b = 2
                c = 3
                d = 4
                return a + b + c + d + e + f + g + h + i + j + k

        self.assertEqual(F.arg7(5, 6, 7, 8, 9, 10, 11), 66)
        info = pyjion.info(F.arg7.__code__)
        self.assertTrue(info['compiled'])

    def test_arg8(self):
        class F:
            @classmethod
            def arg8(cls, e, f, g, h, i, j, k, l):
                a = 1
                b = 2
                c = 3
                d = 4
                return a + b + c + d + e + f + g + h + i + j + k + l

        self.assertEqual(F.arg8(5, 6, 7, 8, 9, 10, 11, 12), 78)
        info = pyjion.info(F.arg8.__code__)
        self.assertTrue(info['compiled'])

    def test_arg9(self):
        class F:
            @classmethod
            def arg9(cls, e, f, g, h, i, j, k, l, m):
                a = 1
                b = 2
                c = 3
                d = 4
                return a + b + c + d + e + f + g + h + i + j + k + l + m

        self.assertEqual(F.arg9(5, 6, 7, 8, 9, 10, 11, 12, 13), 91)
        info = pyjion.info(F.arg9.__code__)
        self.assertTrue(info['compiled'])

    def test_arg10(self):
        class F:
            @classmethod
            def arg10(cls, e, f, g, h, i, j, k, l, m, n):
                a = 1
                b = 2
                c = 3
                d = 4
                return a + b + c + d + e + f + g + h + i + j + k + l + m + n

        self.assertEqual(F.arg10(5, 6, 7, 8, 9, 10, 11, 12, 13, 14), 105)
        info = pyjion.info(F.arg10.__code__)
        self.assertTrue(info['compiled'])

    def test_arg11(self):
        class F:
            @classmethod
            def arg11(cls, e, f, g, h, i, j, k, l, m, n, o):
                a = 1
                b = 2
                c = 3
                d = 4
                return a + b + c + d + e + f + g + h + i + j + k + l + m + n + o

        self.assertEqual(F.arg11(5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15), 120)
        info = pyjion.info(F.arg11.__code__)
        self.assertTrue(info['compiled'])

    def test_arg15(self):
        class F:
            @classmethod
            def arg15(cls, e, f, g, h, i, j, k, l, m, n, o, p ,q ,r,s):
                a = 1
                b = 2
                c = 3
                d = 4
                return a + b + c + d + e + f + g + h + i + j + k + l + m + n + o

        a = 10000
        pre_ref_cnt = sys.getrefcount(a)
        pre_target_cnt = sys.getrefcount(F.arg15)
        self.assertEqual(F.arg15(a, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19), 10115)
        self.assertEqual(sys.getrefcount(a), pre_ref_cnt)
        self.assertEqual(sys.getrefcount(F.arg15), pre_target_cnt)
        info = pyjion.info(F.arg15.__code__)
        self.assertTrue(info['compiled'])

    def test_arg15_cfunction_exc(self):
        target = str.strip
        a = '  aa  '
        pre_ref_cnt = sys.getrefcount(a)
        pre_target_cnt = sys.getrefcount(target)
        with self.assertRaises(TypeError):
            target(a, '6', '7', '8', '9', '10', '11', '12', '13', '14', '15', '16', '17', '18', '19')
        self.assertEqual(sys.getrefcount(a), pre_ref_cnt)
        self.assertEqual(sys.getrefcount(target), pre_target_cnt)
        info = pyjion.info(self.test_arg15_cfunction_exc.__code__)
        self.assertTrue(info['compiled'])

    def test_arg15_exc(self):
        class F:
            @classmethod
            def arg15(cls, e, f, g, h, i, j, k, l, m, n, o, p ,q ,r,s):
                raise ValueError

        a = '1'
        pre_ref_cnt = sys.getrefcount(a)
        pre_target_cnt = sys.getrefcount(F.arg15)
        with self.assertRaises(ValueError):
            F.arg15(a, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19)
        self.assertEqual(sys.getrefcount(a), pre_ref_cnt)
        self.assertEqual(sys.getrefcount(F.arg15), pre_target_cnt)
        info = pyjion.info(F.arg15.__code__)
        self.assertTrue(info['compiled'])


class FunctionKwCallsTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_arg1(self):
        def arg1(e):
            a = '1'
            b = '2'
            c = '3'
            d = '4'
            return a + b + c + d + e

        a = '5'
        pre_ref = sys.getrefcount(a)
        self.assertEqual(sys.getrefcount(arg1), 2)
        self.assertEqual(arg1(e=a), '12345')
        self.assertEqual(sys.getrefcount(arg1), 2)
        self.assertEqual(sys.getrefcount(a), pre_ref)

        info = pyjion.info(arg1)
        self.assertTrue(info['compiled'])

    def test_arg1_unpack_tuple(self):
        def arg1(e):
            a = '1'
            b = '2'
            c = '3'
            d = '4'
            return a + b + c + d + e

        args = ('5',)
        pre_ref = sys.getrefcount(args)
        self.assertEqual(sys.getrefcount(arg1), 2)
        self.assertEqual(arg1(*args), '12345')
        self.assertEqual(sys.getrefcount(arg1), 2)
        self.assertEqual(sys.getrefcount(args), pre_ref)

        info = pyjion.info(arg1)
        self.assertTrue(info['compiled'])

    def test_arg1_unpack_tuple_exc(self):
        def arg1(e):
            raise ValueError

        args = ('5',)
        pre_ref = sys.getrefcount(args)
        self.assertEqual(sys.getrefcount(arg1), 2)
        with self.assertRaises(ValueError):
            arg1(*args)
        self.assertEqual(sys.getrefcount(arg1), 2)
        self.assertEqual(sys.getrefcount(args), pre_ref)
        info = pyjion.info(arg1)
        self.assertTrue(info['compiled'])

    def test_arg1_unpack_dict(self):
        def arg1(e):
            a = '1'
            b = '2'
            c = '3'
            d = '4'
            return a + b + c + d + e

        args = {'e': '5'}
        pre_ref = sys.getrefcount(args)
        self.assertEqual(sys.getrefcount(arg1), 2)
        self.assertEqual(arg1(**args), '12345')
        self.assertEqual(sys.getrefcount(arg1), 2)
        self.assertEqual(sys.getrefcount(args), pre_ref)

        info = pyjion.info(arg1)
        self.assertTrue(info['compiled'])

    def test_arg1_unpack_dict_exc(self):
        def arg1(e):
            raise ValueError

        args = {'e': '5'}
        pre_ref = sys.getrefcount(args)
        self.assertEqual(sys.getrefcount(arg1), 2)
        with self.assertRaises(ValueError):
            arg1(**args)
        self.assertEqual(sys.getrefcount(arg1), 2)
        self.assertEqual(sys.getrefcount(args), pre_ref)

        info = pyjion.info(arg1)
        self.assertTrue(info['compiled'])

    def test_arg1_unpack_dict_and_tuple(self):
        def arg1(e, f):
            a = '1'
            b = '2'
            c = '3'
            d = '4'
            return a + b + c + d + e + f

        args = ('5',)
        kargs = {'f': '6'}
        pre_ref = sys.getrefcount(args)
        kpre_ref = sys.getrefcount(kargs)
        self.assertEqual(sys.getrefcount(arg1), 2)
        self.assertEqual(arg1(*args, **kargs), '123456')
        self.assertEqual(sys.getrefcount(arg1), 2)
        self.assertEqual(sys.getrefcount(args), pre_ref)
        self.assertEqual(sys.getrefcount(kargs), kpre_ref)

        info = pyjion.info(arg1)
        self.assertTrue(info['compiled'])

    def test_arg1_unpack_dict_and_tuple_exc(self):
        def arg1(e, f):
            raise ValueError

        args = ('5',)
        kargs = {'f': '6'}
        pre_ref = sys.getrefcount(args)
        kpre_ref = sys.getrefcount(kargs)
        self.assertEqual(sys.getrefcount(arg1), 2)
        with self.assertRaises(ValueError):
            arg1(*args, **kargs)
        self.assertEqual(sys.getrefcount(arg1), 2)
        self.assertEqual(sys.getrefcount(args), pre_ref)
        self.assertEqual(sys.getrefcount(kargs), kpre_ref)

        info = pyjion.info(arg1)
        self.assertTrue(info['compiled'])

    def test_arg1_exc(self):
        def arg1(e):
            raise ValueError

        a = '5'
        pre_ref = sys.getrefcount(a)
        self.assertEqual(sys.getrefcount(arg1), 2)
        with self.assertRaises(ValueError):
            arg1(e=a)
        self.assertEqual(sys.getrefcount(arg1), 2)
        self.assertEqual(sys.getrefcount(a), pre_ref)

        info = pyjion.info(arg1)
        self.assertTrue(info['compiled'])

    def test_arg3(self):
        def arg3(e, f=None, *args, **kwargs):
            a = '1'
            b = '2'
            c = '3'
            d = '4'
            return a + b + c + d + e + f

        a = '5'
        b = '6'
        c = '7'
        pre_ref_a = sys.getrefcount(a)
        pre_ref_b = sys.getrefcount(b)
        pre_ref_c = sys.getrefcount(c)
        self.assertEqual(sys.getrefcount(arg3), 2)
        self.assertEqual(arg3(a, f=b, g=c), '123456')

        self.assertEqual(sys.getrefcount(arg3), 2)
        self.assertEqual(sys.getrefcount(a), pre_ref_a)
        self.assertEqual(sys.getrefcount(b), pre_ref_b)
        self.assertEqual(sys.getrefcount(c), pre_ref_c)

        info = pyjion.info(arg3)
        self.assertTrue(info['compiled'])

    def test_arg3_exc(self):
        def arg3(e, f=None, *args, **kwargs):
            raise ValueError

        a = '5'
        b = '6'
        c = '7'
        pre_ref_a = sys.getrefcount(a)
        pre_ref_b = sys.getrefcount(b)
        pre_ref_c = sys.getrefcount(c)
        self.assertEqual(sys.getrefcount(arg3), 2)
        with self.assertRaises(ValueError):
            arg3(a, f=b, g=c)

        self.assertEqual(sys.getrefcount(arg3), 2)
        self.assertEqual(sys.getrefcount(a), pre_ref_a)
        self.assertEqual(sys.getrefcount(b), pre_ref_b)
        self.assertEqual(sys.getrefcount(c), pre_ref_c)

        info = pyjion.info(arg3)
        self.assertTrue(info['compiled'])


class ObjectMethodCallsTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()

    def test_arg0(self):
        class F:
            def arg0(cls) -> int:
                a = 1
                b = 2
                c = 3
                d = 4
                return a + b + c + d

        f = F()
        self.assertEqual(sys.getrefcount(F.arg0), 2)
        self.assertEqual(sys.getrefcount(F), 6)
        self.assertEqual(sys.getrefcount(f), 2)
        self.assertEqual(f.arg0(), 10)
        self.assertEqual(sys.getrefcount(F.arg0), 2)
        self.assertEqual(sys.getrefcount(F), 6)
        self.assertEqual(sys.getrefcount(f), 2)
        info = pyjion.info(f.arg0.__code__)
        self.assertTrue(info['compiled'])

    def test_arg0_cfunction(self):
        f = str("hello")
        pre_target_ref = sys.getrefcount(str.title)
        pre_arg_ref = sys.getrefcount(f)
        self.assertEqual(f.title(), "Hello")
        self.assertEqual(sys.getrefcount(f), pre_arg_ref)
        self.assertEqual(sys.getrefcount(str.title), pre_target_ref)
        info = pyjion.info(self.test_arg0_cfunction.__code__)
        self.assertTrue(info['compiled'])

    def test_arg1(self):
        class F:
            def arg1(self, e):
                a = '1'
                b = '2'
                c = '3'
                d = '4'
                return a + b + c + d + e

        f = F()
        test_arg1_arg1 = '5'
        pre_refcnt_a = sys.getrefcount(test_arg1_arg1)
        self.assertEqual(sys.getrefcount(F.arg1), 2)
        self.assertEqual(sys.getrefcount(F), 6)
        self.assertEqual(sys.getrefcount(f), 2)
        self.assertEqual(f.arg1(test_arg1_arg1), '12345')
        self.assertEqual(sys.getrefcount(F.arg1), 2)
        self.assertEqual(sys.getrefcount(F), 6)
        self.assertEqual(sys.getrefcount(f), 2)
        self.assertEqual(pre_refcnt_a, sys.getrefcount(test_arg1_arg1))
        info = pyjion.info(f.arg1.__code__)
        self.assertTrue(info['compiled'])

    def test_arg1_cfunction(self):
        f = str("hello")
        o = "o"
        pre_target_ref = sys.getrefcount(str.strip)
        pre_arg_ref = sys.getrefcount(f)
        pre_arg1_ref = sys.getrefcount(o)
        self.assertEqual(f.strip(o), "hell")
        self.assertEqual(sys.getrefcount(f), pre_arg_ref)
        self.assertEqual(sys.getrefcount(o), pre_arg1_ref)
        self.assertEqual(sys.getrefcount(str.strip), pre_target_ref)
        info = pyjion.info(self.test_arg1_cfunction.__code__)
        self.assertTrue(info['compiled'])

    def test_arg1_cfunction_exc(self):
        f = str("hello")
        o = 1000000
        pre_target_ref = sys.getrefcount(str.strip)
        pre_arg_ref = sys.getrefcount(f)
        pre_arg1_ref = sys.getrefcount(o)
        with self.assertRaises(TypeError):
            f.strip(o)
        self.assertEqual(sys.getrefcount(f), pre_arg_ref)
        self.assertEqual(sys.getrefcount(o), pre_arg1_ref)
        self.assertEqual(sys.getrefcount(str.strip), pre_target_ref)
        info = pyjion.info(self.test_arg1_cfunction_exc.__code__)
        self.assertTrue(info['compiled'])

    def test_arg2(self):
        class F:
            def arg2(self, e, f):
                a = 1
                b = 2
                c = 3
                d = 4
                return a + b + c + d + e + f

        f = F()
        self.assertEqual(f.arg2(5, 6), 21)
        info = pyjion.info(f.arg2.__code__)
        self.assertTrue(info['compiled'])

    def test_arg3(self):
        class F:
            def arg3(self, e, f, g):
                a = 1
                b = 2
                c = 3
                d = 4
                return a + b + c + d + e + f + g

        f = F()
        self.assertEqual(f.arg3(5, 6, 7), 28)
        info = pyjion.info(f.arg3.__code__)
        self.assertTrue(info['compiled'])

    def test_arg4(self):
        class F:
            def arg4(self, e, f, g, h):
                a = 1
                b = 2
                c = 3
                d = 4
                return a + b + c + d + e + f + g + h

        f = F()
        self.assertEqual(f.arg4(5, 6, 7, 8), 36)
        info = pyjion.info(f.arg4.__code__)
        self.assertTrue(info['compiled'])

    def test_arg5(self):
        class F:
            def arg5(self, e, f, g, h, i):
                a = 1
                b = 2
                c = 3
                d = 4
                return a + b + c + d + e + f + g + h + i

        f = F()
        self.assertEqual(f.arg5(5, 6, 7, 8, 9), 45)
        info = pyjion.info(f.arg5.__code__)
        self.assertTrue(info['compiled'])

    def test_arg6(self):
        class F:
            def arg6(self, e, f, g, h, i, j):
                a = 1
                b = 2
                c = 3
                d = 4
                return a + b + c + d + e + f + g + h + i + j

        f = F()
        self.assertEqual(f.arg6(5, 6, 7, 8, 9, 10), 55)
        info = pyjion.info(f.arg6.__code__)
        self.assertTrue(info['compiled'])

    def test_arg7(self):
        class F:
            def arg7(self, e, f, g, h, i, j, k):
                a = 1
                b = 2
                c = 3
                d = 4
                return a + b + c + d + e + f + g + h + i + j + k

        f = F()
        self.assertEqual(f.arg7(5, 6, 7, 8, 9, 10, 11), 66)
        info = pyjion.info(f.arg7.__code__)
        self.assertTrue(info['compiled'])

    def test_arg8(self):
        class F:
            def arg8(self, e, f, g, h, i, j, k, l):
                a = 1
                b = 2
                c = 3
                d = 4
                return a + b + c + d + e + f + g + h + i + j + k + l

        f = F()
        self.assertEqual(f.arg8(5, 6, 7, 8, 9, 10, 11, 12), 78)
        info = pyjion.info(f.arg8.__code__)
        self.assertTrue(info['compiled'])

    def test_arg9(self):
        class F:
            def arg9(self, e, f, g, h, i, j, k, l, m):
                a = 1
                b = 2
                c = 3
                d = 4
                return a + b + c + d + e + f + g + h + i + j + k + l + m

        f = F()
        self.assertEqual(f.arg9(5, 6, 7, 8, 9, 10, 11, 12, 13), 91)
        info = pyjion.info(f.arg9.__code__)
        self.assertTrue(info['compiled'])

    def test_arg10(self):
        class F:
            def arg10(self, e, f, g, h, i, j, k, l, m, n):
                a = 1
                b = 2
                c = 3
                d = 4
                return a + b + c + d + e + f + g + h + i + j + k + l + m + n

        f = F()
        self.assertEqual(f.arg10(5, 6, 7, 8, 9, 10, 11, 12, 13, 14), 105)
        info = pyjion.info(f.arg10.__code__)
        self.assertTrue(info['compiled'])

    def test_arg15(self):
        class F:
            def arg15(self, e, f, g, h, i, j, k, l, m, n, o, p, q, r):
                a = '1'
                b = '2'
                c = '3'
                d = '4'
                return a + b + c + d + e + f + g + h + i + j + k + l + m + n + o

        f = F()
        arg1 = '5'
        pre_refcnt_a = sys.getrefcount(arg1)
        self.assertEqual(sys.getrefcount(F.arg15), 2)
        self.assertEqual(sys.getrefcount(F), 6)
        self.assertEqual(sys.getrefcount(f), 2)
        self.assertEqual(f.arg15(arg1, '6', '7', '8', '9', '10', '11', '12', '13', '14', '15' ,'16', '17', '18'), '123456789101112131415')
        self.assertEqual(sys.getrefcount(F.arg15), 2)
        self.assertEqual(sys.getrefcount(F), 6)
        self.assertEqual(sys.getrefcount(f), 2)
        self.assertEqual(pre_refcnt_a, sys.getrefcount(arg1))
        info = pyjion.info(f.arg15.__code__)
        self.assertTrue(info['compiled'])

    def test_arg15_exc(self):
        class F:
            def arg15(self, e, f, g, h, i, j, k, l, m, n, o, p, q, r):
                raise ValueError
        f = F()
        arg1 = '5'
        pre_refcnt_a = sys.getrefcount(arg1)
        self.assertEqual(sys.getrefcount(F.arg15), 2)
        self.assertEqual(sys.getrefcount(F), 6)
        self.assertEqual(sys.getrefcount(f), 2)
        with self.assertRaises(ValueError):
            f.arg15(arg1, '6', '7', '8', '9', '10', '11', '12', '13', '14', '15' ,'16', '17', '18')
        self.assertEqual(sys.getrefcount(F.arg15), 2)
        self.assertEqual(sys.getrefcount(F), 6)
        self.assertEqual(sys.getrefcount(f), 2)
        self.assertEqual(pre_refcnt_a, sys.getrefcount(arg1))
        info = pyjion.info(f.arg15.__code__)
        self.assertTrue(info['compiled'])

    def test_arg15_cfunction(self):
        f = str("{}{}{}{}{}{}{}{}{}{}{}{}{}{}")
        arg1 = '5'
        pre_refcnt = sys.getrefcount(f)
        pre_refcnt_a = sys.getrefcount(arg1)
        target_pre_refcnt = sys.getrefcount(str.format)
        self.assertEqual(f.format(arg1, '6', '7', '8', '9', '10', '11', '12', '13', '14', '15', '16', '17', '18'), "56789101112131415161718")
        self.assertEqual(sys.getrefcount(f), pre_refcnt)
        self.assertEqual(sys.getrefcount(str.format), target_pre_refcnt)
        self.assertEqual(sys.getrefcount(arg1), pre_refcnt_a)
        info = pyjion.info(self.test_arg15_cfunction.__code__)
        self.assertTrue(info['compiled'])

    def test_arg15_cfunction_exc(self):
        f = str("{}{}{}{}{}{}{}{}{}{}{}{}{}{}{}")
        arg1 = '5'
        pre_refcnt = sys.getrefcount(f)
        pre_refcnt_a = sys.getrefcount(arg1)
        target_pre_refcnt = sys.getrefcount(str.format)
        with self.assertRaises(IndexError):
            f.format(arg1, '6', '7', '8', '9', '10', '11', '12', '13', '14', '15', '16', '17', '18')
        self.assertEqual(sys.getrefcount(f), pre_refcnt)
        self.assertEqual(sys.getrefcount(str.format), target_pre_refcnt)
        self.assertEqual(sys.getrefcount(arg1), pre_refcnt_a)
        info = pyjion.info(self.test_arg15_cfunction_exc.__code__)
        self.assertTrue(info['compiled'])


if __name__ == "__main__":
    unittest.main()
