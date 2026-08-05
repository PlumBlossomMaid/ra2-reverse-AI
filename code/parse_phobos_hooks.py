# -*- coding: utf-8 -*-
"""从 Phobos 源码提取 DEFINE_HOOK 地址 → phobos_hooks.tsv
格式: DEFINE_HOOK(0xADDR, HookName, size)
"""
import glob
import os
import re

PHOBOS_SRC = r"E:\code\ra2-reverse\Phobos\src"
OUT = r"E:\code\ra2-reverse\phobos_hooks.tsv"

HOOK_RE = re.compile(r"DEFINE_HOOK\(\s*(0x[0-9a-fA-F]+)\s*,\s*([A-Za-z_]\w*)\s*,\s*(0x[0-9a-fA-F]+)\s*\)")

hooks = {}  # addr -> name
files = glob.glob(os.path.join(PHOBOS_SRC, "**", "*.cpp"), recursive=True)
for p in files:
    text = open(p, encoding="utf-8", errors="ignore").read()
    for m in HOOK_RE.finditer(text):
        addr = m.group(1).lower()
        if addr not in hooks:
            hooks[addr] = m.group(2)

with open(OUT, "w", encoding="utf-8") as f:
    for addr in sorted(hooks):
        f.write(f"{addr}\t{hooks[addr]}\n")

print(f"扫描 {len(files)} 个 cpp 文件")
print(f"提取 hook 地址: {len(hooks)}")
print(f"输出: {OUT}")
