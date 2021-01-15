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
        result = a % ("world",)
        self.assertEqual(result, "Hello world")


if __name__ == "__main__":
    unittest.main()
