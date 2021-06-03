import pyjion
import timeit

def test_floats(n=10000):
    for y in range(10000):
        x = 0.1
        z = y * y + x - y
        x *= z


if __name__ == "__main__":
    tests = (test_floats, )
    for test in tests:
        print("{1} took {0} without Pyjion".format(timeit.repeat(test, repeat=5, number=1000), str(test)))
        pyjion.enable()
        pyjion.set_optimization_level(1)
        print("{1} took {0} {0} with Pyjion".format(timeit.repeat(test, repeat=5, number=1000), str(test)))
        pyjion.disable()
