# -*- coding: utf-8 -*-
"""把生成器的 150 处取数点归并到所属函数，得到「每个函数抽几次」的总账。

输入：memory/data/rmg/rng_callsites_mapgen.txt（code/randmap/scan_rmg_rng.py 产出）
      只取形如 `0xXXXXXX  Random  call@0xYYYYYY` 的行。
输出：memory/data/rmg/rng_sites_by_function.txt

这份总账就是复刻的施工图：函数按取数次数排下来，逐个还原即可。
"""
import codecs
import os
import re

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(_REPO, "memory", "data", "rmg", "rng_callsites_mapgen.txt")
OUT = os.path.join(_REPO, "memory", "data", "rmg", "rng_sites_by_function.txt")

from ghidra.app.cmd.function import CreateFunctionCmd
from ghidra.util.task import ConsoleTaskMonitor

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
mem = currentProgram.getMemory()
monitor = ConsoleTaskMonitor()


def guess_start(va):
    """Ghidra 没定义函数时，往回找 MSVC6 的 0x90 对齐填充，取其后第一字节为入口。"""
    a = va
    run = 0
    for _ in range(0x1000):
        a -= 1
        b = mem.getByte(af.getAddress("0x%x" % a)) & 0xFF
        if b == 0x90 or b == 0xCC:
            run += 1
            if run >= 4:
                return a + run
        else:
            run = 0
    return None

LINE = re.compile(r"^0x([0-9A-Fa-f]{6})\s+(\w+)\s+call@0x([0-9A-Fa-f]{6})")

groups = {}
guessed = set()
n = 0
for raw in codecs.open(SRC, "r", "utf-8", "replace"):
    m = LINE.match(raw.strip())
    if not m:
        continue
    call_va = int(m.group(3), 16)
    kind = m.group(2)
    if kind != "Random":        # 播种（CRT 静态初始化那处）不算取数
        continue
    fn = fm.getFunctionContaining(af.getAddress("0x%x" % call_va))
    if fn is not None:
        key = fn.getEntryPoint().getOffset()
    else:
        g = guess_start(call_va)
        if g is not None:
            CreateFunctionCmd(af.getAddress("0x%x" % g)).applyTo(currentProgram, monitor)
            fn = fm.getFunctionContaining(af.getAddress("0x%x" % call_va))
        key = fn.getEntryPoint().getOffset() if fn is not None else (g if g else -1)
        guessed.add(key)
    groups.setdefault(key, []).append((call_va, kind))
    n += 1

f = codecs.open(OUT, "w", "utf-8")
f.write("## 生成器取数点按函数归并（this == 0xABE890）\n")
f.write("## 共 %d 处，落在 %d 个函数里\n\n" % (n, len(groups)))
f.write("| 函数 | 取数处数 | 调用点 |\n|------|---------|--------|\n")
for key in sorted(groups, key=lambda k: (-len(groups[k]), k)):
    sites = groups[key]
    name = "0x%06X" % key if key >= 0 else "（无所属函数）"
    if key in guessed:
        name += "&nbsp;⚠推定入口"
    f.write("| %s | %d | %s |\n"
            % (name, len(sites), " ".join("%06X" % a for a, _ in sites)))
f.close()
print("written: %s  (%d sites, %d functions)" % (OUT, n, len(groups)))
