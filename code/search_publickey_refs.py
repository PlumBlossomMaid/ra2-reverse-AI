# -*- coding: utf-8 -*-
"""搜 vtable 全部 16 个槽位地址的引用 + dump 表前内容"""
import struct
import sys

if len(sys.argv) < 2:
    sys.exit("usage: python %s <gamemd.exe>" % __file__)
data = open(sys.argv[1], "rb").read()
pe = struct.unpack_from("<I", data, 0x3C)[0]
n = struct.unpack_from("<H", data, pe + 6)[0]
opt = struct.unpack_from("<H", data, pe + 20)[0]
t = pe + 24 + opt

sections = []
for i in range(n):
    off = t + i * 40
    name = data[off:off + 8].rstrip(b"\0").decode("ascii", "replace")
    vs, vaddr, rawsz, rawptr = struct.unpack_from("<IIII", data, off + 8)
    sections.append((name, vaddr, vs, rawptr, rawsz))

def off2va(off):
    for name, vaddr, vs, rawptr, rawsz in sections:
        if rawptr <= off < rawptr + rawsz:
            return 0x400000 + vaddr + (off - rawptr)
    return None

def va2off(va):
    for name, vaddr, vs, rawptr, rawsz in sections:
        v = 0x400000 + vaddr
        if v <= va < v + max(vs, rawsz):
            return rawptr + (va - v)
    return None

# 16 个槽位 + 起始
found_any = False
for slot in range(0x7E1A40, 0x7E1A84, 4):
    pat = struct.pack("<I", slot)
    hits = []
    start = 0
    while True:
        i = data.find(pat, start)
        if i == -1:
            break
        va = off2va(i)
        sec = "?"
        for name, vaddr, vs, rawptr, rawsz in sections:
            if rawptr <= i < rawptr + rawsz:
                sec = name
                break
        hits.append((va, sec, i))
        start = i + 1
    if hits:
        found_any = True
        print("slot 0x%08X: %d hits" % (slot, len(hits)))
        for va, sec, off in hits:
            print("   0x%08X [%s] ctx=%s" % (va, sec, " ".join("%02x" % b for b in data[off - 8:off + 12])))

if not found_any:
    print("ALL 16 SLOTS: 0 references (module fully isolated)")

# dump 表前内容 0x7E19C0-0x7E1A40
print("\n-- 0x7E19C0-0x7E1A40 --")
off = va2off(0x7E19C0)
for i in range(0, 0x80, 4):
    va = 0x7E19C0 + i
    v = struct.unpack_from("<I", data, off + i)[0]
    kind = ""
    if 0x400000 <= v < 0x500000:
        kind = "<- code?"
    elif 0x7E0000 <= v < 0x840000:
        kind = "<- .rdata?"
    elif v == 0:
        kind = "zero"
    print("0x%08X: %08X %s" % (va, v, kind))
