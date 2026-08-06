# -*- coding: utf-8 -*-
"""在 gamemd.exe 的 .text 里搜索「把某个绝对地址当作立即数/内存操作数」的引用点。

MSVC 6.0 对全局变量一律用绝对寻址，所以直接按小端 4 字节搜就能找到全部引用，
比开 Ghidra 查 xref 快得多。命中后用 disasm.py 看上下文即可。

用法: python code/randmap/xref_abs.py 0xABED10 [更多地址...]
"""
import struct
import sys

EXE = r"E:\YRLauncher\gamemd.exe"


def load_pe(path):
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    secs = []
    for i in range(nsec):
        o = pe + 24 + optsz + i * 40
        name = data[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, vaddr, rsize, roff = struct.unpack_from("<IIII", data, o + 8)
        secs.append((name, vaddr, vsize, roff, rsize))
    return data, base, secs


def find_refs(target, exe=EXE, section=".text"):
    data, base, secs = load_pe(exe)
    needle = struct.pack("<I", target)
    hits = []
    for name, vaddr, vsize, roff, rsize in secs:
        if section and name != section:
            continue
        i = roff
        end = roff + rsize
        while True:
            i = data.find(needle, i, end)
            if i < 0:
                break
            hits.append(base + vaddr + (i - roff))
            i += 1
    return hits


def main():
    for a in sys.argv[1:]:
        t = int(a, 0)
        hits = find_refs(t)
        print("0x%08X : %d 处引用" % (t, len(hits)))
        for h in hits:
            print("    操作数@0x%08X" % h)


if __name__ == "__main__":
    main()
