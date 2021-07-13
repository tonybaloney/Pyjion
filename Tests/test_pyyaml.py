import pyjion
import unittest
import gc

class PyyamlTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_load(self):
        content = """
linear: 
  - [1, 2, 3]
  - test
  - 1
        """
        import yaml
        data1 = yaml.safe_load(content)

        data2 = yaml.safe_load(content)

        data3 = yaml.safe_load(content)

        self.assertEqual(data1, data2)
        self.assertEqual(data1, data3)
        self.assertEqual(data2, data3)
        self.assertEqual(len(data1['linear']), 3)
        self.assertEqual(data1['linear'][0], [1, 2, 3])
        self.assertEqual(data1['linear'][1], 'test')
        self.assertEqual(data1['linear'][2], 1)