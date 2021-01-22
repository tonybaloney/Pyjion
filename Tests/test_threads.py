import pyjion
import unittest
import gc
import threading
import time
import random
import _testcapi
import test.support as support


class TestPendingCalls(unittest.TestCase):
    def setUp(self) -> None:
        pyjion.enable()

    def tearDown(self) -> None:
        pyjion.disable()
        gc.collect()

    def pendingcalls_submit(self, l, n):
        def callback():
            # this function can be interrupted by thread switching so let's
            # use an atomic operation
            l.append(None)

        for i in range(n):
            time.sleep(random.random() * 0.02)  # 0.01 secs on average
            # try submitting callback until successful.
            # rely on regular interrupt to flush queue if we are
            # unsuccessful.
            while True:
                if _testcapi._pending_threadfunc(callback):
                    break;

    def pendingcalls_wait(self, l, n, context=None):
        # now, stick around until l[0] has grown to 10
        count = 0;
        while len(l) != n:
            # this busy loop is where we expect to be interrupted to
            # run our callbacks.  Note that callbacks are only run on the
            # main thread
            print("(%i)" % (len(l),), )
            for i in range(1000):
                a = i * i
            if context and not context.event.is_set():
                continue
            count += 1
            self.assertTrue(count < 10000,
                            "timeout waiting for %i callbacks, got %i" % (n, len(l)))
        print("(%i)" % (len(l),))

    def test_pendingcalls_threaded(self):

        # do every callback on a separate thread
        n = 32  # total callbacks
        threads = []

        class foo(object): pass

        context = foo()
        context.l = []
        context.n = 2  # submits per thread
        context.nThreads = n // context.n
        context.nFinished = 0
        context.lock = threading.Lock()
        context.event = threading.Event()

        threads = [threading.Thread(target=self.pendingcalls_thread,
                                    args=(context,))
                   for i in range(context.nThreads)]
        with support.start_threads(threads):
            self.pendingcalls_wait(context.l, n, context)

    def pendingcalls_thread(self, context):
        try:
            self.pendingcalls_submit(context.l, context.n)
        finally:
            with context.lock:
                context.nFinished += 1
                nFinished = context.nFinished
                if False and support.verbose:
                    print("finished threads: ", nFinished)
            if nFinished == context.nThreads:
                context.event.set()

    def test_pendingcalls_non_threaded(self):
        # again, just using the main thread, likely they will all be dispatched at
        # once.  It is ok to ask for too many, because we loop until we find a slot.
        # the loop can be interrupted to dispatch.
        # there are only 32 dispatch slots, so we go for twice that!
        l = []
        n = 64
        self.pendingcalls_submit(l, n)
        self.pendingcalls_wait(l, n)
