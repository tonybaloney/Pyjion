"""
The Computer Language Benchmarks Game
http://benchmarksgame.alioth.debian.org/
Contributed by Sokolov Yura, modified by Tupteq.
"""

import pyjion
import timeit

DEFAULT_ARG = 9


def fannkuch(n=DEFAULT_ARG):
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
            return max_flips


if __name__ == "__main__":
    import dis
    dis.dis(fannkuch)
    print("Fannkuch({1}) took {0} without Pyjion".format(timeit.timeit(fannkuch, number=10), DEFAULT_ARG))
    pyjion.enable()
    print("Fannkuch({1}) took {0} with Pyjion".format(timeit.timeit(fannkuch, number=10), DEFAULT_ARG))
    pyjion.disable()

"""
14           0 LOAD_GLOBAL              0 (list)
              2 LOAD_GLOBAL              1 (range)
              4 LOAD_CONST               1 (1)
              6 LOAD_FAST                0 (n)
              8 LOAD_CONST               1 (1)
             10 BINARY_ADD
             12 CALL_FUNCTION            2
             14 CALL_FUNCTION            1
             16 STORE_FAST               1 (count)

 15          18 LOAD_CONST               2 (0)
             20 STORE_FAST               2 (max_flips)

 16          22 LOAD_FAST                0 (n)
             24 LOAD_CONST               1 (1)
             26 BINARY_SUBTRACT
             28 STORE_FAST               3 (m)

 17          30 LOAD_FAST                0 (n)
             32 STORE_FAST               4 (r)

 18          34 LOAD_CONST               2 (0)
             36 STORE_FAST               5 (check)

 19          38 LOAD_GLOBAL              0 (list)
             40 LOAD_GLOBAL              1 (range)
             42 LOAD_FAST                0 (n)
             44 CALL_FUNCTION            1
             46 CALL_FUNCTION            1
             48 STORE_FAST               6 (perm1)

 20          50 LOAD_GLOBAL              0 (list)
             52 LOAD_GLOBAL              1 (range)
             54 LOAD_FAST                0 (n)
             56 CALL_FUNCTION            1
             58 CALL_FUNCTION            1
             60 STORE_FAST               7 (perm)

 21          62 LOAD_FAST                6 (perm1)
             64 LOAD_ATTR                2 (insert)
             66 STORE_FAST               8 (perm1_ins)

 22          68 LOAD_FAST                6 (perm1)
             70 LOAD_ATTR                3 (pop)
             72 STORE_FAST               9 (perm1_pop)

 25     >>   74 LOAD_FAST                5 (check)
             76 LOAD_CONST               3 (30)
             78 COMPARE_OP               0 (<)
             80 POP_JUMP_IF_FALSE       90

 26          82 LOAD_FAST                5 (check)
             84 LOAD_CONST               1 (1)
             86 INPLACE_ADD
             88 STORE_FAST               5 (check)

 28     >>   90 LOAD_FAST                4 (r)
             92 LOAD_CONST               1 (1)
             94 COMPARE_OP               3 (!=)
             96 POP_JUMP_IF_FALSE      120

 29          98 LOAD_FAST                4 (r)
            100 LOAD_FAST                1 (count)
            102 LOAD_FAST                4 (r)
            104 LOAD_CONST               1 (1)
            106 BINARY_SUBTRACT
            108 STORE_SUBSCR

 30         110 LOAD_FAST                4 (r)
            112 LOAD_CONST               1 (1)
            114 INPLACE_SUBTRACT
            116 STORE_FAST               4 (r)
            118 JUMP_ABSOLUTE           90

 32     >>  120 LOAD_FAST                6 (perm1)
            122 LOAD_CONST               2 (0)
            124 BINARY_SUBSCR
            126 LOAD_CONST               2 (0)
            128 COMPARE_OP               3 (!=)
            130 POP_JUMP_IF_FALSE      228
            132 LOAD_FAST                6 (perm1)
            134 LOAD_FAST                3 (m)
            136 BINARY_SUBSCR
            138 LOAD_FAST                3 (m)
            140 COMPARE_OP               3 (!=)
            142 POP_JUMP_IF_FALSE      228

 33         144 LOAD_FAST                6 (perm1)
            146 LOAD_CONST               0 (None)
            148 LOAD_CONST               0 (None)
            150 BUILD_SLICE              2
            152 BINARY_SUBSCR
            154 STORE_FAST               7 (perm)

 34         156 LOAD_CONST               2 (0)
            158 STORE_FAST              10 (flips_count)

 35         160 LOAD_FAST                7 (perm)
            162 LOAD_CONST               2 (0)
            164 BINARY_SUBSCR
            166 STORE_FAST              11 (k)

 36     >>  168 LOAD_FAST               11 (k)
            170 POP_JUMP_IF_FALSE      216

 37         172 LOAD_FAST                7 (perm)
            174 LOAD_FAST               11 (k)
            176 LOAD_CONST               0 (None)
            178 LOAD_CONST               4 (-1)
            180 BUILD_SLICE              3
            182 BINARY_SUBSCR
            184 LOAD_FAST                7 (perm)
            186 LOAD_CONST               0 (None)
            188 LOAD_FAST               11 (k)
            190 LOAD_CONST               1 (1)
            192 BINARY_ADD
            194 BUILD_SLICE              2
            196 STORE_SUBSCR

 38         198 LOAD_FAST               10 (flips_count)
            200 LOAD_CONST               1 (1)
            202 INPLACE_ADD
            204 STORE_FAST              10 (flips_count)

 39         206 LOAD_FAST                7 (perm)
            208 LOAD_CONST               2 (0)
            210 BINARY_SUBSCR
            212 STORE_FAST              11 (k)
            214 JUMP_ABSOLUTE          168

 41     >>  216 LOAD_FAST               10 (flips_count)
            218 LOAD_FAST                2 (max_flips)
            220 COMPARE_OP               4 (>)
            222 POP_JUMP_IF_FALSE      228

 42         224 LOAD_FAST               10 (flips_count)
            226 STORE_FAST               2 (max_flips)

 44     >>  228 LOAD_FAST                4 (r)
            230 LOAD_FAST                0 (n)
            232 COMPARE_OP               3 (!=)
            234 EXTENDED_ARG             1
            236 POP_JUMP_IF_FALSE      294

 45         238 LOAD_FAST                8 (perm1_ins)
            240 LOAD_FAST                4 (r)
            242 LOAD_FAST                9 (perm1_pop)
            244 LOAD_CONST               2 (0)
            246 CALL_FUNCTION            1
            248 CALL_FUNCTION            2
            250 POP_TOP

 46         252 LOAD_FAST                1 (count)
            254 LOAD_FAST                4 (r)
            256 DUP_TOP_TWO
            258 BINARY_SUBSCR
            260 LOAD_CONST               1 (1)
            262 INPLACE_SUBTRACT
            264 ROT_THREE
            266 STORE_SUBSCR

 47         268 LOAD_FAST                1 (count)
            270 LOAD_FAST                4 (r)
            272 BINARY_SUBSCR
            274 LOAD_CONST               2 (0)
            276 COMPARE_OP               4 (>)
            278 EXTENDED_ARG             1
            280 POP_JUMP_IF_FALSE      284

 48         282 JUMP_ABSOLUTE           74

 49     >>  284 LOAD_FAST                4 (r)
            286 LOAD_CONST               1 (1)
            288 INPLACE_ADD
            290 STORE_FAST               4 (r)
            292 JUMP_ABSOLUTE          228

 51     >>  294 LOAD_FAST                2 (max_flips)
            296 RETURN_VALUE
            298 JUMP_ABSOLUTE           74
            300 LOAD_CONST               0 (None)
            302 RETURN_VALUE
"""