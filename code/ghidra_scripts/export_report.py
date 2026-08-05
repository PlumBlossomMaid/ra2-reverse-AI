# -*- coding: utf-8 -*-
"""Ghidra headless 后处理：导出 gamemd.exe 分析报告
运行方式: analyzeHeadless.bat <proj> <name> -import gamemd.exe -postScript export_report.py
"""
from ghidra.program.util import DefinedDataIterator
import codecs

OUT = r"E:\code\ra2-reverse\gamemd_report.txt"
f = codecs.open(OUT, "w", "utf-8")


def log(*args):
    line = " ".join(str(a) for a in args)
    print(line)
    f.write(line + "\n")


# 1. 节表
log("== 节表 ==")
for blk in currentProgram.getMemory().getBlocks():
    log("  %-10s start=0x%x size=0x%x" % (blk.getName(),
        blk.getStart().getOffset(), blk.getSize()))

# 2. 函数统计（非 FUN_ 开头的 = 有名字 = 可能来自调试符号）
fm = currentProgram.getFunctionManager()
total = 0
named = []
for fn in fm.getFunctions(True):
    total += 1
    name = fn.getName()
    if not name.startswith("FUN_"):
        named.append((name, str(fn.getEntryPoint())))
log("== 函数 ==")
log("总函数: %d | 命名函数(非FUN_): %d" % (total, len(named)))
for name, addr in named[:300]:
    log("  %s @ %s" % (name, addr))

# 3. 入口点符号
log("== 入口点 ==")
symtab = currentProgram.getSymbolTable()
it = symtab.getExternalEntryPointIterator()
while it.hasNext():
    log("  ", it.next())

# 4. 定义过的字符串
log("== 字符串定义(前400) ==")
count = 0
for d in DefinedDataIterator.getDefinedDataIterator(currentProgram):
    if d.getDataType().getName() != "string":
        continue
    v = d.getValue()
    if not v:
        continue
    s = str(v)
    if len(s) < 4:
        continue
    log("  0x%s  %s" % (d.getAddress(), s[:120]))
    count += 1
    if count >= 400:
        break
log("字符串输出条数: %d" % count)

f.close()
print("REPORT_DONE")
