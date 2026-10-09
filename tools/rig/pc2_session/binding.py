"""Runtime consent/active-console/native-driver binding from ADOPTed smoke07."""
def active_console_session():
    import ctypes
    get=ctypes.WinDLL('kernel32',use_last_error=True).WTSGetActiveConsoleSessionId
    get.argtypes=[];get.restype=ctypes.c_uint32
    return int(get())

def require_session(expected,rows,pid,active):
    assert type(expected) is int and 0<expected<0xffffffff,'Consent requires a nonzero desktop session'
    assert active==expected,'Active console differs from consent session'
    me=[x for x in rows if x['pid']==pid]
    assert len(me)==1 and me[0]['session']==expected,'Driver differs from consent desktop session'
    return me[0]

