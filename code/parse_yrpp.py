# -*- coding: utf-8 -*-
"""解析 YRpp 头文件 → 地址-符号表 (tsv)
提取: JMP_THIS(0x..) 方法地址 + DEFINE_REFERENCE 全局变量地址
改进: 对 JMP_THIS 回溯 5 行提取方法名（覆盖析构/单行/跨行签名）
输出: E:\code\ra2-reverse\yrpp_symbols.tsv (地址\t名字\t类型)
"""
import glob
import os
import re

YRPP_DIR = r"E:\code\ra2-reverse\YRpp"
OUT = r"E:\code\ra2-reverse\yrpp_symbols.tsv"

# DEFINE_REFERENCE(Type, Name, 0xAddr[u])
DEF_RE = re.compile(r"DEFINE_REFERENCE\(\s*[\w:<>]+\s*,\s*([A-Za-z_]\w*)\s*,\s*(0x[0-9a-fA-F]+)u?\s*\)")
# JMP_THIS 地址
JMP_RE = re.compile(r"JMP_THIS\(\s*(0x[0-9a-fA-F]+)\s*\)")
# 方法名候选: ~X( / X(   排除关键字和 :: 限定
NAME_RE = re.compile(r"(?:^|[\s*&])(~?[A-Za-z_]\w*)\s*\(")
KEYWORDS = {"if", "for", "while", "switch", "return", "sizeof", "new", "delete",
            "catch", "throw", "else", "do", "case", "int", "bool", "void", "char",
            "unsigned", "long", "float", "double", "short", "struct", "union",
            "JMP_THIS", "DEFINE_REFERENCE", "noinit_t"}

entries = []  # (addr_int, name, type)


def extract_method_name(ctx_text):
    """在拼接的上下文中找最近的非关键字方法名"""
    for m in reversed(list(NAME_RE.finditer(ctx_text))):
        name = m.group(1)
        base = name.lstrip("~")
        if base in KEYWORDS or "::" in name:
            continue
        return name
    return None


def scan_file(path):
    with open(path, encoding="utf-8", errors="ignore") as f:
        lines = f.read().split("\n")
    current_class = None
    for i, line in enumerate(lines):
        cm = re.search(r"class\s+(?:NOVTABLE\s+)?([A-Za-z_]\w*)", line)
        if cm:
            current_class = cm.group(1)
        # DEFINE_REFERENCE（全局数据）
        for mname, addr in DEF_RE.findall(line):
            entries.append((int(addr, 16), f"{current_class}::{mname}", "GLOBAL"))
        # JMP_THIS 地址 → 回溯 5 行找方法名
        for m in JMP_RE.finditer(line):
            addr = int(m.group(1), 16)
            start = max(0, i - 5)
            ctx = " ".join(l.strip() for l in lines[start:i + 1])
            name = extract_method_name(ctx)
            if name and current_class:
                entries.append((addr, f"{current_class}::{name}", "FUNC"))
            else:
                entries.append((addr, "unknown", "FUNC"))


def main():
    files = glob.glob(os.path.join(YRPP_DIR, "*.h"))
    for p in files:
        scan_file(p)
    # 去重：同一地址保留第一个
    seen = {}
    for addr, name, typ in entries:
        if addr not in seen:
            seen[addr] = (name, typ)
    with open(OUT, "w", encoding="utf-8") as f:
        for addr in sorted(seen):
            name, typ = seen[addr]
            f.write(f"0x{addr:x}\t{name}\t{typ}\n")
    funcs = sum(1 for v in seen.values() if v[1] == "FUNC")
    unknown = sum(1 for v in seen.values() if v[1] == "FUNC" and v[0] == "unknown")
    globals_ = len(seen) - funcs
    print(f"扫描 {len(files)} 个头文件")
    print(f"地址总数: {len(seen)} (函数 {funcs} / 全局 {globals_} / unknown {unknown})")
    print(f"输出: {OUT}")


if __name__ == "__main__":
    main()
