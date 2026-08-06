# -*- coding: utf-8 -*-
"""gamemd.exe 文件偏移 -> VA 转换（按节表映射），为 Ghidra xref 查询准备地址"""
import struct

data = open(r"E:\YRLauncher\gamemd.exe", "rb").read()

pe_off = struct.unpack_from("<I", data, 0x3C)[0]
assert data[pe_off:pe_off + 4] == b"PE\0\0"
num_sections = struct.unpack_from("<H", data, pe_off + 6)[0]
opt_size = struct.unpack_from("<H", data, pe_off + 20)[0]
image_base = struct.unpack_from("<I", data, pe_off + 24 + 28)[0]
sec_off = pe_off + 24 + opt_size

sections = []
for i in range(num_sections):
    off = sec_off + i * 40
    name = data[off:off + 8].rstrip(b"\0").decode()
    vsize, vaddr, rsize, roff = struct.unpack_from("<IIII", data, off + 8)
    sections.append((name, vaddr, vsize, roff, rsize))
    print("%-8s VA=%08x VS=%08x RawOff=%08x RawSz=%08x" % (name, vaddr, vsize, roff, rsize))

print("\nimage_base = 0x%08x" % image_base)

# 之前从 exe 里搜到的随机地图相关字符串（文件偏移）
targets = [
    (0x429abc, "RandMap.img"),
    (0x42bb24, "*.mmp / rmcache\\"),
    (0x42bb44, "rmcache\\RandMap.Map / rmcache"),
    (0x42bc30, "RandMap.Sed / lastmap.sed / GUI:LoadMapMenu"),
    (0x43da5e, '".SED" / "Scen->IsRandom = true"'),
]
for t, label in targets:
    va = None
    for name, vaddr, vsize, roff, rsize in sections:
        if roff <= t < roff + max(rsize, 1):
            va = image_base + vaddr + (t - roff)
            break
    print("file 0x%06x -> VA 0x%08x  (%s)" % (t, va if va else 0, label))
