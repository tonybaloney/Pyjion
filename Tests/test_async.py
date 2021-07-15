import gc
import pyjion
import unittest
import asyncio


class AsyncGeneratorTestCase(unittest.TestCase):

    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def test_basic_generator(self):
        async def yield_values():
            for n in range(10):
                yield n

        async def calculate_generator():
            total = 1
            async for i in yield_values():
                total += i
            return total

        result = asyncio.run(calculate_generator())
        self.assertEqual(result, 46)
