import pyjion
from base import GraphPyjionTestCase


class GraphTestCase(GraphPyjionTestCase):

    def test_graph_dump(self) -> None:
        def f():
            a = 1
            b = 2
            return a + b
        f()
        self.assertIsNotNone(pyjion.get_graph(f))
        f()
        self.assertIsNotNone(pyjion.get_graph(f))
