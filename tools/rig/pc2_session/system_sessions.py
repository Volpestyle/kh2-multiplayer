"""Read-only native process/session snapshot; no per-process open or elevation.

NtQuerySystemInformation(SystemProcessInformation) prefix is documented by
Microsoft; unsupported layouts, races and unavailable entries fail attribution.
"""
import ctypes as c
from ctypes import wintypes as w
import json,sys


class Unicode(c.Structure):
    _fields_=[('length',w.USHORT),('maximum',w.USHORT),('buffer',c.c_void_p)]


class Prefix(c.Structure):
    _fields_=[('next',w.ULONG),('threads',w.ULONG),('reserved',c.c_byte*48),
              ('image',Unicode),('priority',w.LONG),('pid',c.c_void_p),
              ('parent',c.c_void_p),('handles',w.ULONG),('session',w.ULONG)]


def snapshot():
    if c.sizeof(c.c_void_p)!=8 or Prefix.session.offset!=100:
        raise RuntimeError('unsupported native process/session layout')
    nt=c.WinDLL('ntdll')
    query=nt.NtQuerySystemInformation
    query.argtypes=[w.ULONG,c.c_void_p,w.ULONG,c.POINTER(w.ULONG)]
    query.restype=w.LONG
    size=1<<20
    for _ in range(8):
        buf=c.create_string_buffer(size);needed=w.ULONG()
        status=query(5,buf,size,c.byref(needed))
        if status==-1073741820:
            size=max(size*2,needed.value+65536)
            if size>128<<20:break
            continue
        if status<0:raise RuntimeError('native process snapshot failed: '+str(status))
        limit=needed.value
        if not c.sizeof(Prefix)<=limit<=size:raise RuntimeError('invalid native snapshot size')
        rows=[];offset=0;base=c.addressof(buf);seen=set()
        while True:
            if offset<0 or offset+c.sizeof(Prefix)>limit:raise RuntimeError('native record out of bounds')
            p=Prefix.from_buffer(buf,offset);pid=p.pid or 0
            if pid in seen:raise RuntimeError('duplicate native PID')
            seen.add(pid)
            if p.image.length:
                pointer=p.image.buffer
                if p.image.length%2 or not pointer or pointer<base or pointer+p.image.length>base+limit:
                    raise RuntimeError('native process name out of bounds')
                name=c.wstring_at(pointer,p.image.length//2)
            else:name=''
            created=c.c_longlong.from_buffer(buf,offset+32).value
            rows.append({'pid':pid,'parent':p.parent or 0,'name':name,
                         'session':p.session,'creationTicks':created})
            step=p.next
            if not step:break
            if step<c.sizeof(Prefix) or step%8:raise RuntimeError('invalid native continuation')
            offset+=step
        return rows
    raise RuntimeError('native process snapshot changed too often')


def verified_session(row, native_rows):
    matches=[p for p in native_rows if p['pid']==row['pid']
             and p['parent']==row['parent'] and p['name'].lower()==row['name'].lower()
             and p['creationTicks']>0]
    if len(matches)!=1:return None
    if row.get('creationTicks') and row['creationTicks']!=matches[0]['creationTicks']:return None
    return matches[0]


if __name__=='__main__':
    with open(sys.argv[1],'x') as out:json.dump({'source':'NtQuerySystemInformation','processes':snapshot()},out,indent=2)
