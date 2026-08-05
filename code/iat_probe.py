# -*- coding: utf-8 -*-
"""解析 gamemd.exe 导入表，查 IAT 0x7E4C30 对应哪个 API（修正 section 字段顺序）"""
import struct
import sys

if len(sys.argv) < 2:
    sys.exit("usage: python %s <gamemd.exe>" % __file__)
data = open(sys.argv[1], "rb").read()
pe = struct.unpack_from("<I", data, 0x3C)[0]
n = struct.unpack_from("<H", data, pe + 6)[0]
opt = struct.unpack_from("<H", data, pe + 20)[0]
t = pe + 24 + opt
IMG = 0x400000
secs = []
for i in range(n):
    off = t + i * 40
    name = data[off:off + 8].rstrip(b"\0").decode("ascii", "replace")
    vsize, vaddr, rawsz, rawptr = struct.unpack_from("<IIII", data, off + 8)
    secs.append((name, vaddr, vsize, rawptr, rawsz))

def va2off(va):
    for name, vaddr, vsize, rawptr, rawsz in secs:
        v = IMG + vaddr
        if v <= va < v + max(vsize, rawsz):
            return rawptr + (va - v)
    return None

def cstr(off):
    e = data.find(b"\0", off)
    return data[off:e].decode("ascii", "replace")

imp_rva, imp_sz = struct.unpack_from("<II", data, pe + 0x80)
imp_off = va2off(IMG + imp_rva)
TARGET = 0x7E4C30
print("TARGET IAT slot VA = 0x%08X (section: %s)" % (TARGET,
    next((nm for nm, va, vs, rp, rs in secs if IMG + va <= TARGET < IMG + va + max(vs, rs)), "?")))

i = 0
while True:
    ent = imp_off + i * 20
    if ent + 20 > len(data):
        break
    oft, ts, name_rva, ft = struct.unpack_from("<IIII", data, ent)
    if name_rva == 0:
        break
    dll = cstr(va2off(IMG + name_rva))
    ift_off = va2off(IMG + oft) if oft else None
    if ift_off is None:
        i += 1
        continue
    k = 0
    while True:
        v = struct.unpack_from("<I", data, ift_off + k * 4)[0]
        if v == 0:
            break
        slot_va = IMG + ft + k * 4
        if slot_va == TARGET:
            if v & 0x80000000:
                print("FOUND: %s!ordinal %d" % (dll, v & 0xFFFF))
            else:
                print("FOUND: %s!%s" % (dll, cstr(va2off(IMG + v + 2))))
            raise SystemExit
        k += 1
    i += 1

print("0x7E4C30 NOT in IAT -> 打印该地址 4 字节原始值")
off = va2off(TARGET)
print("raw bytes at 0x%08X (off 0x%x): 0x%08x" % (TARGET, off, struct.unpack_from("<I", data, off)[0]))
