"""Read-only CaptureChannel prefix from reviewed JOIN27; never arms a request."""
import ctypes as c
import struct

def snapshot(pid):
    k=c.WinDLL('kernel32',use_last_error=True)
    k.OpenFileMappingW.argtypes=[c.c_uint32,c.c_int,c.c_wchar_p];k.OpenFileMappingW.restype=c.c_void_p
    k.MapViewOfFile.argtypes=[c.c_void_p,c.c_uint32,c.c_uint32,c.c_uint32,c.c_size_t];k.MapViewOfFile.restype=c.c_void_p
    k.UnmapViewOfFile.argtypes=[c.c_void_p];k.CloseHandle.argtypes=[c.c_void_p]
    handle=k.OpenFileMappingW(4,False,'Local\\kh2coop_capture_'+str(pid))
    if not handle:raise c.WinError(c.get_last_error())
    view=None
    try:
        view=k.MapViewOfFile(handle,4,0,0,1096)
        if not view:raise c.WinError(c.get_last_error())
        data=c.string_at(view,1096)
        values=struct.unpack('<14I',data[:56])
        assert values[2]==struct.unpack('<I',c.string_at(view+8,4))[0],'capture request changed during snapshot'
        assert values[:2]==(0x50433248,1),'capture channel version'
        names=('magic','version','request','done','status','frameCount','interval','written','width','height','overlay','presents','renderer','format')
        row=dict(zip(names,values));row['output']=data[56:].decode('utf-16-le').split('\0',1)[0]
        return row
    finally:
        if view:k.UnmapViewOfFile(view)
        k.CloseHandle(handle)
