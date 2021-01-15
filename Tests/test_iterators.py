import sys
sys.path.append('/Users/anthonyshaw/CLionProjects/pyjion/src')
import pyjion
import pyjion.dis
import unittest
import gc


class IteratorTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_nested_tuple(self):
        l = (1,2,3)
        for n in l:
            for x in dict(), dict():
                pass

    def test_nested_list(self):
        l = [1,2,3]
        for n in l:
            for x in dict(), dict():
                pass
