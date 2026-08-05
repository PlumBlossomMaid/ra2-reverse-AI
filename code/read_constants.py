# -*- coding: utf-8 -*-
"""读取 gamemd.exe 的 .rdata 中 0x7f4e34 处的 float 常量（防除零值）"""
import struct

path = r"E:\YRLauncher\gamemd.exe"
with open(path, "rb") as f:
    data = f.read()

# PE 头解析
pe_off = struct.unpack_from("<I", data, 0x3C)[0]
num_sections = struct.unpack_from("<H", data, pe_off + 6)[0]
opt_size = struct.unpack_from("<H", data, pe_off + 20)[0]
sec_table = pe_off + 24 + opt_size

sections = []
for i in range(num_sections):
    off = sec_table + i * 40
    name = data[off:off + 8].rstrip(b"\x00").decode("ascii", "replace")
    vsize, vaddr, raw_size, raw_ptr = struct.unpack_from("<IIII", data, off + 8)
    sections.append((name, vaddr, vsize, raw_ptr, raw_size))
    print("section %-8s vaddr=0x%08x vsize=0x%x raw=0x%x rawsize=0x%x" % (name, vaddr, vsize, raw_ptr, raw_size))

IMAGE_BASE = 0x400000

def va_to_off(va):
    for name, vaddr, vsize, raw_ptr, raw_size in sections:
        vstart = IMAGE_BASE + vaddr
        if vstart <= va < vstart + max(vsize, raw_size):
            return raw_ptr + (va - vstart)
    raise ValueError("VA 0x%x not in any section" % va)

for va, desc, fmt in [(0x7f4e34, "TimeToBuild 防除零常量", "f"),
                      (0x7e2ac8, "1.0f (TimeToBuild)", "f"),
                      (0x7e1748, "0.0f (TimeToBuild 比较)", "f"),
                      (0x7e1718, "GetPowerPercentage 电力充足 (double!)", "d"),
                      (0x7e2800, "GetPowerPercentage 断电 (double!)", "d")]:
    off = va_to_off(va)
    raw = data[off:off + 8]
    if fmt == "f":
        val = struct.unpack("<f", raw[:4])[0]
        print("0x%08x %-44s = %g" % (va, desc, val))
    else:
        val = struct.unpack("<d", raw[:8])[0]
        print("0x%08x %-44s = %g" % (va, desc, val))
