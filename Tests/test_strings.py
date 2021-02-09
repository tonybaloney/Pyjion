import sys
sys.path.append('/Users/anthonyshaw/CLionProjects/pyjion/src')
import pyjion
import unittest
import gc


class StringFormattingTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_perc_format(self):
        a = "Hello %s"
        before_ref = sys.getrefcount(a)
        c = a % ("world",)
        self.assertEqual(before_ref, sys.getrefcount(a))
        self.assertEqual(c, "Hello world")
        b = "w0rld"
        before_ref_b = sys.getrefcount(b)
        before_ref_c = sys.getrefcount(c)
        c += a % b
        self.assertEqual(sys.getrefcount(a), before_ref)
        self.assertEqual(sys.getrefcount(b), before_ref_b)
        self.assertEqual(sys.getrefcount(c), before_ref_c)
        self.assertEqual(c, "Hello worldHello w0rld")
        c += a % ("x", )

    def test_add_inplace(self):
        c = "..."
        a = "Hello "
        b = "world!"
        before_ref = sys.getrefcount(a)
        before_ref_b = sys.getrefcount(b)
        before_ref_c = sys.getrefcount(c)
        c += a + b
        self.assertEqual(sys.getrefcount(a), before_ref, )
        self.assertEqual(sys.getrefcount(b), before_ref_b, )
        self.assertEqual(sys.getrefcount(c), before_ref_c - 1 )
        self.assertEqual(c, "...Hello world!")

    def test_many_fstring_expressions(self):
        # Create a string with many expressions in it. Note that
        #  because we have a space in here as a literal, we're actually
        #  going to use twice as many ast nodes: one for each literal
        #  plus one for each expression.
        def build_fstr(n, extra=''):
            return "f'" + ('{x} ' * n) + extra + "'"

        x = 'X'
        width = 1

        # Test around 256.
        for i in range(250, 260):
            self.assertEqual(eval(build_fstr(i)), (x+' ')*i)

        # Test concatenating 2 largs fstrings.
        self.assertEqual(eval(build_fstr(255)*256), (x+' ')*(255*256))

        s = build_fstr(253, '{x:{width}} ')
        self.assertEqual(eval(s), (x+' ')*254)

        # Test lots of expressions and constants, concatenated.
        s = "f'{1}' 'x' 'y'" * 1024
        self.assertEqual(eval(s), '1xy' * 1024)

if __name__ == "__main__":
    unittest.main()
