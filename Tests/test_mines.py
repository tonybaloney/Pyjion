import pyjion
import pyjion.dis
import unittest
import gc
import re


class ProblemTestCase(unittest.TestCase):
    """
    Test problematic and complex functions
    """

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    # def test_regexps(self):
    #     print(pyjion.dis.dis(re.sre_compile.compile, True))
    #     print(pyjion.dis.dis_native(re.sre_compile.compile, True))

    #     def by(s):
    #         return bytearray(map(ord, s))
    #     b = by("Hello, world")
    #     self.assertEqual(re.findall(br"\w+", b), [by("Hello"), by("world")])
