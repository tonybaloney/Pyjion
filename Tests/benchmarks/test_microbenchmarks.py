import pyjion
import timeit
from statistics import fmean


def test_floats(n=10000):
    for y in range(n):
        x = 0.1
        z = y * y + x - y
        x *= z


def test_ints(n=10000):
    for y in range(n):
        x = 2
        z = y * y + x - y
        x *= z


if __name__ == "__main__":
    tests = (test_floats, test_ints)
    for test in tests:
        without_result = timeit.repeat(test, repeat=5, number=1000)
        print("{0} took {1} min, {2} max, {3} mean without Pyjion".format(str(test), min(without_result), max(without_result), fmean(without_result)))
        pyjion.enable()
        pyjion.set_optimization_level(1)
        with_result = timeit.repeat(test, repeat=5, number=1000)
        pyjion.disable()
        print("{0} took {1} min, {2} max, {3} mean with Pyjion".format(str(test), min(with_result), max(with_result), fmean(with_result)))
        delta = (abs(fmean(with_result) - fmean(without_result)) / fmean(without_result)) * 100.0
        print(f"Pyjion is {delta:.2f}% faster")
