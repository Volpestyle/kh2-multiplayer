"""Read-only owned process identity/RPM. All writes stay in canonical kh2ctl."""
import ctypes as c
from ctypes import wintypes as w
from pathlib import Path
import hashlib, struct
from party_leaf import Refused

def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()

class Module(c.Structure):
    _fields_ = [('size',w.DWORD),('id',w.DWORD),('pid',w.DWORD),('globalUsage',w.DWORD),
                ('processUsage',w.DWORD),('base',c.c_void_p),('bytes',w.DWORD),('handle',w.HMODULE),
                ('name',w.WCHAR*256),('path',w.WCHAR*260)]

class ReadOnly:
    def __init__(self, pid, owned, expected_exe, expected_dll):
        self.pid, self.owned, self.expected_exe, self.expected_dll = pid, Path(owned), expected_exe, expected_dll
        self.k = c.WinDLL('kernel32', use_last_error=True)
        signatures = {
          'OpenProcess':([w.DWORD,w.BOOL,w.DWORD],w.HANDLE),
          'CloseHandle':([w.HANDLE],w.BOOL),
          'ReadProcessMemory':([w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)],w.BOOL),
          'GetProcessTimes':([w.HANDLE,c.POINTER(w.FILETIME),c.POINTER(w.FILETIME),c.POINTER(w.FILETIME),c.POINTER(w.FILETIME)],w.BOOL),
          'GetExitCodeProcess':([w.HANDLE,c.POINTER(w.DWORD)],w.BOOL),
          'CreateToolhelp32Snapshot':([w.DWORD,w.DWORD],w.HANDLE),
          'Module32FirstW':([w.HANDLE,c.POINTER(Module)],w.BOOL),
          'Module32NextW':([w.HANDLE,c.POINTER(Module)],w.BOOL)}
        for name,(args,ret) in signatures.items():
            f=getattr(self.k,name);f.argtypes=args;f.restype=ret
        self.h=self.k.OpenProcess(0x1010,False,pid)
        if not self.h: raise c.WinError(c.get_last_error())
        try:
            self.creation=self.times()
            self.module_identity=None
            self.identity()
        except BaseException:
            self.close();raise
    def times(self):
        times=[w.FILETIME() for _ in range(4)]
        if not self.k.GetProcessTimes(self.h,*[c.byref(t) for t in times]): raise c.WinError(c.get_last_error())
        return times[0].dwLowDateTime | times[0].dwHighDateTime<<32
    def identity(self):
        exitcode=w.DWORD()
        if not self.k.GetExitCodeProcess(self.h,c.byref(exitcode)) or exitcode.value!=259: raise Refused('owned process not alive')
        if self.times()!=self.creation: raise Refused('creation identity changed')
        rows=[line.split() for line in self.owned.read_text().splitlines() if line.strip()]
        if [str(self.pid),str(self.creation)] not in rows: raise Refused('PID/creation absent from canonical owned.txt')
        snap=self.k.CreateToolhelp32Snapshot(0x18,self.pid)
        if snap==c.c_void_p(-1).value: raise c.WinError(c.get_last_error())
        mods=[]
        try:
            m=Module();m.size=c.sizeof(m);ok=self.k.Module32FirstW(snap,c.byref(m))
            while ok:
                mods.append({'name':m.name,'path':m.path,'base':m.base,'bytes':m.bytes})
                ok=self.k.Module32NextW(snap,c.byref(m))
        finally: self.k.CloseHandle(snap)
        exe=[m for m in mods if m['name'].lower()=='kingdom hearts ii final mix.exe']
        dll=[m for m in mods if 'kh2coop_inject' in m['name'].lower()]
        if len(exe)!=1 or len(dll)!=1: raise Refused('unique loaded EXE/DLL unavailable')
        actual=[exe[0],dll[0]]
        if [sha(m['path']) for m in actual]!=[self.expected_exe,self.expected_dll]: raise Refused('loaded product hash mismatch')
        if self.module_identity is not None and actual!=self.module_identity: raise Refused('loaded module identity changed')
        self.module_identity=actual;self.base=exe[0]['base']
        if self.read(self.base+0x9A98B0,4)!=b'KH2J': raise Refused('KH2J magic mismatch')
        return {'pid':self.pid,'creationTime':self.creation,'modules':actual}
    def read(self,address,size):
        if not 0x10000<=address<0x800000000000-size: raise Refused('invalid read span')
        data=c.create_string_buffer(size);n=c.c_size_t()
        if not self.k.ReadProcessMemory(self.h,address,data,size,c.byref(n)) or n.value!=size: raise Refused('partial/unavailable RPM')
        return data.raw
    def value(self,address,fmt): return struct.unpack('<'+fmt,self.read(address,struct.calcsize('<'+fmt)))[0]
    def close(self):
        if self.h:self.k.CloseHandle(self.h);self.h=None
