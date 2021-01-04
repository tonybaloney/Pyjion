import sys
sys.path.append("/Users/anthonyshaw/CLionProjects/pyjion/src")

import pyjion
import re
import os
import gc
import test.libregrtest.cmdline
import unittest

args = test.libregrtest.cmdline._parse_args(sys.argv[1:])

SAVEDCWD = os.getcwd()
tests = []

# Lifted from libregr.main
regex = re.compile(r'\btest_[a-zA-Z0-9_]+\b')
with open(os.path.join(SAVEDCWD, args.fromfile)) as fp:
    for line in fp:
        line = line.split('#', 1)[0]
        line = line.strip()
        match = regex.search(line)
        if match is not None:
            tests.append(match.group())

for test in tests:
    test_cases = unittest.defaultTestLoader.loadTestsFromName(f"test.{test}")
    pyjion.enable()
    result = test_cases.run(unittest.result.TestResult())
    print(result)
    pyjion.disable()
    gc.collect()
