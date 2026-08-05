# -*- coding: utf-8 -*-
"""对比 Phobos hooks 与 YRpp 符号：
1. hook 地址是否命中 YRpp 函数/全局
2. hook 地址密度分布（哪些 .text 区域被社区验证过）
"""
import re

def load_tsv(path, col_addr=0, col_name=1):
    out = {}
    for line in open(path, encoding="utf-8"):
        parts = line.rstrip("\r\n").split("\t")
        if len(parts) <= col_name:
            continue
        out[parts[col_addr].lower()] = parts[col_name]
    return out

yrpp = load_tsv(r"E:\code\ra2-reverse\yrpp_symbols.tsv")
hooks = load_tsv(r"E:\code\ra2-reverse\phobos_hooks.tsv")

print(f"YRpp 符号: {len(yrpp)} | Phobos hooks: {len(hooks)}")

# 1. hook 地址与 YRpp 精确重合
exact = [a for a in hooks if a in yrpp]
print(f"hook 精确命中 YRpp 符号: {len(exact)}")

# 2. hook 地址区域分布（按 .text 前两位划分区块）
from collections import Counter
buckets = Counter()
for a in hooks:
    addr = int(a, 16)
    if 0x401000 <= addr < 0x7E0000:
        buckets[hex((addr >> 12) << 12)] += 1

print(f"\nhook 密度最高的 15 个 4KB 区块:")
for b, c in buckets.most_common(15):
    # 尝试定位区块附近最近的 YRpp 符号
    nearby = min(yrpp, key=lambda k: abs(int(k, 16) - int(b, 16)))
    print(f"  {b} 区: {c} 个 hook | 最近 YRpp 符号: {yrpp[nearby]} @ {nearby}")

# 3. hook 但 YRpp 完全没有的（可能 YRpp 空白）
no_yrpp = [a for a in hooks if a not in yrpp]
print(f"\nhook 地址不在 YRpp 符号表中: {len(no_yrpp)}")
