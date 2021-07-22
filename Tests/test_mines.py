import re
from base import PyjionTestCase
import unittest


class ProblemTestCase(PyjionTestCase):
    """
    Test problematic and complex functions
    """

    @unittest.skip("crashes on linux (loadhash, frame error)")
    def test_regexps(self):
        # TODO : Fix crash on dis.
        # print(pyjion.dis.dis(re.sre_compile.compile, True))
        # print(pyjion.dis.dis_native(re.sre_compile.compile, True))

        def by(s):
            return bytearray(map(ord, s))
        b = by("Hello, world")
        self.assertEqual(re.findall(br"\w+", b), [by("Hello"), by("world")])
