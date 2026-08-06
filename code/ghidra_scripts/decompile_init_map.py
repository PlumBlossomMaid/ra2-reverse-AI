# -*- coding: utf-8 -*-
"""阶段 1 `RMG: Init random map`（FUN_00599650，本编译单元最大函数）

它排在 Seeding water 之前，若消耗随机数则整条随机流都要跟着对齐，
所以必须先把它的取数点和整体结构弄清楚。
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
from ghidra.app.cmd.function import CreateFunctionCmd
import codecs, os

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(_REPO, "memory", "data", "decomp", "rmg_init_map.txt")

TARGETS = [
    ("0x599650", "阶段 1：Init random map"),
    ("0x42b1f0", "可用格子总数（MapType 1/2 覆盖率分母）"),
]

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface(); decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

# 顺带对阶段 1 做一次随机数取数点的传递闭包
SINKS = {0x65C780: "Randomizer::Random",
         0x65C7E0: "Randomizer::RandomRanged",
         0x65C6D0: "Randomizer::Randomizer(seed)"}

def callees(fn):
    out = set()
    listing = currentProgram.getListing()
    for ins in listing.getInstructions(fn.getBody(), True):
        for r in ins.getReferencesFrom():
            if r.getReferenceType().isCall():
                out.add(r.getToAddress().getOffset())
    return out

def closure(root):
    seen, stack, hits = set(), [root], []
    parent = {root: None}
    while stack:
        cur = stack.pop()
        if cur in seen:
            continue
        seen.add(cur)
        fn = fm.getFunctionAt(af.getAddress(hex(cur).rstrip("L")))
        if fn is None:
            continue
        for c in callees(fn):
            if c in SINKS:
                chain, p = [], cur
                while p is not None:
                    chain.append("%08X" % p)
                    p = parent.get(p)
                hits.append((SINKS[c], " <- ".join(chain)))
            elif c not in seen:
                parent.setdefault(c, cur)
                stack.append(c)
    return seen, hits

f = codecs.open(OUT, "w", "utf-8")
for sa, label in TARGETS:
    addr = af.getAddress(sa)
    fn = fm.getFunctionAt(addr)
    if fn is None:
        CreateFunctionCmd(addr).applyTo(currentProgram, monitor)
        fn = fm.getFunctionAt(addr)
    f.write("\n" + "=" * 70 + "\n## %s @ %s\n\n" % (label, sa))
    if fn is None:
        f.write("// CREATE_FAILED\n"); continue
    res = decomp.decompileFunction(fn, 600, monitor)
    f.write(res.getDecompiledFunction().getC() if res.decompileCompleted()
            else "// 反编译失败: %s\n" % res.getErrorMessage())

f.write("\n" + "=" * 70 + "\n## 阶段 1 的随机数取数点（传递闭包）\n\n")
seen, hits = closure(0x599650)
f.write("可达函数数：%d\n取数点 %d 处：\n" % (len(seen), len(hits)))
for name, chain in sorted(set(hits)):
    f.write("  %-26s <- %s\n" % (name, chain))
f.close()
print("written: " + OUT)
