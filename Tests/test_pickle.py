import pyjion
import unittest
import gc
import io


class PickleTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_simple_pickle(self):
        import pickle

        f = io.BytesIO()

        # An arbitrary collection of objects supported by pickle.
        data = {
            'a': [1, 2.0, 3, 4+6j],
            'b': ("character string", b"byte string"),
            'c': {None, True, False}
        }

        pic = pickle.dumps(data, pickle.HIGHEST_PROTOCOL)
        output = pickle.loads(pic)
        self.assertEqual(data, output)
