# -*- coding: utf-8 -*-
"""读 .rdata 0x826590-0x8265D0 的命令行开关字符串 + FUN_0052F620 上下文"""
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

def va2off(va):
    for name, vaddr, vs, rawptr, rawsz in sections:
        v = 0x400000 + vaddr
        if v <= va < v + max(vs, rawsz):
            return rawptr + (va - v)
    return None

def cstr(va, maxlen=64):
    o = va2off(va)
    if o is None:
        return "<bad va>"
    e = data.find(b"\0", o, o + maxlen)
    return data[o:e].decode("ascii", "replace")

# 开关字符串区
for va in [0x8265bc, 0x8265b8, 0x8265b4, 0x8265b0, 0x8265ac, 0x8265a8, 0x82659c, 0x826598]:
    print("0x%06x: %r" % (va, cstr(va)))

# 整块 dump 上下文（开关字符串前后可能有更多）
print("\n--- block 0x826590-0x826600 ---")
o = va2off(0x826590)
raw = data[o:o + 0x70]
print(" ".join("%02x" % b for b in raw))
print(repr(raw))
