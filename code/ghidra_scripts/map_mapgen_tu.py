# -*- coding: utf-8 -*-
"""MapGen.cpp 编译单元结构测绘 (0x595000 - 0x59B000)

背景：调试宏扫描(code/randmap/scan_source_macros.py)实锤 MapGen.cpp 的代码区间为
      0x005956E7 - 0x0059A63A，源码行号跨度 3934-6746。
产出：区间内全部函数的清单、大小、调用关系（内/外）、Randomizer 调用点、
      字符串引用，作为还原生成流程的骨架。
运行: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
      -postScript map_mapgen_tu.py -scriptPath <repo>/code/ghidra_scripts
"""
import codecs
import os

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(_REPO, "memory", "data", "decomp", "mapgen_tu_map.txt")

LO = 0x595000
HI = 0x59B000

# 已知关键调用目标（用于标注函数用途）
KNOWN = {
    0x65c780: "Randomizer::Random",
    0x65c7e0: "Randomizer::RandomRanged",
    0x65d430: "Randomizer?_0x65d430",
    0x65d460: "Randomizer?_0x65d460",
    0x65d480: "Randomizer?_0x65d480",
    0x5275c0: "CCINI::ReadInteger?/WriteInteger",
    0x5276d0: "CCINI::ReadInteger",
    0x528e00: "CCINI::WriteString",
    0x528f00: "CCINI::ReadString",
    0x734e60: "DebugMacro(file,line)",
    0x689e90: "ReadMap",
}

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
listing = currentProgram.getListing()
refmgr = currentProgram.getReferenceManager()
mem = currentProgram.getMemory()

f = codecs.open(OUT, "w", "utf-8")
f.write("## MapGen.cpp 编译单元结构测绘 [0x%x - 0x%x]\n\n" % (LO, HI))

lo = af.getAddress("0x%x" % LO)
hi = af.getAddress("0x%x" % HI)


def in_range(a):
    return a is not None and LO <= a.getOffset() < HI


# ---------- 1. 函数清单 ----------
funcs = []
it = fm.getFunctions(lo, True)
while it.hasNext():
    fn = it.next()
    ep = fn.getEntryPoint()
    if ep.compareTo(hi) >= 0:
        break
    funcs.append(fn)

f.write("-- 区间内已识别函数 %d 个 --\n" % len(funcs))
f.write("%-10s %-10s %6s  %s\n" % ("入口", "结束", "字节", "名称"))
for fn in funcs:
    body = fn.getBody()
    f.write("0x%08x 0x%08x %6d  %s\n" % (
        fn.getEntryPoint().getOffset(), body.getMaxAddress().getOffset(),
        body.getNumAddresses(), fn.getName()))

# ---------- 2. 未建函数的指令空洞 ----------
f.write("\n-- 未归属函数的指令区(NOFN 空洞) --\n")
cur = None
prev_end = None
iit = listing.getInstructions(lo, True)
holes = []
while iit.hasNext():
    inst = iit.next()
    a = inst.getAddress()
    if a.compareTo(hi) >= 0:
        break
    fn = fm.getFunctionContaining(a)
    if fn is None:
        if cur is None:
            cur = a.getOffset()
        prev_end = a.getOffset() + inst.getLength()
    else:
        if cur is not None:
            holes.append((cur, prev_end))
            cur = None
if cur is not None:
    holes.append((cur, prev_end))
for lo_h, hi_h in holes:
    f.write("0x%08x - 0x%08x  (%d 字节)\n" % (lo_h, hi_h, hi_h - lo_h))

# ---------- 3. 每个函数的调用出入 ----------
f.write("\n-- 函数调用关系 --\n")
for fn in funcs:
    ep = fn.getEntryPoint()
    f.write("\n### %s @ 0x%08x  (%d 字节)\n" % (
        fn.getName(), ep.getOffset(), fn.getBody().getNumAddresses()))

    # 入边
    callers = set()
    for r in refmgr.getReferencesTo(ep):
        ca = r.getFromAddress()
        cf = fm.getFunctionContaining(ca)
        tag = "%s@0x%08x" % (cf.getName() if cf else "NOFN", ca.getOffset())
        callers.add(("IN " if in_range(ca) else "OUT", tag))
    if callers:
        f.write("  被调用: %s\n" % ", ".join(
            "%s%s" % (k, v) for k, v in sorted(callers)))
    else:
        f.write("  被调用: (无直接引用，可能是虚函数/间接调用)\n")

    # 出边 + 字符串引用 + 常量
    calls = []
    strs = []
    body = fn.getBody()
    iit2 = listing.getInstructions(body, True)
    while iit2.hasNext():
        inst = iit2.next()
        mn = inst.getMnemonicString()
        if mn == "CALL":
            for r in inst.getReferencesFrom():
                t = r.getToAddress()
                if t is None:
                    continue
                off = t.getOffset()
                nm = KNOWN.get(off)
                if nm is None:
                    tf = fm.getFunctionAt(t)
                    nm = tf.getName() if tf else "sub_%08x" % off
                calls.append((inst.getAddress().getOffset(), off, nm))
        else:
            for r in inst.getReferencesFrom():
                t = r.getToAddress()
                if t is None:
                    continue
                d = listing.getDataAt(t)
                if d is not None and d.hasStringValue():
                    strs.append((inst.getAddress().getOffset(),
                                 t.getOffset(), str(d.getValue())))
    if calls:
        f.write("  调用(%d):\n" % len(calls))
        for site, tgt, nm in calls:
            f.write("    0x%08x -> 0x%08x %s\n" % (site, tgt, nm))
    if strs:
        f.write("  字符串:\n")
        for site, tgt, s in strs:
            f.write("    0x%08x -> 0x%08x %r\n" % (site, tgt, s))

f.close()
print("MAPGEN_TU_MAP_DONE -> " + OUT)
