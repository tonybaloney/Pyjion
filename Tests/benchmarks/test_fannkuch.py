"""
The Computer Language Benchmarks Game
http://benchmarksgame.alioth.debian.org/
Contributed by Sokolov Yura, modified by Tupteq.
"""
import pyjion
import pyjion.dis
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
            assert len(perm) == len(perm1), "Invalid size after copy"
            flips_count = 0
            k = perm[0]
            while k:
                perm[:k + 1] = perm[k::-1]
                flips_count += 1
                assert len(perm) > 0, f"Invalid size after reverse slice assignment for {k}"
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
    print("Fannkuch({1}) took {0} without Pyjion".format(timeit.repeat(fannkuch, repeat=5, number=1), DEFAULT_ARG))
    pyjion.enable()
    pyjion.set_optimization_level(1)
    print("Fannkuch({1}) took {0} with Pyjion".format(timeit.repeat(fannkuch, repeat=5, number=1), DEFAULT_ARG))
    pyjion.disable()
