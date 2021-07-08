import pyjion
import unittest
import os
import gc

class DictTypeTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_subclassdict(self):
        """Test that a subclass of dict can be merged into a static dict"""
        out = {**os.environ, 'foo': 'bar'}

        self.assertTrue('foo' in out)


class BaseClassTestCase(unittest.TestCase):
    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_resolve_bases(self):
        import types

        class A: pass
        class B: pass
        class C:
            def __mro_entries__(self, bases):
                if A in bases:
                    return ()
                return (A,)
        c = C()
        self.assertEqual(types.resolve_bases(()), ())
        self.assertEqual(types.resolve_bases((c,)), (A,))
        self.assertEqual(types.resolve_bases((C,)), (C,))
        self.assertEqual(types.resolve_bases((A, C)), (A, C))
        self.assertEqual(types.resolve_bases((c, A)), (A,))
        self.assertEqual(types.resolve_bases((A, c)), (A,))
        x = (A,)
        y = (C,)
        z = (A, C)
        t = (A, C, B)
        for bases in [x, y, z, t]:
            self.assertIs(types.resolve_bases(bases), bases)


if __name__ == "__main__":
    unittest.main()
