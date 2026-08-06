# -*- coding: utf-8 -*-
"""扫描 gamemd.exe 里所有调用随机数发生器的位置，并归属到源文件/行号。

思路：
  1. 全 .text 扫 `E8 <rel32>`，命中三个 Randomizer 入口即记为一次随机数消耗点
  2. 用调试宏（`68 <行号> 68 <文件VA>`，见 scan_source_macros.py）建立
     「地址 -> 源文件:行号」的稀疏标尺，取调用点之前最近的一条宏定位归属

用途：随机数消耗点 = 生成算法的真正骨架。阶段顺序即种子兼容性，
      必须先把「谁在取随机数、取几次」摸清，才能逐阶段对齐。

用法: python code/randmap/scan_rng_callsites.py [起始VA 结束VA]
"""
import bisect
import re
import struct
import sys

EXE = r"E:\YRLauncher\gamemd.exe"
MACRO_CALL = 0x734E60

RNG = {
    0x65C6D0: "Seed",       # Randomizer::Randomizer(seed)
    0x65C780: "Random",     # R250 取数
    0x65C7E0: "Ranged",     # 区间随机（掩码+拒绝采样）
}


def load_pe(path):
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    secs = []
    for i in range(nsec):
        o = pe + 24 + optsz + i * 40
        name = data[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, vaddr, rsize, roff = struct.unpack_from("<IIII", data, o + 8)
        secs.append((name, vaddr, vsize, roff, rsize))
    return data, base, secs


def build_ruler(data, base, secs):
    """调试宏标尺：[(va, 文件名, 行号), ...] 按 va 升序"""
    # 先收集所有形如 X:\...\*.cpp 的字符串 VA
    files = {}
    pat = re.compile(rb"[A-Za-z]:[\x5c][\x20-\x7e]{2,200}?\.(?:cpp|c|hpp|h)\x00")
    for _, vaddr, vsize, roff, rsize in secs:
        for m in pat.finditer(data, roff, roff + rsize):
            va = base + vaddr + (m.start() - roff)
            files[va] = m.group()[:-1].decode("ascii", "replace")

    ruler = []
    for name, vaddr, vsize, roff, rsize in secs:
        if name != ".text":
            continue
        i = roff
        end = roff + rsize
        while True:
            i = data.find(b"\x68", i, end)
            if i < 0 or i + 10 > end:
                break
            if data[i + 5] == 0x68:
                fva = struct.unpack_from("<I", data, i + 6)[0]
                if fva in files:
                    line = struct.unpack_from("<I", data, i + 1)[0]
                    va = base + vaddr + (i - roff)
                    ruler.append((va, files[fva], line))
            i += 1
    ruler.sort()
    return ruler


def scan_calls(data, base, secs, lo=None, hi=None):
    hits = []
    for name, vaddr, vsize, roff, rsize in secs:
        if name != ".text":
            continue
        i = roff
        end = roff + rsize
        while True:
            i = data.find(b"\xE8", i, end)
            if i < 0 or i + 5 > end:
                break
            va = base + vaddr + (i - roff)
            rel = struct.unpack_from("<i", data, i + 1)[0]
            tgt = (va + 5 + rel) & 0xFFFFFFFF
            if tgt in RNG and (lo is None or lo <= va < hi):
                hits.append((va, RNG[tgt]))
            i += 1
    hits.sort()
    return hits


def collect_func_starts(data, base, secs):
    """把全 .text 里所有 CALL 目标当作函数入口，作为归属边界。

    对本用途足够可靠：随机数消耗点所在的函数必然被别处调用过，
    取「不大于该地址的最大入口」即得所属函数。
    """
    starts = set()
    for name, vaddr, vsize, roff, rsize in secs:
        if name != ".text":
            continue
        i = roff
        end = roff + rsize
        while True:
            i = data.find(b"\xE8", i, end)
            if i < 0 or i + 5 > end:
                break
            va = base + vaddr + (i - roff)
            rel = struct.unpack_from("<i", data, i + 1)[0]
            starts.add((va + 5 + rel) & 0xFFFFFFFF)
            i += 1
    return sorted(starts)


def main():
    args = sys.argv[1:]
    lo, hi = 0x594000, 0x5B0000
    if len(args) >= 2:
        lo, hi = int(args[0], 0), int(args[1], 0)

    data, base, secs = load_pe(EXE)
    ruler = build_ruler(data, base, secs)
    rkeys = [r[0] for r in ruler]
    starts = collect_func_starts(data, base, secs)
    hits = scan_calls(data, base, secs, lo, hi)

    print(u"扫描区间 0x%X-0x%X：随机数消耗点 %d 处" % (lo, hi, len(hits)))
    print(u"调试宏标尺 %d 条，识别函数入口 %d 个" % (len(ruler), len(starts)))
    print(u"")

    groups = {}
    for va, kind in hits:
        fi = bisect.bisect_right(starts, va) - 1
        fn = starts[fi] if fi >= 0 else 0
        groups.setdefault(fn, []).append((va, kind))

    print(u"| 函数入口 | Seed | Random | Ranged | 源文件:行号(函数内首条宏) |")
    print(u"|----------|------|--------|--------|---------------------------|")
    for fn in sorted(groups):
        lst = groups[fn]
        c = {k: sum(1 for _, x in lst if x == k) for k in ("Seed", "Random", "Ranged")}
        ri = bisect.bisect_left(rkeys, fn)
        src = u"-"
        if ri < len(ruler) and ruler[ri][0] < fn + 0x1800:
            src = u"%s:%d" % (ruler[ri][1].split("\\")[-1], ruler[ri][2])
        print(u"| 0x%08X | %d | %d | %d | %s |" % (fn, c["Seed"], c["Random"], c["Ranged"], src))

    print(u"")
    print(u"-- 逐点明细 --")
    for fn in sorted(groups):
        print(u"### 0x%08X" % fn)
        print(u"   " + u"  ".join(u"0x%X:%s" % (va, k) for va, k in groups[fn]))


if __name__ == "__main__":
    main()
