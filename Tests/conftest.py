import pytest
import pyjion
import gc
import warnings


@pytest.hookimpl(hookwrapper=True)
def pytest_pyfunc_call(pyfuncitem):
    yield
    # Force pyjion to be disabled, useful when a test raises an exception
    # so it stops every consequent Python call to be JITted and making it
    # impossible to figure out what went wrong.
    pyjion.disable()


def pytest_runtest_call(item: pytest.Item) -> None:
    pyjion.enable()
    pyjion.config(level=int(item.config.option.opt_level), pgc=True)
    for mark in item.iter_markers():
        if mark.name == "graph":
            pyjion.config(graph=True)
        if mark.name == "nopgc":
            pyjion.config(pgc=False)
    pyjion.config(debug=True)
    item.runtest()
    pyjion.disable()
    info = pyjion.info(item.function)
    if not info.compiled:
        warnings.warn("{0} did not compile ({1})".format(item.function, str(info.compile_result)))
    pyjion.config(graph=False)
    gc.collect()


def pytest_addoption(parser):
    parser.addoption('--opt-level', action='store', type=int, default=1,
                     help='Optimization level')
    parser.addoption(
        "--external", action="store_true", default=False, help="run external tests"
    )


def pytest_generate_tests(metafunc):
    metafunc.fixturenames.append('repeat_n')
    metafunc.parametrize('repeat_n', (0, 1, 2))


def pytest_collection_modifyitems(config, items):
    skip_external = pytest.mark.skip(reason="need --external option to run")
    skip_optimization = pytest.mark.skip(reason="test designed for higher optimization levels")
    for item in items:
        if "external" in item.keywords and not config.getoption("--external"):
            item.add_marker(skip_external)
        if "optimization" in item.keywords:
            args = item.get_closest_marker(name="optimization").args
            if len(args) == 0:
                level = 1
            else:
                level = args[0]
            if config.getoption("--opt-level") != level:
                item.add_marker(skip_optimization)
