# -*- coding: utf-8 -*-
u"""把原版 gamemd.exe 的正弦查表（0x84F084，10240 项 float）导出为 C++ 头文件。

原版 `sin` @0x4CACB0 与 `cos` @0x4CAD00 共用这一张表：
    table[i] ~= sin(i * 2*pi / 8192)      i in [0, 0x2800)
余弦用 `+0x800`（= 8192/4）的相位偏移取同一张表，所以表比一个整周期多 2048 项。

用法：  python code/randmap/gen_sin_table.py > code/rewrite/rmg/sin_table.h
"""
import math
import os
import struct
import sys

GAME = os.environ.get("RA2_GAME_DIR", r"E:\YRLauncher")
EXE = os.path.join(GAME, "gamemd.exe")
TABLE_VA = 0x84F084
COUNT = 0x2800


def _sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    out = []
    for i in range(nsec):
        o = pe + 24 + optsz + i * 40
        vsz, va, rsz, ptr = struct.unpack_from("<IIII", data, o + 8)
        out.append((base + va, max(vsz, rsz), ptr))
    return out


def read_va(data, va, nbytes):
    for start, size, ptr in _sections(data):
        if start <= va < start + size:
            off = ptr + (va - start)
            return data[off:off + nbytes]
    raise KeyError(hex(va))


def main():
    data = open(EXE, "rb").read()
    bits = struct.unpack("<%dI" % COUNT, read_va(data, TABLE_VA, COUNT * 4))

    # 自检：与 chop(sin(i*2pi/8192)) 逐项比对，只允许 i=4096/8192 两项不同
    # （那两处真值 ~1e-16，是原版生成工具用 x87 fsin 时 pi 归约残留的噪声）
    odd = []
    for i, b in enumerate(bits):
        got = struct.unpack("<f", struct.pack("<I", b))[0]
        ref = struct.unpack("<f", struct.pack("<f", math.sin(i * 2 * math.pi / 8192)))[0]
        if abs(ref) > abs(math.sin(i * 2 * math.pi / 8192)):
            ref = struct.unpack("<f", struct.pack("<I",
                  struct.unpack("<I", struct.pack("<f", ref))[0] - 1))[0]
        if got != ref:
            odd.append(i)
    assert odd == [4096, 8192], odd

    w = sys.stdout.write
    w(u"// \u539f\u7248\u6b63\u5f26\u67e5\u8868\u2014\u2014gamemd.exe `0x84F084`\uff0c10240 \u9879 float\u3002\n")
    w(u"//\n")
    w(u"// \u8fd9\u662f\u4e00\u5f20**\u6570\u5b66\u5e38\u91cf\u8868**\uff1a\u7b2c i \u9879 = sin(i * 2\u03c0 / 8192)\n")
    w(u"// \u5148\u5728 double \u4e0a\u7b97\u518d**\u5411\u96f6\u622a\u65ad**\u5b58\u6210 float\uff08\u539f\u7248\u5168\u5c40 FPU \u662f RC=chop\uff09\u3002\n")
    w(u"// 10240 \u9879\u91cc 10238 \u9879\u4e0e\u8be5\u5b9a\u4e49\u9010\u4f4d\u76f8\u540c\uff1b\u4f8b\u5916\u53ea\u6709 i=4096 \u4e0e i=8192\n")
    w(u"// \uff08sin\u03c0 \u4e0e sin2\u03c0\uff0c\u771f\u503c ~1e-16 \u7684\u820d\u5165\u566a\u58f0\uff09\u2014\u2014\u5b83\u4eec\u662f\u539f\u7248\u751f\u6210\u5de5\u5177\u7528\n")
    w(u"// x87 `fsin` \u65f6 \u03c0 \u5f52\u7ea6\u4e0d\u8db3\u7559\u4e0b\u7684\u75d5\u8ff9\uff08fsin(M_PI) = 1.2246063538223773e-16\n")
    w(u"// \u662f\u4e2a\u5df2\u77e5\u7684 x87 \u73b0\u8c61\uff09\uff0c\u7167\u539f\u6837\u4fdd\u7559\u3002\u81ea\u68c0\u89c1 test_fast_math.cpp\u3002\n")
    w(u"//\n")
    w(u"// \u4e00\u4e2a\u5468\u671f\u662f 8192 \u9879\uff0c\u8868\u5374\u6709 0x2800 = 10240 \u9879\uff1a\u4f59\u5f26\u7528 `+0x800`\n")
    w(u"// \uff08= \u56db\u5206\u4e4b\u4e00\u5468\u671f\uff09\u7684\u76f8\u4f4d\u504f\u79fb\u67e5\u540c\u4e00\u5f20\u8868\uff0c\u591a\u51fa\u7684 2048 \u9879\u5c31\u662f\u7ed9\u5b83\u7559\u7684\u3002\n")
    w(u"//\n")
    w(u"// \u5199\u6b7b\u800c\u4e0d\u8fd0\u884c\u671f\u751f\u6210\u7684\u7406\u7531\u540c sqrt_table.h\uff1aCI \u53ef\u80fd\u8dd1\u5728 x86 / ARM / \u9f99\u82af\uff0c\n")
    w(u"// \u5199\u6b7b\u624d\u80fd\u8ba9\u201c\u7ed3\u679c\u4e0e\u67b6\u6784\u65e0\u5173\u201d\u5728\u7f16\u8bd1\u671f\u5c31\u6210\u7acb\u3002\n")
    w(u"#pragma once\n\n")
    w(u"#include <cstdint>\n\n")
    w(u"namespace ra2::rmg {\n\n")
    w(u"inline constexpr int kSinTableSize = 0x2800;   // 10240\n")
    w(u"inline constexpr int kSinTablePeriod = 8192;   // \u4e00\u4e2a\u5b8c\u6574\u5468\u671f\u7684\u9879\u6570\n")
    w(u"inline constexpr int kSinTableQuarter = 0x800; // \u4f59\u5f26\u7684\u76f8\u4f4d\u504f\u79fb\n\n")
    w(u"// \u5b58\u7684\u662f float \u7684\u4f4d\u5f62\uff0c\u907f\u5f00\u5341\u8fdb\u5236\u5b57\u9762\u91cf\u89e3\u6790\u5e26\u6765\u7684\u4efb\u4f55\u4e0d\u786e\u5b9a\u6027\u3002\n")
    w(u"inline constexpr std::uint32_t kSinTableBits[kSinTableSize] = {\n")
    for i in range(0, COUNT, 8):
        w(u"    " + u", ".join(u"0x%08X" % b for b in bits[i:i + 8]) + u",\n")
    w(u"};\n\n")
    w(u"}  // namespace ra2::rmg\n")


if __name__ == "__main__":
    main()
