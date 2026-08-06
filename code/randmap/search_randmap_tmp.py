# -*- coding: utf-8 -*-
"""在 gamemd.exe 中搜索 RandMap/随机地图相关字符串"""
import re

data = open(r"E:\YRLauncher\gamemd.exe", "rb").read()
pats = [b"RandMap", b"RandomMap", b".Sed", b"randmap", b"SEED", b"IsRandom", b"Random"]
for p in pats:
    hits = [m.start() for m in re.finditer(re.escape(p), data)]
    print("%-12s %d hits" % (p.decode(), len(hits)))
    for h in hits[:6]:
        print("  0x%x: %r" % (h, data[h - 16:h + 32]))
    print()
