# -*- coding: utf-8 -*-
"""导出原版「浮点比例 + 拒绝采样」取数惯用法的基准值。

被测代码片段（都在 Seeding water 阶段的 FUN_0059C630 里，是该惯用法最干净的样本）：
  0x59C6DA-0x59C701  fmul 10/(2^32-1) + fadd 1.0 + _ftol   -> 拒绝 v>10  -> [1,10]
  0x59C70F-0x59C730  fmul 242/(2^32-1)        + _ftol       -> 拒绝 v>241 -> [0,241]
  0x59C79C-0x59C7BD  fmul 201/(2^32-1)        + _ftol       -> 拒绝 v>200 -> [0,200]

模拟时把 FPCW 设成 0x0E7F（原版 _control87(_RC_CHOP,_MCW_RC) 后的实际控制字）。

用法: python code/randmap/dump_scaled_golden.py > memory/data/rmg/scaled_golden.txt
"""
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0] if "\\" in __file__ else ".")
from emu_oracle import Oracle  # noqa: E402

SITES = [
    ("PickFrom1(10)", 0x59C6DA, 0x59C701, 10, True),
    ("PickBelow(242)", 0x59C70F, 0x59C730, 242, False),
    ("PickBelow(201)", 0x59C79C, 0x59C7BD, 201, False),
]
SEEDS = [0, 1, 11584, 60556, 0xDEADBEEF]
COUNT = 24


def main():
    o = Oracle()
    print("# 原版浮点取数惯用法基准（FPCW=0x0E7F: PC=53 位 + RC=向零截断）")
    print("# 全局 Randomizer @ 0xABE890，每次重新播种")
    for name, start, stop, n, plus1 in SITES:
        limit = n if plus1 else n - 1
        print("")
        print("== %s  片段 0x%X-0x%X  拒绝条件 v>%d ==" % (name, start, stop, limit))
        for s in SEEDS:
            o.seed_global(s)
            vals = []
            while len(vals) < COUNT:
                v = o.run_range(start, stop, {"ebx": 0})
                if v <= limit:
                    vals.append(v)
            print("  seed %-10u : %s" % (s & 0xFFFFFFFF,
                                         " ".join(str(v) for v in vals)))


if __name__ == "__main__":
    main()
