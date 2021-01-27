import gc
import pyjion
import pyjion.dis
import unittest
import io
import contextlib


class KnownMethodsBuiltins(unittest.TestCase):
    """
    Test that methods of known, builtin types will resolve into cached variables and bypass PyJit_LoadMethod
    """

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def assertOptimized(self, func) -> None:
        self.assertTrue(pyjion.info(func)['compiled'])
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            pyjion.dis.dis(func)
        self.assertNotIn("MethodTokens.METHOD_LOAD_METHOD", f.getvalue())


    def test_dict_keys(self):
        def test_f():
            l = {'a': 1, 'b': 2}
            k = l.keys()
            return tuple(k)
        self.assertEqual(test_f(), ('a', 'b'))
        self.assertOptimized(test_f)


if __name__ == "__main__":
    unittest.main()
