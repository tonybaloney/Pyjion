import runpy
import pyjion
import sys

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(
        """
Usage: python -m pyjion <script.py> or python -m pyjion -m module ...args
        """
        )
        exit(1)
    pyjion.enable()
    if sys.argv[1] == "-m":
        runpy.run_module(sys.argv[2])
    else:
        runpy.run_path(sys.argv[1])
    pyjion.disable()
