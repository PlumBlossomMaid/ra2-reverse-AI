# -*- coding: utf-8 -*-
u"""生成 Blowfish 的 P 数组与四张 S 盒（code/rewrite/mix/blowfish_tables.h）。

Blowfish 的初始表就是 **pi 的十六进制小数位**（Schneier 原论文如此定义），
所以这里不从任何第三方实现里搬运常量，直接用 Machin 公式把 pi 算到
1042 * 8 = 8336 位十六进制，再按 8 位一组切成 32 位字：
    P[0..17]      前 18 个字
    S[0..3][0..255]  随后 1024 个字
自检：P[0] 必须是 0x243F6A88（pi = 3.243F6A88... 十六进制）。
"""
import os
import sys


def atan_inv(x, bits):
    u"""floor(atan(1/x) * 2**bits)，整数级数。"""
    total = 0
    term = (1 << bits) // x
    x2 = x * x
    k = 1
    while term:
        total += term // k if (k // 2) % 2 == 0 else -(term // k)
        term //= x2
        k += 2
    return total


def pi_hexdigits(n):
    u"""pi 小数点后 n 位十六进制（字符串）。"""
    bits = 4 * (n + 16)
    pi = 16 * atan_inv(5, bits) - 4 * atan_inv(239, bits)   # Machin
    frac = pi - (3 << bits)
    return ("%x" % (frac >> (bits - 4 * n))).rjust(n, "0")[:n]


def main():
    words_needed = 18 + 4 * 256
    digits = pi_hexdigits(words_needed * 8)
    words = [int(digits[i * 8:i * 8 + 8], 16) for i in range(words_needed)]
    assert words[0] == 0x243F6A88, "pi 算错了：P[0] = 0x%08X" % words[0]
    assert words[1] == 0x85A308D3, "pi 算错了：P[1] = 0x%08X" % words[1]
    assert words[18] == 0xD1310BA6, "pi 算错了：S0[0] = 0x%08X" % words[18]

    out = []
    out.append(u"// 本文件由 code/mix/gen_blowfish_tables.py 生成，请勿手改。")
    out.append(u"//")
    out.append(u"// Blowfish 的初始 P 数组与 S 盒按定义就是 pi 的十六进制小数位")
    out.append(u"//（Schneier 1993），生成脚本用 Machin 公式现算，没有从任何第三方")
    out.append(u"// 实现里搬运常量。自检：P[0] == 0x243F6A88。")
    out.append(u"#ifndef RA2_MIX_BLOWFISH_TABLES_H_")
    out.append(u"#define RA2_MIX_BLOWFISH_TABLES_H_")
    out.append(u"")
    out.append(u"#include <cstdint>")
    out.append(u"")
    out.append(u"namespace ra2 {")
    out.append(u"namespace mix {")
    out.append(u"")
    out.append(u"const std::uint32_t kBlowfishP[18] = {")
    for i in range(0, 18, 6):
        out.append(u"    " + " ".join("0x%08Xu," % w for w in words[i:i + 6]))
    out.append(u"};")
    out.append(u"")
    out.append(u"const std::uint32_t kBlowfishS[4][256] = {")
    for b in range(4):
        out.append(u"    {")
        box = words[18 + b * 256:18 + (b + 1) * 256]
        for i in range(0, 256, 6):
            out.append(u"        " + " ".join("0x%08Xu," % w for w in box[i:i + 6]))
        out.append(u"    },")
    out.append(u"};")
    out.append(u"")
    out.append(u"}  // namespace mix")
    out.append(u"}  // namespace ra2")
    out.append(u"")
    out.append(u"#endif  // RA2_MIX_BLOWFISH_TABLES_H_")

    path = (sys.argv[1] if len(sys.argv) > 1 else
            os.path.join(os.path.dirname(__file__), "..", "rewrite", "mix",
                         "blowfish_tables.h"))
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print("wrote %s (%d words)" % (path, words_needed))


if __name__ == "__main__":
    main()
