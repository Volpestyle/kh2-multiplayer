"""Rough backtrace from a minidump without a debugger: the faulting thread's
stack is scanned for qwords that point into a loaded module, printed as
module+offset (most likely return addresses, plus some stale values).

    python tools/scenario/spikes/dumpstack.py crash.dmp [--depth 60]
"""

import struct
import sys
from pathlib import Path


def main() -> int:
    path = Path(sys.argv[1])
    depth = int(sys.argv[sys.argv.index("--depth") + 1]) if "--depth" in sys.argv else 60
    d = path.read_bytes()
    _, _, nstreams, dir_rva = struct.unpack_from("<IIII", d, 0)
    streams = {}
    for i in range(nstreams):
        stype, size, rva = struct.unpack_from("<III", d, dir_rva + 12 * i)
        streams[stype] = (size, rva)

    # Modules (stream 4): base, size, name.
    modules = []
    _, rva = streams[4]
    (count,) = struct.unpack_from("<I", d, rva)
    for i in range(count):
        off = rva + 4 + 108 * i
        base, size = struct.unpack_from("<QI", d, off)
        (name_rva,) = struct.unpack_from("<I", d, off + 20)
        (nlen,) = struct.unpack_from("<I", d, name_rva)
        name = d[name_rva + 4:name_rva + 4 + nlen].decode("utf-16-le").split("\\")[-1]
        modules.append((base, size, name))

    def where(addr: int) -> str | None:
        for base, size, name in modules:
            if base <= addr < base + size:
                return f"{name}+0x{addr - base:X}"
        return None

    # Exception (stream 6): thread id, code, address, thread context.
    _, rva = streams[6]
    tid, = struct.unpack_from("<I", d, rva)
    code, _, _, addr = struct.unpack_from("<IIQQ", d, rva + 8)
    ctx_size, ctx_rva = struct.unpack_from("<II", d, rva + 8 + 152)
    rsp, = struct.unpack_from("<Q", d, ctx_rva + 0x98)
    rip, = struct.unpack_from("<Q", d, ctx_rva + 0xF8)
    print(f"thread {tid} exception 0x{code:08X} at {where(addr) or hex(addr)} rip {where(rip) or hex(rip)}")
    for reg, off in (("rax", 0x78), ("rcx", 0x80), ("rdx", 0x88), ("rbx", 0x90), ("rsi", 0xA8), ("rdi", 0xB0)):
        val, = struct.unpack_from("<Q", d, ctx_rva + off)
        print(f"  {reg} = 0x{val:016X} {where(val) or ''}")

    # Thread list (stream 3): find the stack memory of that thread.
    _, rva = streams[3]
    (count,) = struct.unpack_from("<I", d, rva)
    stack = None
    for i in range(count):
        off = rva + 4 + 48 * i
        t, = struct.unpack_from("<I", d, off)
        if t == tid:
            start, size, mem_rva = struct.unpack_from("<QII", d, off + 24)
            stack = (start, size, mem_rva)
    if not stack:
        print("faulting thread's stack not in the dump")
        return 1
    start, size, mem_rva = stack
    print(f"stack 0x{start:X}+0x{size:X}, scanning from rsp 0x{rsp:X}")
    shown = 0
    for a in range(max(rsp, start), start + size - 7, 8):
        val, = struct.unpack_from("<Q", d, mem_rva + (a - start))
        w = where(val)
        # A module base (+0x0) is data, not a return address.
        if w and not w.endswith("+0x0"):
            print(f"  [rsp+0x{a - rsp:X}] {w}")
            shown += 1
            if shown >= depth:
                break
    return 0


if __name__ == "__main__":
    sys.exit(main())
