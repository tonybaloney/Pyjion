import contextlib
import io

import pyjion
import pyjion.dis
import unittest

from base import PyjionTestCase


class GlobalOptimizationTestCase(PyjionTestCase):

    def test_import(self):
        def _f():
            print("foo foo")
            return 2

        self.assertEqual(_f(), 2)
        info = pyjion.info(_f)
        self.assertTrue(info['compiled'])
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            pyjion.dis.dis(_f)
        self.assertIn("ldarg.1", f.getvalue())
        self.assertIn("METHOD_LOADGLOBAL_HASH", f.getvalue())


if __name__ == "__main__":
    unittest.main()
