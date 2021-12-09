import pytest

try:
    import numpy as np
    has_numpy = True
except ImportError:
    has_numpy = False


@pytest.mark.skipif(not has_numpy, reason="No numpy installed")
@pytest.mark.external
def test_array_math():
    t = np.array([250., 300., 350., 400.])
    v = 275.4
    x = 324.5

    lte = (t <= v)
    assert isinstance(lte, np.ndarray)
    j = (t <= v) & (t >= x)
    assert isinstance(j, np.ndarray)
    comp1 = j == np.array([False, False, False, False])
    assert isinstance(comp1, np.ndarray)
    assert comp1.all()

    i = t <= v
    assert isinstance(i, np.ndarray)
    comp2 = i == np.array([True, False, False, False])
    assert isinstance(comp2, np.ndarray)
    assert comp2.all()
