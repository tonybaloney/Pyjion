from base import NoPgcPyjionTestCase


class GeneratorsTestCase(NoPgcPyjionTestCase):

    def test_single_yield(self):
        def gen():
            x = 1
            yield x
        g = gen()
        self.assertEqual(next(g), 1)

    def test_double_yield(self):
        def gen():
            x = 1
            yield x
            yield 2
        g = gen()
        self.assertEqual(next(g), 1)
        self.assertEqual(next(g), 2)

    def test_conditional_yield(self):
        def gen():
            x = 1
            if x == 1:
                yield x
            else:
                yield 2
        g = gen()
        self.assertEqual(next(g), 1)

    def test_yields_from_iterator(self):
        def gen():
            yield 1
            yield 2
            yield 3

        g = gen()
        result = list(g)
        self.assertEqual(result, [1, 2, 3])

    def test_yields_from_range_gen(self):
        def gen():
            for n in range(10):
                yield f'{n}!'

        result = []
        for x in gen():
            result.append(x)
        self.assertEqual(result, ['0!', '1!', '2!', '3!', '4!', '5!', '6!', '7!', '8!', '9!'])

    def test_yields_from_range_gen_listcomp(self):
        def gen():
            for n in range(10):
                yield f'{n}!'

        result = [x for x in gen()]
        self.assertEqual(result, ['0!', '1!', '2!', '3!', '4!', '5!', '6!', '7!', '8!', '9!'])
