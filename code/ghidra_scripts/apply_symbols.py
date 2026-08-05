# -*- coding: utf-8 -*-
"""Ghidra 脚本：用 yrpp_symbols.tsv 批量给 FUN_ 函数/全局数据命名
运行: analyzeHeadless.bat <proj> RA2 -process gamemd.exe -postScript apply_symbols.py
"""
from ghidra.program.model.symbol import SourceType
import codecs

TSV = r"E:\code\ra2-reverse\yrpp_symbols.tsv"
fm = currentProgram.getFunctionManager()
st = currentProgram.getSymbolTable()
space = currentProgram.getAddressFactory().getDefaultAddressSpace()

applied_func = 0
applied_global = 0
skipped = 0

for line in codecs.open(TSV, "r", "utf-8"):
    parts = line.rstrip("\r\n").split("\t")
    if len(parts) < 3:
        continue
    addr_s, name, typ = parts[0], parts[1], parts[2]
    if name == "unknown":
        continue
    try:
        # Jython (Python 2.7) 的 int() 不接受 0x 前缀，需剥离
        addr = space.getAddress(int(addr_s.lstrip("0x"), 16))
    except Exception:
        skipped += 1
        continue
    if typ == "FUNC":
        fn = fm.getFunctionAt(addr)
        if fn is None:
            skipped += 1
            continue
        if fn.getName().startswith("FUN_"):
            fn.setName(name, SourceType.USER_DEFINED)
            applied_func += 1
        else:
            skipped += 1
    else:
        try:
            st.createLabel(addr, name, SourceType.USER_DEFINED)
            applied_global += 1
        except Exception:
            skipped += 1

print("APPLY_DONE funcs=%d globals=%d skipped=%d" % (applied_func, applied_global, skipped))
