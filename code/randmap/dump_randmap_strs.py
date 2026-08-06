# -*- coding: utf-8 -*-
"""dump gamemd.exe .data 里随机地图参数键名/字符串内容
从 xref_randmap_strings.py 汇编取证中收集的指针，读取其 ASCII 字符串。
"""
import struct

data = open(r"E:\YRLauncher\gamemd.exe", "rb").read()

# 节表
pe_off = struct.unpack_from("<I", data, 0x3C)[0]
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


def va2off(va):
    for name, vaddr, vsize, roff, rsize in sections:
        if vaddr <= va - image_base < vaddr + max(vsize, 1):
            return roff + (va - image_base - vaddr)
    return None


def cstr(va, maxlen=128):
    off = va2off(va)
    if off is None:
        return "<无映射>"
    end = data.find(b"\x00", off, off + maxlen)
    if end < 0:
        end = off + maxlen
    raw = data[off:end]
    return raw.decode("ascii", "replace")


# 从汇编取证收集的指针
ptrs = [
    # 读参数函数里的键名/字符串
    (0x82bb24, "节名 *.mmp 区域"),
    (0x82bb5c, "ReadInteger 键名"),
    (0x82bb68, "ReadInteger 键名"),
    (0x82bb78, "ReadInteger 键名"),
    (0x82bb84, "ReadInteger 键名"),
    (0x82bb94, "ReadInteger 键名"),
    (0x82bba0, "ReadInteger 键名"),
    (0x82bbb0, "ReadInteger 键名"),
    (0x82bbbc, "ReadInteger 键名"),
    (0x82bbc8, "ReadInteger 键名"),
    (0x82bbd0, "ReadInteger 键名"),
    (0x82bbd8, "ReadInteger 键名"),
    (0x82bbe4, "ReadInteger 键名"),
    (0x818658, "ReadInteger 键名"),
    (0x81a7a8, "ReadInteger 键名"),
    (0x81f11c, "ReadInteger 键名"),
    (0x817278, "ReadInteger 键名"),
    (0x81b1a4, "ReadString 键名"),
    (0x82bc0c, "路径/格式串"),
    (0x82bc24, "lastmap.sed?"),
    (0x82bc30, "RandMap.Sed?"),
    (0x82bc3c, "日志串"),
    (0x82bc4c, "日志串"),
    (0x82bc5c, "日志串"),
    (0x82bc70, "日志串"),
    (0x82ba2c, "格式串源"),
    (0x82ba48, "格式模板"),
    (0x82b038, "循环数组起点"),
]

for va, label in ptrs:
    print("0x%08x  %-18s  %r" % (va, label, cstr(va)))

# 0x7e1b88 0x70 步长数组（rdata），dump 8 个元素
print("\n-- 0x7e1b88 数组（0x70 步长 x8）--")
for i in range(8):
    va = 0x7e1b88 + i * 0x70
    # 该位置可能是指针或字符串
    off = va2off(va)
    if off is None:
        continue
    v = struct.unpack_from("<I", data, off)[0]
    print("0x%08x +%02d: ptr=0x%08x %r" % (va, i, v, cstr(v)))
