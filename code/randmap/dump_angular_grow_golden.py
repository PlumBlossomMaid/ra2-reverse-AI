# -*- coding: utf-8 -*-
u"""0x5A08D0 AngularGrow 的整函数模拟执行。

「按方位角优先」的堆扩散：以 0x5A0700 收来的边界格为种子，
优先级 = 1.5 * |方位角 - 基准角| + 2.0 * U(0,1)，每推进一格
基准角再随机游走 NormalDeviate() * pi/4。河道/湖泊的走向就来自这里。

环境沿用 OpsOracle（区域 oracle：摘掉 0x4865D0 恒真桩、ClearToGreenLat=494、
静态 MapSeedClass 实例 0xABDFD8，+0x180/+0x184 = 地图宽高）。
正态对象 0xABDFB8 已由 PaintOracle 摆好（缓存标志 0、+0x10 = 0x598000）。

⚠️ 这一段取随机数，且**每格取一次**（0x5A0F16 / 0x5A0B5A）外加
迭代次数那一次拒绝采样（0x5A0C99），基准里必须记取数个数。

基准记：
  IN  行 —— 输入（tile / subtile / level / +0x38 / +0x3C / +0x4B）
  OUT 行 —— 终态（同上六项）
  外加返回值、随机数调用次数、共享哑元坐标 [0xABDC74]
"""
import io
import struct
import sys

from dump_region_ops_golden import OpsOracle
from dump_regions_golden import SEEDOBJ, sx
from dump_shore_smooth_golden import WATER, SHORE, UNDECIDED
from dump_blob_paint_golden import WORK_SIZE

A_GROW = 0x5A08D0


class GrowOracle(OpsOracle):
    def snap(self):
        out = {}
        for (x, y) in self._diamond():
            c = self.cell(x, y)
            r = self.work + (y * self.side + x) * WORK_SIZE
            out[(x, y)] = (self.rd32(c + 0x38),
                           self.uc.mem_read(c + 0x11A, 1)[0],
                           struct.unpack("b", self.uc.mem_read(c + 0x11B, 1))[0],
                           sx(self.rd32(r + 0x38)), sx(self.rd32(r + 0x3C)),
                           self.uc.mem_read(r + 0x4B, 1)[0])
        return out

    def grow(self, blob, weight, rect, origin, flush):
        pr = self._bump(16)
        for i, v in enumerate(rect):
            self.wr32(pr + i * 4, v)
        po = self._bump(8)
        self.uc.mem_write(po, struct.pack("<hh", origin[0], origin[1]))
        wbits = struct.unpack("<I", struct.pack("<f", weight))[0]
        self.rng_calls = 0
        r = self._call(A_GROW, SEEDOBJ,
                       (blob & 0xFFFFFFFF, wbits, pr, po, 1 if flush else 0))
        return r & 0xFF, self.rng_calls


# 名字, W, H, 地形, 归属, blob, weight, rect, origin, flush
CASES = [
    # ── 基准角的四条分支（rect 的四个字段决定，0x5A099D-0x5A0A03）──
    # x=0 w=512 y=0 h=512 → 0.0（两组 if 都不改）
    ("g-base0-full", 12, 12, "undecided", "disk", 1, 0.05,
     (0, 0, 512, 512), (18, 18), 0),
    # x=0 w!=512 y=0 h=512 → pi
    ("g-base-pi", 12, 12, "undecided", "disk", 1, 0.05,
     (0, 0, 64, 512), (18, 18), 0),
    # x=0 w!=512 y=0 h!=512 → 先 pi 再被 pi/2 覆盖
    ("g-base-halfpi", 12, 12, "undecided", "disk", 1, 0.05,
     (0, 0, 64, 64), (18, 18), 0),
    # y!=0 → 3pi/2（第一组算出来的 0.0 白算）
    ("g-base-3halfpi", 12, 12, "undecided", "disk", 1, 0.05,
     (4, 4, 40, 40), (18, 18), 0),
    # x!=0 且 y=0 h=512 → 0.0，但走的是第一组的 else
    ("g-base0-xnz", 12, 12, "undecided", "disk", 1, 0.05,
     (4, 0, 40, 512), (18, 18), 0),
    # ── 迭代次数：weight 直接决定 ──
    ("g-w002", 12, 12, "undecided", "disk", 1, 0.02,
     (0, 0, 512, 512), (18, 18), 0),
    ("g-w05", 12, 12, "undecided", "disk", 1, 0.5,
     (0, 0, 512, 512), (18, 18), 0),
    # weight 大到 total = 0：主循环一次都不进，堆里全是种子
    ("g-w2-noflush", 12, 12, "undecided", "disk", 1, 2.0,
     (0, 0, 512, 512), (18, 18), 0),
    ("g-w2-flush", 12, 12, "undecided", "disk", 1, 2.0,
     (0, 0, 512, 512), (18, 18), 1),
    # ── 收尾遍：flush 认领时**不查砖**，与主循环不一致 ──
    ("g-flush-lake", 12, 12, "lake", "disk", 1, 0.2,
     (0, 0, 512, 512), (18, 18), 1),
    ("g-noflush-lake", 12, 12, "lake", "disk", 1, 0.2,
     (0, 0, 512, 512), (18, 18), 0),
    # ── 原点偏在一角：方位角一头倒，长出来的是一条带子 ──
    ("g-origin-corner", 14, 14, "undecided", "disk", 1, 0.03,
     (0, 0, 512, 512), (0, 0), 0),
    ("g-origin-far", 14, 14, "undecided", "disk", 1, 0.03,
     (0, 0, 512, 512), (60, 4), 0),
    # 原点正压在种子上：dx == 0 那条（角度直接取 pi/2）会被踩到
    ("g-origin-on", 12, 12, "undecided", "disk", 1, 0.05,
     (0, 0, 512, 512), (18, 24), 0),
    # ── 撞上第三方地盘：ok 变 false，主循环立刻收摊 ──
    ("g-foe-false", 12, 12, "undecided", "disk-and-foe", 1, 0.02,
     (0, 0, 512, 512), (18, 18), 0),
    ("g-foe-flush", 12, 12, "undecided", "disk-and-foe", 1, 0.02,
     (0, 0, 512, 512), (18, 18), 1),
    # ── 砌死的一圈普通砖：邻格无主但不是未定砖 → 既不入堆也不算失败 ──
    ("g-wall", 12, 12, "wall", "disk", 1, 0.02,
     (0, 0, 512, 512), (18, 18), 0),
    ("g-solid", 10, 10, "solid", "disk", 1, 0.05,
     (0, 0, 512, 512), (15, 15), 0),
    # ── 矩形夹死：种子被筛掉一半，剩下的往一个角长 ──
    ("g-rect-clip", 14, 14, "undecided", "disk", 1, 0.03,
     (16, 16, 10, 10), (21, 21), 0),
    # 矩形宽高为 0：种子一个都进不去 → 堆空 → 主循环第一轮就 break
    ("g-rect-empty", 12, 12, "undecided", "disk", 1, 0.05,
     (8, 8, 0, 0), (18, 18), 0),
    # ── 没有这个编号：k = 0，ln(0) 落到 max(...,1.0) 的兜底上 ──
    ("g-noowner", 10, 10, "undecided", "none", 1, 0.05,
     (0, 0, 512, 512), (15, 15), 0),
    # ── 甜甜圈 / 两团 / 条带：种子分布决定堆序 ──
    ("g-donut", 12, 12, "undecided", "donut", 1, 0.05,
     (0, 0, 512, 512), (18, 18), 0),
    ("g-two-blobs", 12, 10, "undecided", "two-blobs", 1, 0.05,
     (0, 0, 512, 512), (17, 16), 0),
    ("g-stripe", 12, 10, "undecided", "stripe", 1, 0.05,
     (0, 0, 512, 512), (17, 16), 0),
    # 贴满全图：种子只有菱形边上那一圈，往外全被菱形判定挡回来
    ("g-all1", 8, 8, "undecided", "all1", 1, 0.05,
     (0, 0, 512, 512), (12, 12), 0),
    # 非 1 号编号 + 岸砖地形
    ("g-blob2-shore", 12, 12, "shore-ring", "disk-in-2", 2, 0.05,
     (0, 0, 512, 512), (18, 18), 0),
    # 负 weight：total 变负 → 主循环整个跳过（0x5A0D0A 的 jle）
    ("g-wneg", 12, 12, "undecided", "disk", 1, -0.05,
     (0, 0, 512, 512), (18, 18), 1),
]

HEAD = (u"# 0x5A08D0 AngularGrow 整函数模拟执行基准\n"
        u"# 生成脚本 code/randmap/dump_angular_grow_golden.py\n"
        u"# IN/OUT 行 = x y tile subtile level owner queued flag4b\n"
        u"# WaterSet=314 ShorePieces=89 GreenTile=493 ClearToGreenLat=494\n")


def main():
    out = []
    for (name, w, h, terr, own, blob, weight, rect, origin, flush) in CASES:
        o = GrowOracle(w, h)
        o.terrain(terr)
        o.owners(own)
        o.set_level30c(4)
        before = o.snap()
        ret, calls = o.grow(blob, weight, rect, origin, flush)
        after = o.snap()
        dummy = o.rd32(0xABDC50 + 0x24)
        head = ("CASE %s %d %d terrain=%s owners=%s blob=%d weight=%s"
                " rect=%d,%d,%d,%d origin=%d,%d flush=%d -> ret=%d rng=%d"
                " dummy=%d,%d"
                % (name, w, h, terr, own, blob, repr(weight), rect[0], rect[1],
                   rect[2], rect[3], origin[0], origin[1], 1 if flush else 0,
                   ret, calls, dummy & 0xFFFF, (dummy >> 16) & 0xFFFF))
        out.append(head)
        for k in sorted(before):
            out.append("  IN %d %d %d %d %d %d %d %d" % (k + before[k]))
        for k in sorted(after):
            out.append("  OUT %d %d %d %d %d %d %d %d" % (k + after[k]))
        print(head)
        sys.stdout.flush()
    path = (sys.argv[1] if len(sys.argv) > 1
            else "../../memory/data/rmg/angular_grow_golden.txt")
    with io.open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(HEAD)
        f.write(u"\n".join(out) + u"\n")
    print("wrote %s (%d lines)" % (path, len(out)))


if __name__ == "__main__":
    main()
