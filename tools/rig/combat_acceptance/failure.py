"""Lossless local-service exception evidence; no native or file side effects."""
import copy
import time
import traceback

def failure_record(error):
    result=dict(type=type(error).__name__,reason=str(error),
                traceback=''.join(traceback.format_exception(type(error),error,error.__traceback__)),
                qpcNs=time.perf_counter_ns())
    if hasattr(error,'evidence'):result['evidence']=copy.deepcopy(error.evidence)
    cause=error.__cause__
    if cause is not None and cause is not error:
        result['cause']=dict(type=type(cause).__name__,reason=str(cause))
        if hasattr(cause,'evidence'):result['cause']['evidence']=copy.deepcopy(cause.evidence)
    return result

def run_with_evidence(adapter):
    """Local service wrapper entry: retain the full original refusal, then raise."""
    from .profile import Profile
    try:return Profile(adapter).run()
    except BaseException as error:
        adapter.retain('source-terminal-failure',failure_record(error))
        raise
