import pyjion
import gc
from test.libregrtest import main

pyjion.enable()
main()
pyjion.disable()
gc.collect()
