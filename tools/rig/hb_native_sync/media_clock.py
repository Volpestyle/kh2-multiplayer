"""Source6-local QPC clock. Never replaces Python/global/source5 time functions.

Python 3.11 on Windows uses coarse GetTickCount64 for time.monotonic(), unlike
perf_counter's QPC. Media timestamps and absolute deadlines must use ONE clock.
"""
import time as system_time
from types import SimpleNamespace
clock=SimpleNamespace(monotonic=system_time.perf_counter,monotonic_ns=system_time.perf_counter_ns,perf_counter_ns=system_time.perf_counter_ns)
