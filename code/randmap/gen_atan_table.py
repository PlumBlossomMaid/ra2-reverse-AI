# -*- coding: utf-8 -*-
u"""把原版 gamemd.exe 的反正切查表（0x8610B4，0x1001 项 float）导出为 C++ 头文件。

原版 `atan` @0x4CADE0 查这张表，步长是紧跟在表尾的那个 float 常量
`h = [0x8650B8] = 0.024413354694843292`（表尾 `table[0x1000]` 正好落在 0x8650B4）。

**表的生成式已完全还原**（4097/4097 逐位相符）：

    float x = 0.0f;
    for (i = 0; i <= 0x1000; ++i) {
        table[i] = (float)atan(x);   // 向零截断存 float
        x = x + h;                   // float 累加，也是向零截断
    }

两处「向零截断」缺一不可——它正是原版全局 FPU 的 `_control87(_RC_CHOP)`。
换成就近舍入，4097 项里只对得上 36 项。这条等于反过来证明了
**这张表当年就是被同一套 RC=chop 环境下的代码生成出来的**。

用法：  python code/randmap/gen_atan_table.py > code/rewrite/rmg/atan_table.h
"""
import math
import os
import struct
import sys

GAME = os.environ.get("RA2_GAME_DIR", r"E:\YRLauncher")
EXE = os.path.join(GAME, "gamemd.exe")
TABLE_VA = 0x8610B4
STEP_VA = 0x8650B8
COUNT = 0x1001


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


def chop_f(x):
    u"""double -> float，向零截断（原版 FPU 是 RC=chop）"""
    y = struct.unpack("<f", struct.pack("<f", x))[0]
    if abs(y) > abs(x):
        b = struct.unpack("<I", struct.pack("<f", y))[0]
        y = struct.unpack("<f", struct.pack("<I", b - 1))[0]
    return y


def main():
    data = open(EXE, "rb").read()
    bits = struct.unpack("<%dI" % COUNT, read_va(data, TABLE_VA, COUNT * 4))
    step = struct.unpack("<f", read_va(data, STEP_VA, 4))[0]

    # 自检：用上面那条生成式重算全表，必须 4097/4097 逐位相符
    x = 0.0
    for i, b in enumerate(bits):
        want = struct.unpack("<I", struct.pack("<f", chop_f(math.atan(x))))[0]
        assert b == want, "i=%d got 0x%08X want 0x%08X" % (i, b, want)
        x = chop_f(x + step)

    w = sys.stdout.write
    w(u"// \u539f\u7248\u53cd\u6b63\u5207\u67e5\u8868\u2014\u2014gamemd.exe `0x8610B4`\uff0c0x1001 = 4097 \u9879 float\u3002\n")
    w(u"//\n")
    w(u"// \u8fd9\u662f\u4e00\u5f20**\u6570\u5b66\u5e38\u91cf\u8868**\uff0c\u751f\u6210\u5f0f\u5df2\u5b8c\u5168\u8fd8\u539f\uff084097/4097 \u9010\u4f4d\u76f8\u7b26\uff09\uff1a\n")
    w(u"//\n")
    w(u"//     float x = 0.0f;\n")
    w(u"//     for (i = 0; i <= 0x1000; ++i) {\n")
    w(u"//         table[i] = (float)atan(x);   // \u5411\u96f6\u622a\u65ad\u5b58 float\n")
    w(u"//         x = x + h;                   // float \u7d2f\u52a0\uff0c\u4e5f\u662f\u5411\u96f6\u622a\u65ad\n")
    w(u"//     }\n")
    w(u"//\n")
    w(u"// \u4e24\u5904\u300c\u5411\u96f6\u622a\u65ad\u300d\u7f3a\u4e00\u4e0d\u53ef\u2014\u2014\u5b83\u6b63\u662f\u539f\u7248\u5168\u5c40 FPU \u7684 `_control87(_RC_CHOP)`\u3002\n")
    w(u"// \u6362\u6210\u5c31\u8fd1\u820d\u5165\uff0c4097 \u9879\u91cc\u53ea\u5bf9\u5f97\u4e0a 36 \u9879\u3002\u8fd9\u6761\u7b49\u4e8e\u53cd\u8fc7\u6765\u8bc1\u660e\uff1a\n")
    w(u"// **\u8fd9\u5f20\u8868\u5f53\u5e74\u5c31\u662f\u88ab\u540c\u4e00\u5957 RC=chop \u73af\u5883\u4e0b\u7684\u4ee3\u7801\u751f\u6210\u51fa\u6765\u7684**\u3002\n")
    w(u"// \u7d2f\u52a0\u800c\u4e0d\u662f `i * h`\uff1a\u6b63\u56e0\u4e3a\u9010\u6b65\u622a\u65ad\uff0c\u8868\u5c3e\u7684\u81ea\u53d8\u91cf\u6bd4 4096*h\n")
    w(u"// \u4f4e\u4e86\u7ea6 0.0155\uff0c\u4e8e\u662f table[4096] \u6bd4 atan(4096*h) \u4f4e 13 \u4e2a float ulp\u3002\n")
    w(u"//\n")
    w(u"// \u5199\u6b7b\u800c\u4e0d\u8fd0\u884c\u671f\u751f\u6210\u7684\u7406\u7531\u540c sqrt_table.h / sin_table.h\uff1a\n")
    w(u"// CI \u53ef\u80fd\u8dd1\u5728 x86 / ARM / \u9f99\u82af\uff0c\u5199\u6b7b\u624d\u80fd\u8ba9\u201c\u7ed3\u679c\u4e0e\u67b6\u6784\u65e0\u5173\u201d\u5728\u7f16\u8bd1\u671f\u5c31\u6210\u7acb\u3002\n")
    w(u"#pragma once\n\n")
    w(u"#include <cstdint>\n\n")
    w(u"namespace ra2::rmg {\n\n")
    w(u"inline constexpr int kAtanTableSize = 0x1001;   // 4097\n\n")
    w(u"// exe `0x8650B8`\uff0c\u7d27\u8ddf\u5728\u8868\u5c3e\u540e\u9762\u3002\u539f\u7248\u7528 `fdiv dword` \u628a\u81ea\u53d8\u91cf\u9664\u5b83\u5f97\u4e0b\u6807\u3002\n")
    w(u"// \u5b83\u7684\u6765\u5386\uff08\u4e3a\u4ec0\u4e48\u662f\u8fd9\u4e2a\u503c\u800c\u4e0d\u662f 100/4096 = 0.0244140625\uff09**\u672a\u786e\u8ba4**\uff0c\n")
    w(u"// \u4e0d\u5f71\u54cd\u590d\u523b\uff1a\u53ea\u8981\u7167\u539f\u6837\u7528\u5b83\u3002\n")
    w(u"inline constexpr std::uint32_t kAtanStepBits = 0x%08X;  // %.17gf\n\n" % (
        struct.unpack("<I", struct.pack("<f", step))[0], step))
    w(u"// exe `0x7E897C`\uff1a|x| \u5927\u5230\u4e0b\u6807\u8d8a\u754c\u65f6\u76f4\u63a5\u8fd4\u56de\u7684 float \u03c0/2\u3002\n")
    w(u"// \u6ce8\u610f\u5b83\u4e0e table[4096] = 1.5607948303222656 \u76f8\u5dee 0.01 \u5f27\u5ea6\uff0c\n")
    w(u"// \u6240\u4ee5\u8fd9\u4e2a atan \u5728 |x| \u2248 100 \u5904\u6709\u4e00\u4e2a\u8089\u76ee\u7684**\u8df3\u53d8**\u3002\n")
    w(u"inline constexpr std::uint32_t kAtanHalfPiBits = 0x3FC90FDB;  // 1.5707963705062866f\n\n")
    w(u"// \u5b58\u7684\u662f float \u7684\u4f4d\u5f62\uff0c\u907f\u5f00\u5341\u8fdb\u5236\u5b57\u9762\u91cf\u89e3\u6790\u5e26\u6765\u7684\u4efb\u4f55\u4e0d\u786e\u5b9a\u6027\u3002\n")
    w(u"inline constexpr std::uint32_t kAtanTableBits[kAtanTableSize] = {\n")
    for i in range(0, COUNT, 8):
        w(u"    " + u", ".join(u"0x%08X" % b for b in bits[i:i + 8]) + u",\n")
    w(u"};\n\n")
    w(u"}  // namespace ra2::rmg\n")


if __name__ == "__main__":
    main()
