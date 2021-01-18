import pyjion
import unittest
import gc
import sys


class SliceTestCase(unittest.TestCase):
    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_list_slicing(self):
        l = [0, 1, 2, 3]
        initial_ref = sys.getrefcount(l)
        self.assertEqual(l[0:1], [0])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual(l[:1], [0])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual(l[1:], [1, 2, 3])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual(l[:], [0, 1, 2, 3])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual(l[-2:-1], [2])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual(l[-1:-2], [])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual(l[-1:], [3])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual(l[:-1], [0, 1, 2])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual(l[0:1:], [0])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual(l[0:1:1], [0])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual(l[::1], [0, 1, 2, 3])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual(l[::-1], [3, 2, 1, 0])
        self.assertEqual(initial_ref, sys.getrefcount(l))
        n = l[::-1]
        self.assertEqual(sys.getrefcount(n), 2)

        m = l[:]
        self.assertEqual(sys.getrefcount(m), 2)

        self.assertEqual(l[::-2], [3, 1])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual(l[::2], [0, 2])
        self.assertEqual(initial_ref, sys.getrefcount(l))

        self.assertEqual([0, 1, 2, 3][False:True], [0])
        self.assertEqual(initial_ref, sys.getrefcount(l))
        pass

    def test_list_slicing_expressions(self):
        l = [0, 1, 2, 3]
        x = int(2)  # prevent const rolling
        initial_ref = sys.getrefcount(l)
        initial_ref_x = sys.getrefcount(x)
        self.assertEqual(l[x + 1:0], [])
        self.assertEqual(initial_ref, sys.getrefcount(l))
        self.assertEqual(initial_ref_x, sys.getrefcount(x))
        self.assertEqual(l[:x-1], [0])
        self.assertEqual(initial_ref, sys.getrefcount(l))
        self.assertEqual(initial_ref_x, sys.getrefcount(x))
        self.assertEqual(l[x:x+1], [2])
        self.assertEqual(initial_ref, sys.getrefcount(l))
        self.assertEqual(initial_ref_x, sys.getrefcount(x))
        self.assertEqual(l[:x-4], [0, 1])
        self.assertEqual(initial_ref, sys.getrefcount(l))
        self.assertEqual(initial_ref_x, sys.getrefcount(x))

    def test_string_slicing(self):
        self.assertEqual('The train to Oxford leaves at 3pm'[-1:3:-2], 'm3t ealdox tnat')

    def test_fannkuch(self):
        DEFAULT_ARG = 9
        n=DEFAULT_ARG
        count = list(range(1, n + 1))
        max_flips = 0
        m = n - 1
        r = n
        check = 0
        perm1 = list(range(n))
        perm = list(range(n))
        perm1_ins = perm1.insert
        perm1_pop = perm1.pop

        while 1:
            if check < 30:
                check += 1

            while r != 1:
                count[r - 1] = r
                r -= 1

            if perm1[0] != 0 and perm1[m] != m:
                perm = perm1[:]
                flips_count = 0
                k = perm[0]
                while k:
                    perm[:k + 1] = perm[k::-1]
                    flips_count += 1
                    k = perm[0]

                if flips_count > max_flips:
                    max_flips = flips_count

            while r != n:
                perm1_ins(r, perm1_pop(0))
                count[r] -= 1
                if count[r] > 0:
                    break
                r += 1
            else:
                return


if __name__ == "__main__":
    unittest.main()
