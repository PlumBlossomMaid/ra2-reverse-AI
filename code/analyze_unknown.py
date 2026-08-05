# -*- coding: utf-8 -*-
"""分析 341 个 unknown 地址在 YRpp 头文件中的上下文，定位解析失败原因"""
import glob
import os
import re

unknown_addrs = set()
for line in open(r"E:\code\ra2-reverse\yrpp_symbols.tsv", encoding="utf-8"):
    parts = line.rstrip("\r\n").split("\t")
    if len(parts) >= 3 and parts[2] == "FUNC" and parts[1] == "unknown":
        unknown_addrs.add(parts[0].lower())

print("unknown 地址数:", len(unknown_addrs))
found = 0
shown = 0
for path in glob.glob(r"E:\code\ra2-reverse\YRpp\*.h"):
    lines = open(path, encoding="utf-8", errors="ignore").read().split("\n")
    for i, line in enumerate(lines):
        for m in re.finditer(r"JMP_THIS\(\s*(0x[0-9a-fA-F]+)\s*\)", line):
            if m.group(1).lower() not in unknown_addrs:
                continue
            found += 1
            if shown < 25:
                ctx = lines[max(0, i - 3):i + 2]
                print("=" * 70)
                print(os.path.basename(path), m.group(1))
                for c in ctx:
                    print("  |", c.rstrip()[:90])
                shown += 1
print("=" * 70)
print("在头文件中定位到 unknown 上下文:", found, "处")
