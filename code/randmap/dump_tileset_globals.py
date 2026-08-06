# -*- coding: utf-8 -*-
"""把 theater 的 TileSet 全局变量（.bss）对回它的 INI 键名。

原理：`0x545150` 这个函数分两段——
  1. 先用 `FUN_005276D0(section="General", key=..., default=-1)` 把 theater ini 里
     几十个 TileSet 键名对应的**组序号**读进栈上的局部变量；
  2. 再遍历全部 TileSet，`cmp edi, [esp+OFF]` 命中就把当前 TileSet 的
     **起始砖序号** `ebx` 写进对应的全局。

两段的 esp 差 0xC（第一段记录结果时正压着 3 个参数），据此把
「键名 → 栈槽」与「栈槽 → 全局」拼起来。

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
PUSH_DELTA = 0xC          # 记录结果那一刻已压入 section/key/default 三个参数


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


def main(exe):
    rd = strings(exe)
    txt = subprocess.check_output(
        [sys.executable, os.path.join(HERE, "disasm.py"), hex(FN), hex(FNLEN)]
    ).decode("utf-8", "replace")

    lines = txt.splitlines()
    # 每组形如：push default / push KEY / push SECTION / mov [esp+OFF],eax / call
    # 其中 mov 存的是**上一组**的返回值，故 key2slot 要错一位配对。
    calls = []          # 每次 FUN_005276D0 调用用的键名
    movs = []           # 每次 mov [esp+OFF],eax 的槽位（已换算回基准 esp）
    order = []          # 交错的事件序列
    for i, l in enumerate(lines):
        m = re.search(r"mov dword ptr \[esp \+ (0x[0-9a-f]+)\], eax", l)
        if m:
            order.append(("mov", int(m.group(1), 16) - PUSH_DELTA))
        if "call 0x5276d0" in l:
            key = None
            for j in range(i - 1, max(-1, i - 6), -1):
                mm = re.search(r"push 0x(8[0-9a-f]{5})", lines[j])
                if mm:
                    nxt = rd(int(mm.group(1), 16))
                    if nxt and nxt != "General":
                        key = nxt
            order.append(("call", key))
    key2slot, prev = [], None
    for kind, v in order:
        if kind == "mov":
            if prev:
                key2slot.append((prev, v))
            prev = None
        else:
            prev = v

    slot2glob, cmpoff = [], None
    for l in lines:
        m = re.search(r"cmp edi, dword ptr \[esp \+ (0x[0-9a-f]+)\]", l)
        if m:
            cmpoff = int(m.group(1), 16)
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
    f.write(u"## 取自 FUN_00545150：读键存栈槽（esp 差 0x%X）-> 遍历 TileSet 回填全局\n" % PUSH_DELTA)
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
    f.close()
    print("written: %s  (%d globals, %d keys)" % (OUT, len(slot2glob), len(key2slot)))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else r"E:\YRLauncher\gamemd.exe")
