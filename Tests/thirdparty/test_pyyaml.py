import pytest
try:
    import yaml
    has_pyyaml = True
except ImportError:
    has_pyyaml = False


@pytest.mark.skipif(not has_pyyaml, reason="No pyyaml installed")
@pytest.mark.external
def test_load_yaml():
    content = """
linear: 
- [1, 2, 3]
- test
- 1
    """
    data1 = yaml.safe_load(content)

    data2 = yaml.safe_load(content)

    data3 = yaml.safe_load(content)

    assert data1 == data2
    assert data1 == data3
    assert data2 == data3
    assert len(data1['linear']) == 3
    assert data1['linear'][0] == [1, 2, 3]
    assert data1['linear'][1] == 'test'
    assert data1['linear'][2] == 1
