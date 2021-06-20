import pyjion
import unittest
import gc


class GraphTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()
        pyjion.enable_graphs()

    def tearDown(self) -> None:
        pyjion.disable()
        pyjion.disable_graphs()
        gc.collect()

    def test_graph_dump(self) -> None:
        def f():
            a = 1
            b = 2
            return a + b
        f()
        self.assertIsNotNone(pyjion.get_graph(f))
        f()
        self.assertIsNotNone(pyjion.get_graph(f))
