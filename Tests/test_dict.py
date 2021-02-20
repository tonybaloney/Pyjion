import pyjion
import unittest
import gc


class DictTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_bad_key(self):
        # Dictionary lookups should fail if __eq__() raises an exception.
        class CustomException(Exception):
            pass

        class BadDictKey:
            def __hash__(self):
                return hash(self.__class__)

            def __eq__(self, other):
                if isinstance(other, self.__class__):
                    raise CustomException
                return other

        d = {}
        x1 = BadDictKey()
        x2 = BadDictKey()
        d[x1] = 1

        with self.assertRaises(CustomException):
            d[x2] = 2
        with self.assertRaises(CustomException):
            z = d[x2]
        with self.assertRaises(CustomException):
            x2 in d
        with self.assertRaises(CustomException):
            d.setdefault(x2, 42)
        with self.assertRaises(CustomException):
            d.pop(x2)
        with self.assertRaises(CustomException):
            d.update({x2: 2})


if __name__ == "__main__":
    unittest.main()
