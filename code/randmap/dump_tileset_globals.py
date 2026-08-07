# -*- coding: utf-8 -*-
"""把 theater 的 TileSet 全局变量（.bss）对回它的 INI 键名。

原理：`0x545150` 这个函数分两段——
  1. 先用 `FUN_005276D0(section="General", key=..., default=-1)` 把 theater ini 里
     几十个 TileSet 键名对应的**组序号**读进栈上的局部变量；
  2. 再遍历全部 TileSet，`cmp edi, [esp+OFF]` 命中就把当前 TileSet 的
     **起始砖序号** `ebx` 写进对应的全局。

第一段记录返回值那一刻 esp 上还压着若干个"下一组"的参数，压了几个**每组都不一样**
（0、1 或 2 个），所以不能像早前那样统一减一个固定的 0xC——那样会把
MiscPaveTile / TrackTunnels 之类挤进同一个槽，造出二义。正确做法是
**逐条数从 call 到 mov 之间的 push**，把槽位换算回调用返回那一刻的基准 esp。

输出：memory/data/rmg/tileset_globals.txt
"""
import io
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(REPO, "memory", "data", "rmg", "tileset_globals.txt")
FN, FNLEN = 0x545150, 0x1400


def strings(exe):
    data = open(exe, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    secs = []
    for i in range(nsec):
        o = pe + 24 + optsz + i * 40
        secs.append((base + struct.unpack_from("<I", data, o + 12)[0],
                     struct.unpack_from("<I", data, o + 20)[0],
                     struct.unpack_from("<I", data, o + 16)[0]))

    def rd(va):
        for v, p, s in secs:
            if v <= va < v + s:
                off = p + (va - v)
                e = data.index(b"\0", off)
                b = data[off:e]
                if b and all(32 <= c < 127 for c in bytearray(b)):
                    return b.decode("latin1")
        return None
    return rd


FOOTER = u"""
## 交叉验证：谁在代码里硬编码了"区间宽度"，和 TilesInSet 对不对得上
  0xAA1020 CliffSet      宽 0x28 = 40 == TilesInSet   (0x5A17F0 / 0x4863D0)
  0xAA0738 WaterSet      宽 0x0E = 14 == TilesInSet   (0x485060)
  0xABAD28 ShorePieces   宽 0x2A = 42 == TilesInSet   (岸砖清扫)
  0xAA10A4 MiscPaveTile  宽 0x0E = 14 == TilesInSet   (0x486650)
  0xABBEC4 PavedRoadEnds 宽    4 ==  4 == TilesInSet  (0x4866F0；0x58F2C0 的四个桥头
                                                       正好按 +0/+1/+2/+3 用掉东北西南)
## 对不上的（照抄即可，别"修"）
  0xABBEC8 PavedRoads  代码写 0x0F = 15，TilesInSet = 21
  0xABC2B0 PaveTile    代码写 0x10 = 16，TilesInSet = 1
  0xABBEBC CliffRamps  代码写 0x14 = 20，TilesInSet = 10
"""

def main(exe):
    rd = strings(exe)
    txt = subprocess.check_output(
        [sys.executable, os.path.join(HERE, "disasm.py"), hex(FN), hex(FNLEN)]
    ).decode("utf-8", "replace")

    lines = txt.splitlines()
    # 编译器把相邻几组的取值/存值交错在一起：
    #   push KEY_n / push "General" / call 5276D0 / push KEY_n+1 / push "General"
    #   / mov [esp+OFF], eax        <- 存的是第 n 组的返回值，此刻 esp 已被压低 8
    # 所以：先按顺序找 call 的键名，再找该 call 之后、下一个 call 之前的那条 mov，
    # 数中间有几个 push，把 OFF 换算回 call 返回那一刻的 esp。
    key2slot = []
    lastkey = None
    pending = None      # (键名, 已数到的 push 个数)
    for l in lines:
        m = re.search(r"push 0x(8[0-9a-f]{5})", l)
        if m:
            nm = rd(int(m.group(1), 16))
            if nm and nm != "General":
                lastkey = nm
        if re.search(r"\bpush\b", l) and pending is not None:
            pending = (pending[0], pending[1] + 1)
        m = re.search(r"mov dword ptr \[esp \+ (0x[0-9a-f]+)\], eax", l)
        if m and pending is not None:
            key2slot.append((pending[0], int(m.group(1), 16) - 4 * pending[1]))
            pending = None
            continue
        if "call 0x5276d0" in l:
            pending = (lastkey, 0)

    slot2glob, cmpoff, eaxslot = [], None, None
    for l in lines:
        m = re.search(r"cmp edi, dword ptr \[esp \+ (0x[0-9a-f]+)\]", l)
        if m:
            cmpoff = int(m.group(1), 16)
            continue
        # 有一处（RampBase）编译器先把槽读进 eax 再比，pattern 不一样，得单独认
        m = re.search(r"mov eax, dword ptr \[esp \+ (0x[0-9a-f]+)\]$", l)
        if m:
            eaxslot = int(m.group(1), 16)
            continue
        if re.search(r"cmp edi, eax$", l) and eaxslot is not None:
            cmpoff = eaxslot
            continue
        m = re.search(r"mov dword ptr \[(0x[0-9a-f]+)\], (\w+)", l)
        if m and cmpoff is not None and not m.group(1).startswith("0x8"):
            slot2glob.append((cmpoff, int(m.group(1), 16)))
            cmpoff = None

    bykey = {}
    for k, s in key2slot:
        bykey.setdefault(s, []).append(k)

    f = io.open(OUT, "w", encoding="utf-8")
    f.write(u"## theater TileSet 全局（.bss）<-> theater ini [General] 键名\n")
    f.write(u"## 取自 FUN_00545150：读键存栈槽（逐条数 push 换算 esp）-> 遍历 TileSet 回填全局\n")
    f.write(u"## 全局里存的是该 TileSet 的**起始砖序号**（CellClass+0x38 与之比较）\n\n")
    f.write(u"| 全局 | 栈槽 | ini 键名 |\n|---|---|---|\n")
    for s, g in sorted(slot2glob, key=lambda x: x[1]):
        names = bykey.get(s)
        f.write(u"| 0x%06X | [esp+0x%X] | %s |\n"
                % (g, s, u" / ".join(names) if names else u"?（未匹配）"))
    unmatched = [(k, s) for k, s in key2slot
                 if s not in set(x[0] for x in slot2glob)]
    if unmatched:
        f.write(u"\n## 读进来了但没回填全局的键（只用作局部量）\n")
        for k, s in unmatched:
            f.write(u"  [esp+0x%X]  %s\n" % (s, k))
    f.write(FOOTER)
    f.close()
    print("written: %s  (%d globals, %d keys)" % (OUT, len(slot2glob), len(key2slot)))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else r"E:\YRLauncher\gamemd.exe")
