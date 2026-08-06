# -*- coding: utf-8 -*-
"""解析 RandMap.img 结构：头部、可能的区块、重复模式"""
import struct

data = open(r"E:\YRLauncher\RandMap.img", "rb").read()
print("size =", len(data))

# 头部 0x100 字节逐行 hex + ascii
for i in range(0, 0x100, 16):
    chunk = data[i:i + 16]
    hexs = " ".join("%02x" % b for b in chunk)
    ascii_ = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in chunk)
    print("%04x  %-47s  %s" % (i, hexs, ascii_))

# 头部可能的 16 位/32 位值
print("\n-- head dwords (LE) --")
for i in range(0, 0x40, 4):
    v = struct.unpack_from("<I", data, i)[0]
    print("  +0x%02x: 0x%08x (%d)" % (i, v, v))
