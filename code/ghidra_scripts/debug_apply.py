# -*- coding: utf-8 -*-
"""Debug：分步统计 apply_symbols 各分支的计数，定位 skipped 原因"""
from ghidra.program.model.symbol import SourceType
import codecs

TSV = r"E:\code\ra2-reverse\yrpp_symbols.tsv"
fm = currentProgram.getFunctionManager()
st = currentProgram.getSymbolTable()
space = currentProgram.getAddressFactory().getDefaultAddressSpace()

c_lines = 0
c_parse = 0
c_int_fail = 0
c_unknown = 0
c_fn_none = 0
c_not_fun = 0
c_applied = 0
c_global_ok = 0
c_global_fail = 0

f = codecs.open(TSV, "r", "utf-8")
lines = f.readlines()
f.close()
c_lines = len(lines)
print("TSV lines:", c_lines)

# 前 3 行原始内容（repr 看分隔符）
for l in lines[:3]:
    print("RAW:", repr(l))

for line in lines:
    parts = line.rstrip("\n").split("\t")
    if len(parts) < 3:
        print("SHORT_PARTS:", repr(line))
        continue
    addr_s, name, typ = parts[0], parts[1], parts[2]
    if name == "unknown":
        c_unknown += 1
        continue
    try:
        addr_val = int(addr_s.lstrip("0x"), 16)
    except Exception as e:
        c_int_fail += 1
        print("INT_FAIL:", addr_s, e)
        continue
    try:
        addr = space.getAddress(addr_val)
    except Exception as e:
        print("ADDR_FAIL:", addr_s, e)
        continue
    if typ == "FUNC":
        fn = fm.getFunctionAt(addr)
        if fn is None:
            c_fn_none += 1
            if c_fn_none <= 3:
                print("FN_NONE:", addr_s, name)
            continue
        if not fn.getName().startswith("FUN_"):
            c_not_fun += 1
            if c_not_fun <= 3:
                print("NOT_FUN:", addr_s, name, "->", fn.getName())
            continue
        try:
            fn.setName(name, SourceType.USER_DEFINED)
            c_applied += 1
        except Exception as e:
            print("SETNAME_FAIL:", addr_s, name, e)
    else:
        try:
            st.createLabel(addr, name, True)
            c_global_ok += 1
        except Exception as e:
            c_global_fail += 1
            if c_global_fail <= 3:
                print("GLOBAL_FAIL:", addr_s, name, e)

print("=" * 40)
print("lines=%d parse_ok=%d unknown=%d int_fail=%d fn_none=%d not_fun=%d applied=%d global_ok=%d global_fail=%d" % (
    c_lines, c_lines - c_unknown - c_int_fail, c_unknown, c_int_fail,
    c_fn_none, c_not_fun, c_applied, c_global_ok, c_global_fail))
