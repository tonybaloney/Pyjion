import pyjion
import re
import os
import gc
from multiprocessing import Pool, TimeoutError
import unittest
import argparse
import io
import pathlib
from contextlib import redirect_stdout, redirect_stderr
from rich.console import Console

console = Console()
TIMEOUT = 60


def run_test(test, level, pgc):
    test_cases = unittest.defaultTestLoader.loadTestsFromName(f"test.{test}")
    reasons = ""
    pass_count = 0
    fail_count = 0
    for case in test_cases:
        pyjion.enable()
        pyjion.config(level=level, pgc=pgc, graph=True, debug=True)
        r = unittest.result.TestResult()
        with redirect_stderr(io.StringIO()) as _:
            with redirect_stdout(io.StringIO()) as _:
                case.run(r)
        if r.wasSuccessful():
            pass_count += 1
        else:
            fail_count += 1

            for failedcase, reason in r.expectedFailures:
                reasons += f"---------------------------------------------------------------\n"
                reasons += f"Test case {failedcase} was expected to fail:\n"
                reasons += reason
                reasons += f"\n---------------------------------------------------------------\n"

            for failedcase, reason in r.failures:
                reasons += f"---------------------------------------------------------------\n"
                reasons += f"Test case {failedcase} failed:\n"
                reasons += reason
                reasons += f"\n---------------------------------------------------------------\n"

            for failedcase, reason in r.errors:
                reasons += f"---------------------------------------------------------------\n"
                reasons += f"Test case {failedcase} failed with errors:\n"
                reasons += reason
                reasons += f"\n---------------------------------------------------------------\n"

        pyjion.disable()
        gc.collect()
    return test, fail_count, pass_count, reasons


def main(input_file, opt_level, pgc):
    SAVEDCWD = os.getcwd()
    tests = []
    logs = []
    console.print(f"Trying with Optimizations = {opt_level}, PGC = {pgc}")

    # Lifted from libregr.main
    regex = re.compile(r'\btest_[a-zA-Z0-9_]+\b')
    with open(os.path.join(SAVEDCWD, input_file)) as fp:
        for line in fp:
            line = line.split('#', 1)[0]
            line = line.strip()
            match = regex.search(line)
            if match is not None:
                tests.append(match.group())

    def result_f(args):
        case, fail_count, pass_count, r = args
        if not fail_count:
            console.print(f":white_check_mark: {case} ({pass_count} Pass, {fail_count} Failed)")
        else:
            console.print(f":cross_mark: {case} ({pass_count} Pass, {fail_count} Failed)")
            logs.append((case, r))

    with Pool(processes=4) as pool:
        multiple_results = [pool.apply_async(run_test, (test, opt_level, pgc), callback=result_f) for test in tests]
        for res in multiple_results:
            try:
                res.get(timeout=TIMEOUT)
            except TimeoutError:
                console.print(f":cross_mark: Case timed out")  # ToDO: work out which one.
            except unittest.case.SkipTest:
                console.print(f":cross_mark: Skipping test")

    log_path = pathlib.Path(SAVEDCWD) / "Tests" / "logs"
    if not log_path.exists():
        log_path.mkdir()
    for case, reason in logs:
        with open(log_path / (case + ".txt"), "w") as f:
            f.write(reason)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Pyjion smoke tests")
    parser.add_argument('test', nargs='?', help='run an individual test')
    parser.add_argument('-f', '--fromfile', metavar='FILE',
                        help='read names of tests to run from a file.')
    parser.add_argument('-o', '--opt-level', type=int,
                        default=1,
                        help='target optimization level')
    parser.add_argument('--pgc', action='store_true', help='Enable PGC')
    args = parser.parse_args()
    if args.test:
        case, fail_count, pass_count, r = run_test(args.test, args.opt_level, args.pgc)
        if not fail_count:
            console.print(f":white_check_mark: {case} ({pass_count} Pass, {fail_count} Failed)")
        else:
            console.print(f":cross_mark: {case} ({pass_count} Pass, {fail_count} Failed)")
        console.print(r)
    else:
        main(args.fromfile, args.opt_level, args.pgc)
