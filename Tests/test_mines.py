"""
Test problematic and complex functions
"""
import re


def test_regexps():
    # TODO : Fix crash on dis.
    # print(pyjion.dis.dis(re.sre_compile.compile, True))
    # print(pyjion.dis.dis_native(re.sre_compile.compile, True))

    def by(s):
        return bytearray(map(ord, s))
    b = by("Hello, world")
    assert re.findall(br"\w+", b) == [by("Hello"), by("world")]


import tqdm

def test_tqdm():
    for _ in tqdm.tqdm(range(10)):
        pass