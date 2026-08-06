# -*- coding: utf-8 -*-
"""在 gamemd.exe 的 .text 里搜索「call/jmp 到某个函数」的引用点（rel32 相对寻址）。

与 xref_abs.py 互补，两个都得跑：
  - xref_abs.py  搜绝对 4 字节  → 全局变量、虚表项、跳转表项、函数指针赋值
  - xref_rel32.py 搜 E8/E9 rel32 → 直接 call / 尾调用 jmp

只搜绝对值会把直接调用**全部漏掉**：`call 0x578350` 编码成 `E8 <rel32>`，
镜像里根本不存在 `50 83 57 00` 这四个字节。实测该函数 96 处调用，xref_abs 报 0。

也不要用「线性反汇编整个 .text 再找 call」代替：.text 里混着数据与对齐填充，
capstone 从头顺着解会在那里失步，往后一大段全解错。本脚本按**字节**扫，
只认 E8/E9 且校验落点，不依赖任何指令边界。

代价是会有假阳性（数据里恰好凑出这个 rel32）。判据：真调用点的
`E8` 前一条通常是参数传递（`mov ecx, ...`/`push`），用 disasm.py 看一眼即可。

用法: python code/randmap/xref_rel32.py 0x578350 [更多地址...]
"""
import struct
import sys

from xref_abs import EXE, load_pe


def find_calls(target, exe=EXE, section=".text"):
    """返回 [(指令VA, 'call'|'jmp'), ...]。"""
    data, base, secs = load_pe(exe)
    hits = []
    for name, vaddr, vsize, roff, rsize in secs:
        if section and name != section:
            continue
        blob = data[roff:roff + rsize]
        sec_va = base + vaddr
        for i, b in enumerate(blob):
            if b not in (0xE8, 0xE9):
                continue
            if i + 5 > len(blob):
                continue
            rel = struct.unpack_from("<i", blob, i + 1)[0]
            if sec_va + i + 5 + rel == target:
                hits.append((sec_va + i, "call" if b == 0xE8 else "jmp"))
    return hits


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    for a in sys.argv[1:]:
        t = int(a, 0)
        hits = find_calls(t)
        print("0x%08X : %d 处 rel32 引用" % (t, len(hits)))
        for va, kind in hits:
            print("    %s @0x%08X" % (kind, va))


if __name__ == "__main__":
    main()
