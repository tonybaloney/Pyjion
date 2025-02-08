import statistics
from fractions import Fraction
import pytest

def test_floats():
    a = 2.0
    b = 3.0
    c = 4.0
    c += a * b
    assert c == 10.0
    c /= a + b
    assert c == 2.0
    c %= a % b
    assert c == 0.0


def test_ints():
    a = 2
    b = 3
    c = 4
    c += a * b
    assert c == 10
    c /= a + b
    assert c == 2.0
    c //= a + b
    assert c == 0
    c = 4
    c %= a % b
    assert c == 0


def test_mixed():
    a = 2
    b = 3.0
    c = 4
    c += a * b
    assert c == 10.0
    c /= a + b
    assert c == 2.0
    c = 4
    c %= a % b
    assert c == 0.0


def test_mixed2():
    a = 2.0
    b = 3
    c = 4
    c += a * b
    assert c == 10.0
    c /= a + b
    assert c == 2.0
    c = 4
    c %= a % b
    assert c == 0.0


def test_mixed3():
    a = 2
    b = 3
    c = 4.0
    c += a * b
    assert c == 10.0
    c /= a + b
    assert c == 2.0
    c = 4.0
    c %= a % b
    assert c == 0.0


def test_mixed4():
    a = 2
    b = 3.0
    c = 4.0
    c += a * b
    assert c == 10.0
    c /= a + b
    assert c == 2.0
    c = 4.0
    c %= a % b
    assert c == 0.0

    i = -10
    x = 1234567890.0 * (10.0 ** i)
    assert x == 0.12345678900000001
    i = 0
    x = 1234567890.0 * (10.0 ** i)
    assert x == 1234567890.0
    i = 10
    x = 1234567890.0 * (10.0 ** i)
    assert x == 1.23456789e+19


def test_mixed5():
    a = 2.0
    b = 3
    c = 4.0
    c += a * b
    assert c == 10.0
    c /= a + b
    assert c == 2.0
    c %= a + b
    assert c == 2


def test_mixed6():
    a = 2.0
    b = 3.0
    c = 4
    c += a * b
    assert c == 10.0
    c /= a + b
    assert c == 2.0
    c %= a % b
    assert c == 0.0


def test_modulo():
    a = 1
    b = 2
    c = "boo %s"
    x = c % (a + b)
    assert x == "boo 3"

def test_mean():
    answer = statistics.mean([1, 2, 3, 4, 4])
    assert answer == 2.8
    answer = statistics.mean([1, 2, 3, 4, 4])
    assert answer == 2.8

@pytest.mark.optimization(1)
@pytest.mark.skip(reason="currently broken in 3.10, causes overflow or invalid float.")
def test_variance():
    data = [0, 0, 1]
    result = statistics.variance(data)
    assert result == 0.33333333333333337

def test_variance2():
    data = [0, 1]
    result = statistics.variance(data)
    assert result == 0.5

@pytest.mark.optimization(1)
def test_float_fraction_arithmetic():
    f1 = Fraction(0.3333333333333333)
    assert f1 == Fraction(6004799503160661, 18014398509481984)
    f2 = Fraction(0.1111111111111111)
    assert f2 == Fraction(2001599834386887, 18014398509481984)
    f3 = f1 + f2
    assert f3 == Fraction(2001599834386887, 4503599627370496)

def test_fraction_operations():
    result1 = Fraction(2, 3) + Fraction(7, 5)
    assert result1 == Fraction(31, 15)
    result2 = Fraction(2, 3) - Fraction(7, 5)
    assert result2 == Fraction(-11, 15)
    result3 = Fraction(2, 3) * Fraction(7, 5)
    assert result3 == Fraction(14, 15)
    result4 = Fraction(2, 3) ** Fraction(7, 5)
    assert result4 == 0.5668553336114626


def test_strides_from_shape():
    ndim = 3
    layout = 'C'
    shape = [3, 2, 5]
    itemsize = 1
    if layout == 'C':
        strides = list(shape[1:]) + [itemsize]
        for i in range(ndim-2, -1, -1):
            strides[i] *= strides[i+1]
    else:
        strides = [itemsize] + list(shape[:-1])
        for i in range(1, ndim):
            strides[i] *= strides[i-1]
    assert strides == [10, 5, 1]


@pytest.mark.optimization(1)
def test_ema():
    class EMA:
        """
        From the tqdm library

        Exponential moving average: smoothing to give progressively lower
        weights to older values.
        Parameters
        ----------
        smoothing  : float, optional
            Smoothing factor in range [0, 1], [default: 0.3].
            Increase to give more weight to recent values.
            Ranges from 0 (yields old value) to 1 (yields new value).
        """
        def __init__(self, smoothing=0.3):
            self.alpha = smoothing
            self.last = 0
            self.calls = 0

        def __call__(self, x=None):
            """
            Parameters
            ----------
            x  : float
                New value to include in EMA.
            """
            beta = 1 - self.alpha
            if x is not None:
                self.last = self.alpha * x + beta * self.last
                self.calls += 1
            return self.last / (1 - beta ** self.calls) if self.calls else self.last

    ema = EMA()
    assert ema() == 0
    assert round(ema(1)) == 1
    assert round(ema()) == 1
    assert round(ema(2), 1) == 1.6
