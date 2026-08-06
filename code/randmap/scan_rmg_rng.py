# -*- coding: utf-8 -*-
"""列出**生成器自己那个** Randomizer(0xABE890) 的全部取数点。

为什么不能直接数 `call Randomizer::Random`：
  生成阶段会调进大量引擎通用代码，那里也在调同一个 Randomizer 方法，
  但 `this` 是引擎自己的对象，与 0xABE890 是两份独立状态。
  只按"调了哪个函数"判断会把整个引擎算进来，结论不可用。

做法：扫 `mov ecx/esi/edi, 0xABE890` 三种立即数加载，在其后 64 字节内找
第一条 `E8 rel32` 且目标是三个 Randomizer 入口之一，配成一对。

用法: python code/randmap/scan_rmg_rng.py [exe路径]
"""
import struct
import sys

RNG = {
    0x65C6D0: "Seed",     # Randomizer::Randomizer(seed)
    0x65C780: "Random",   # R250 取数
    0x65C7E0: "Ranged",   # 区间随机
}
THIS = 0xABE890
WINDOW = 64   # ecx 有时被编译器提前 0x2B 字节加载，窗口要够宽


def main(exe):
    data = open(exe, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]

    imm = struct.pack("<I", THIS)
    REGS = {0xB9: "ecx", 0xBE: "esi", 0xBF: "edi"}
    pairs, orphans = [], []
    i = 0
    while True:
        j = data.find(imm, i)
        if j < 0:
            break
        i = j + 1
        if data[j - 1] not in REGS:
            continue
        j -= 1
        site = base + j
        found = None
        k = j + 5
        while k < j + 5 + WINDOW:
            if data[k] == 0xE8:
                tgt = base + k + 5 + struct.unpack_from("<i", data, k + 1)[0]
                if tgt in RNG:
                    found = (base + k, RNG[tgt])
                    break
            k += 1
        (pairs if found else orphans).append((site, found))

    print("立即数 0x%06X 加载共 %d 处；配到取数调用 %d 处，未配到 %d 处"
          % (THIS, len(pairs) + len(orphans), len(pairs), len(orphans)))
    lo = min(s for s, _ in pairs + orphans)
    hi = max(s for s, _ in pairs + orphans)
    print("地址跨度 0x%06X - 0x%06X" % (lo, hi))
    kinds = {}
    for _, f in pairs:
        kinds[f[1]] = kinds.get(f[1], 0) + 1
    print("按类别:", kinds)
    print()
    for site, f in pairs:
        print("0x%06X  %-6s  call@0x%06X" % (site, f[1], f[0]))
    if orphans:
        print("\n-- 32 字节内没找到取数调用（可能是别的方法或被跳转打断）--")
        for site, _ in orphans:
            print("0x%06X" % site)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else r"E:\YRLauncher\gamemd.exe")
