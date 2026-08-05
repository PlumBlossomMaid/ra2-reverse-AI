# -*- coding: utf-8 -*-
"""Ghidra 脚本：用 phobos_hooks.tsv 给 hook 地址创建标注标签
标签名: PhobosHook_<hook名>（非主标签，不覆盖函数名）
"""
from ghidra.program.model.symbol import SourceType
import codecs

TSV = r"E:\code\ra2-reverse\phobos_hooks.tsv"
st = currentProgram.getSymbolTable()
space = currentProgram.getAddressFactory().getDefaultAddressSpace()

applied = 0
skipped = 0

for line in codecs.open(TSV, "r", "utf-8"):
    parts = line.rstrip("\r\n").split("\t")
    if len(parts) < 2:
        continue
    addr_s, name = parts[0], parts[1]
    try:
        addr = space.getAddress(int(addr_s.lstrip("0x"), 16))
    except Exception:
        skipped += 1
        continue
    label = "PhobosHook_" + name
    try:
        st.createLabel(addr, label, SourceType.USER_DEFINED)
        applied += 1
    except Exception:
        skipped += 1

print("PHOBOS_HOOKS_DONE applied=%d skipped=%d" % (applied, skipped))
