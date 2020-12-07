import pyjion
import unittest
import os

class DictTypeTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()

    def test_subclassdict(self):
        """Test that a subclass of dict can be merged into a static dict"""
        out = {**os.environ, 'foo': 'bar'}

        self.assertTrue('foo' in out)


if __name__ == "__main__":
    unittest.main()
