from base import PyjionTestCase


class IteratorTestCase(PyjionTestCase):

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
