# -*- coding: utf-8 -*-
"""从 gamemd.exe 的调试宏 PUSH 序列反推「源码文件 -> 代码地址区间」映射。

原理：MSVC 6.0 调试宏编译出的固定序列
    PUSH <行号 imm32>   = 68 xx xx xx xx
    PUSH <文件名 VA>    = 68 <va imm32>
    ... CALL 0x734e60
扫描 .text 里所有 `68 <lineno> 68 <va>` 组合，即可得到每个源文件的
代码分布区间与行号，无需 Ghidra。

用法: python code/randmap/scan_source_macros.py [过滤子串]
"""
import re
import struct
import sys

EXE = r"E:\YRLauncher\gamemd.exe"


def load_pe(path):
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    secoff = pe + 24 + optsz
    secs = []
    for i in range(nsec):
        o = secoff + i * 40
        name = data[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, vaddr, rsize, roff = struct.unpack_from("<IIII", data, o + 8)
        secs.append((name, vaddr, vsize, roff, rsize))
    return data, base, secs


def make_conv(base, secs):
    def va2off(va):
        rva = va - base
        for _, vaddr, vsize, roff, rsize in secs:
            if vaddr <= rva < vaddr + max(vsize, rsize):
                d = rva - vaddr
                return roff + d if d < rsize else None
        return None

    def off2va(off):
        for _, vaddr, vsize, roff, rsize in secs:
            if roff <= off < roff + rsize:
                return base + vaddr + (off - roff)
        return None

    return va2off, off2va


def cstr(data, off, maxlen=260):
    end = data.find(b"\x00", off, off + maxlen)
    if end < 0:
        end = off + maxlen
    return data[off:end].decode("ascii", "replace")


def main():
    filt = sys.argv[1] if len(sys.argv) > 1 else None
    data, base, secs = load_pe(EXE)
    va2off, off2va = make_conv(base, secs)

    # 1) 收集所有形如源码路径的字符串（含 .cpp/.h，带盘符或反斜杠）
    files = {}   # va -> name
    # 注意：路径分隔符用 \x5c 字符类写，避免转义层折叠反斜杠
    pat = re.compile(rb"[A-Za-z]:[\x5c][\x20-\x7e]{2,200}?\.(?:cpp|c|hpp|h)\x00")
    for m in pat.finditer(data):
        va = off2va(m.start())
        if va is not None:
            files[va] = m.group()[:-1].decode("ascii", "replace")
    print("源码路径字符串: %d 条" % len(files))

    # 2) 扫描 PUSH imm32 ; PUSH <file va> 序列
    hits = {}    # file va -> list of (code va, lineno)
    for fva in files:
        needle = b"\x68" + struct.pack("<I", fva)
        pos = 0
        while True:
            pos = data.find(needle, pos)
            if pos < 0:
                break
            # 前一条必须也是 PUSH imm32（行号）
            if pos >= 5 and data[pos - 5] == 0x68:
                lineno = struct.unpack_from("<I", data, pos - 4)[0]
                cva = off2va(pos - 5)
                if cva is not None and 0 < lineno < 100000:
                    hits.setdefault(fva, []).append((cva, lineno))
            pos += 1

    rows = []
    for fva, lst in hits.items():
        name = files[fva]
        if filt and filt.lower() not in name.lower():
            continue
        cvas = [c for c, _ in lst]
        lns = [l for _, l in lst]
        rows.append((min(cvas), max(cvas), len(lst), min(lns), max(lns), name, fva))
    rows.sort()

    print("\n%-10s %-10s %5s %6s %6s  %s" % (
        "代码起", "代码止", "宏数", "行min", "行max", "源文件"))
    for lo, hi, n, lmin, lmax, name, fva in rows:
        print("0x%08x 0x%08x %5d %6d %6d  %s  (str@0x%x)" % (
            lo, hi, n, lmin, lmax, name, fva))

    # 3) 过滤模式下输出逐条明细（代码地址 <-> 源码行号）
    if filt:
        for lo, hi, n, lmin, lmax, name, fva in rows:
            print("\n== %s 明细（按代码地址）==" % name)
            for cva, ln in sorted(hits[fva]):
                print("  0x%08x  line %5d" % (cva, ln))


if __name__ == "__main__":
    main()
