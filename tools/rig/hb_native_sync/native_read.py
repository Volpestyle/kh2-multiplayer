"""Read-only handles for explicit runner-owned PIDs; never creates a mapping."""
import ctypes
from ctypes import wintypes as W
import struct


class Reader:
    def __init__(self,pid,base):
        self.pid=pid; self.base=base
        self.k=ctypes.WinDLL('kernel32',use_last_error=True)
        self.k.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD]; self.k.OpenProcess.restype=W.HANDLE
        self.k.ReadProcessMemory.argtypes=[W.HANDLE,W.LPCVOID,W.LPVOID,ctypes.c_size_t,ctypes.POINTER(ctypes.c_size_t)]
        self.k.ReadProcessMemory.restype=W.BOOL
        self.k.CloseHandle.argtypes=[W.HANDLE]
        self.handle=self.k.OpenProcess(0x1010,False,pid) # QUERY_LIMITED_INFORMATION | VM_READ
        if not self.handle: raise OSError('owned process read handle unavailable')
    def read(self,address,size):
        if not 0x10000<=address<0x800000000000-size or not 1<=size<=0x10FC0:
            raise ValueError('read outside finite user span')
        raw=ctypes.create_string_buffer(size); count=ctypes.c_size_t()
        if not self.k.ReadProcessMemory(self.handle,address,raw,size,ctypes.byref(count)) or count.value!=size:
            raise OSError('owned native read failed')
        return raw.raw
    def value(self,rva,fmt): return struct.unpack('<'+fmt,self.read(self.base+rva,struct.calcsize('<'+fmt)))[0]
    def close(self):
        if self.handle: self.k.CloseHandle(self.handle); self.handle=None


class Mapping:
    def __init__(self,pid,prefix="world",size=128):
        self.k=ctypes.WinDLL('kernel32',use_last_error=True)
        self.k.OpenFileMappingW.argtypes=[W.DWORD,W.BOOL,W.LPCWSTR];self.k.OpenFileMappingW.restype=W.HANDLE
        self.k.MapViewOfFile.argtypes=[W.HANDLE,W.DWORD,W.DWORD,W.DWORD,ctypes.c_size_t];self.k.MapViewOfFile.restype=W.LPVOID
        self.k.UnmapViewOfFile.argtypes=[W.LPCVOID];self.k.CloseHandle.argtypes=[W.HANDLE]
        self.handle=self.k.OpenFileMappingW(4,False,'Local\\kh2coop_'+prefix+'_'+str(pid)) # FILE_MAP_READ
        self.size=size
        self.view=self.k.MapViewOfFile(self.handle,4,0,0,size) if self.handle else None
        if not self.view:
            self.close();raise OSError('owned existing world mapping unavailable')
    def read(self): return ctypes.string_at(self.view,self.size)
    def close(self):
        if getattr(self,'view',None):self.k.UnmapViewOfFile(self.view);self.view=None
        if getattr(self,'handle',None):self.k.CloseHandle(self.handle);self.handle=None
