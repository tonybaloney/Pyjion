import unittest
import pyjion
import gc


class PyjionTestCase(unittest.TestCase):
    def run(self, result=None):
        super().run(result)
        super().run(result)

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()


class NoPgcPyjionTestCase(PyjionTestCase):
    def setUp(self) -> None:
        pyjion.enable()
        pyjion.disable_pgc()


class DebugPyjionTestCase(PyjionTestCase):
    def setUp(self) -> None:
        pyjion.enable()
        pyjion.enable_debug()

    def tearDown(self) -> None:
        pyjion.disable()
        pyjion.disable_debug()
        gc.collect()


class GraphPyjionTestCase(PyjionTestCase):
    def setUp(self) -> None:
        pyjion.enable()
        pyjion.enable_graphs()

    def tearDown(self) -> None:
        pyjion.enable()
        pyjion.disable_graphs()
        gc.collect()
