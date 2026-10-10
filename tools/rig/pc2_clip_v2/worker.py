"""Retain one synchronous clip04 call; cancellation never discards its outcome."""
import math
import threading
import time
import traceback
import copy


class RetainedClipWorker:
    def __init__(self, call, *, cancel, deadline):
        if not callable(call) or not isinstance(cancel, threading.Event):
            raise ValueError('clip callable and original Event required')
        if type(deadline) is not float or not math.isfinite(deadline) or deadline <= time.perf_counter():
            raise ValueError('original future local QPC deadline required')
        self._call, self.cancel_event, self.deadline = call, cancel, deadline
        self._thread = None
        self._done = threading.Event()
        self._lock = threading.Lock()
        self._result = self._error = None
        self.started_ns = self.finished_ns = None
        self.cancel_reason = None

    def start(self):
        with self._lock:
            if self._thread is not None:
                raise RuntimeError('worker start is single-use')
            if self.cancel_event.is_set() or time.perf_counter() >= self.deadline:
                raise RuntimeError('worker admission cancelled/expired')
            self.started_ns = time.perf_counter_ns()
            self._thread = threading.Thread(target=self._run, name='owned-combat-clip', daemon=False)
            self._thread.start()
        return self

    def _run(self):
        try:
            if self.cancel_event.is_set() or time.perf_counter() >= self.deadline:
                raise RuntimeError('worker final admission cancelled/expired')
            self._result = self._call()
            if self.cancel_event.is_set() or time.perf_counter() >= self.deadline:
                raise RuntimeError('clip returned after cancellation/deadline')
        except BaseException as error:
            self._error = dict(type=type(error).__name__, message=str(error), traceback=traceback.format_exc())
        finally:
            self.finished_ns = time.perf_counter_ns()
            self._done.set()

    def poll(self):
        with self._lock:
            thread = self._thread
            joined = thread is not None and self._done.is_set() and not thread.is_alive()
            state = ('NOT_STARTED' if thread is None else 'RUNNING' if not joined
                     else 'FAILED' if self._error else 'FINISHED')
            return dict(schema='combat-clip-worker-v1', state=state, joined=joined,
                        threadId=thread.ident if thread else None, startedNs=self.started_ns,
                        finishedNs=self.finished_ns, cancelRequested=self.cancel_event.is_set(),
                        cancelReason=self.cancel_reason, error=copy.deepcopy(self._error),
                        result=copy.deepcopy(self._result) if joined else None,
                        descendantsAbsent=None)

    def finish(self, *, deadline):
        if type(deadline) is not float or not math.isfinite(deadline):
            raise ValueError('bounded local finish deadline required')
        if self._thread is None:
            raise RuntimeError('worker not started')
        self._thread.join(max(0, min(deadline, self.deadline) - time.perf_counter()))
        return self.poll()

    def cancel(self, reason, *, cleanup_deadline):
        if type(reason) is not str or not reason:
            raise ValueError('cancellation reason required')
        if type(cleanup_deadline) is not float or not math.isfinite(cleanup_deadline):
            raise ValueError('explicit bounded cleanup deadline required')
        self.cancel_reason = self.cancel_reason or reason
        self.cancel_event.set()  # the original Event; never a replacement
        if self._thread is not None:
            self._thread.join(max(0, cleanup_deadline - time.perf_counter()))
        # A joined thread is NOT native descendant closure. Canonical receipts
        # and fresh identity absence remain independently mandatory.
        return self.poll()
