# -*- coding: utf-8 -*-
"""RandMap.img 结构侦察 v2：字节分布、熵、整除性、头部字段候选、周期粗检

目的：给下午的代码级逆向提供字节层对照特征（代码说格式 -> 探针验证字节符合）。
"""
import math
import struct
from collections import Counter

data = open(r"E:\YRLauncher\RandMap.img", "rb").read()
size = len(data)
print("size =", size)

print("\n-- 头部 0x100 hex dump --")
for i in range(0, 0x100, 16):
    chunk = data[i:i + 16]
    hexs = " ".join("%02x" % b for b in chunk)
    asc = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in chunk)
    print("%04x  %-47s  %s" % (i, hexs, asc))


def entropy(seq):
    c = Counter(seq)
    n = len(seq)
    return -sum((k / n) * math.log2(k / n) for k in c.values())


print("\n-- 熵 --")
print("head(0x0-0x88): %.3f" % entropy(data[:0x88]))
print("body(0x88+):    %.3f" % entropy(data[0x88:]))
print("full:           %.3f" % entropy(data))

print("\n-- 字节分布 top16 --")
for b, n in Counter(data).most_common(16):
    print("  0x%02x : %d (%.2f%%)" % (b, n, 100.0 * n / size))

print("\n-- body(0x88+) 字节分布 top16, len=%d --" % (size - 0x88))
body = data[0x88:]
for b, n in Counter(body).most_common(16):
    print("  0x%02x : %d (%.2f%%)" % (b, n, 100.0 * n / len(body)))

print("\n-- 头部 u32 全览 (0x0-0x80) --")
for i in range(0, 0x80, 4):
    v = struct.unpack_from("<I", data, i)[0]
    print("  +0x%02x: 0x%08x (%d)" % (i, v, v))

print("\n-- 头部 u16 候选尺寸字段 (16..4096) --")
for i in range(0, 0x80, 2):
    v16 = struct.unpack_from("<H", data, i)[0]
    v32 = struct.unpack_from("<I", data, i)[0] if i + 4 <= 0x80 else None
    if 16 <= v16 <= 4096 or (v32 and 16 <= v32 <= 4096):
        print("  +0x%02x u16=%d(0x%04x)%s" % (
            i, v16, v16,
            ("  u32=%d(0x%08x)" % (v32, v32)) if v32 else ""))

print("\n-- 不同起始偏移的 body 整除性 --")
for start in (0x80, 0x88, 0x90, 0x100, 0x200):
    b = data[start:]
    facts = [w for w in range(64, 2049) if len(b) % w == 0]
    print("  start 0x%03x len=%d 因数[64..2048]: %s" % (start, len(b), facts))

print("\n-- 周期粗检（body 0x88+，前 200*period 字节内同位置相同率）--")
for period in (64, 128, 256, 512, 1024, 2048, 4096):
    n = len(body)
    window = min(n, period * 200)
    if window <= period:
        print("  period %5d: 窗口不足" % period)
        continue
    matched = sum(1 for i in range(period, window)
                  if body[i] == body[i - period])
    print("  period %5d: %.1f%% 相同" % (period, 100.0 * matched / (window - period)))
