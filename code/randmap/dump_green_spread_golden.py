# -*- coding: utf-8 -*-
u"""绿地扩张 0x59B740 的整函数模拟执行（阶段 5 的前半段）。

跑的是 0x598E1A 调用的那个真身，一个桩都不留：
  0x4867B0 IsGreen / 0x486380 未定砖 / 0x481810 邻格 / 0x5657A0 哑元退化
  0x5AD820 向量构造 / 0x5AD010 Resize / Randomizer::Random
全部原身。malloc/free 用 RegionOracle 那套带回收的 bump 分配器
（增长步长 10000，每次 Resize 要 40KB，跑大图会连着要好几次）。

环境沿用 RegionOracle：
  · 摘掉 0x4865D0 的「恒真」桩 —— 跑阶段 2 拿真实地形时要用它的真身
  · [0xAA0748] ClearToGreenLat = 494，[0xAA0E18] GreenTile = 493（temperate）
  · MapSeedClass 用静态实例 0xABDFD8

⚠️ 共享哑元 0xABDC50 也参与这一段：出图的邻格全部退化成它，而它开局的
+0x38 是 0（「还没定砖」），于是它会被真的收进前沿表、还可能被抽中涂绿。
一旦被涂成 GreenTile，后面所有出图邻格就都不再「未定」而被跳过。
所以基准把哑元的 +0x38 与 +0x24 的**进场值和终值**都记下来。

基准记：
  IN  行 —— 阶段 5 的输入（每格 tile / subtile / level）
  OUT 行 —— 终态（同三样；0x59B740 只写 +0x38，另两样应当原样不动）
  外加随机数调用次数、哑元 +0x38 与 +0x24 的进场值/终值
"""
import struct
import sys

from dump_regions_golden import RegionOracle
from dump_stage_water_golden import GREEN

A_SPREAD = 0x59B740
DUMMY = 0xABDC50


A_RNGCTOR = 0x65C6D0


class GreenOracle(RegionOracle):
    def reseed(self, seed):
        u"""把 Randomizer 重新播种。

        跑过阶段 2 拿真实地形的用例会先消耗掉一大截随机数，
        而对拍侧只想单独测 0x59B740。进场前重播一次，
        基准里的随机流就从「刚播种」算起，
        C++ 那边一个 Randomizer(seed) 就能对上。
        """
        self._call(A_RNGCTOR, 0xABE890, (seed,))

    def spread(self):
        self.rng_calls = 0
        self._call(A_SPREAD, 0)
        return self.rng_calls

    def snap(self):
        out = {}
        for (x, y) in self._diamond():
            c = self.cell(x, y)
            out[(x, y)] = (
                self.rd32(c + 0x38),
                self.uc.mem_read(c + 0x11A, 1)[0],
                struct.unpack("b", self.uc.mem_read(c + 0x11B, 1))[0])
        return out

    def dummy(self):
        v = self.rd32(DUMMY + 0x24)
        return self.rd32(DUMMY + 0x38), v & 0xFFFF, (v >> 16) & 0xFFFF

    def paint_green(self, pred):
        u"""按谓词直接摆绿地，用来构造阶段 2 到不了的前沿形状。"""
        for (x, y) in self._diamond():
            if pred(x, y):
                self.set_cell(x, y, tile=GREEN, subtile=0, level=0)


# 名字, W, H, 种子, 玩家, WaterAmount, MapType, 可用区, 剖面, 地形
CASES = [
    # ── 阶段 2 出来的真实地形：绿地是沿岸刷出来的那一圈 ──
    ("s2-mt1-p2", 10, 10, 0x12345678, 2, 0, 1, (0, 0, 10, 10), "mix", "stage2"),
    ("s2-mt1-p4", 12, 10, 0x5A5A5A5A, 4, 100, 1, (0, 0, 12, 10), "mix", "stage2"),
    ("s2-mt0-p2", 20, 20, 0x12345678, 2, 0, 0, (0, 0, 20, 20), "mix", "stage2"),
    ("s2-mt2-p4", 12, 10, 0x0F0F0F0F, 4, 20, 2, (0, 0, 12, 10), "mix", "stage2"),
    ("s2-mt1-real", 12, 10, 0x5A5A5A5A, 4, 100, 1, (0, 0, 12, 10), "real",
     "stage2"),
    ("s2-mt0-big", 30, 30, 0xDEADBEEF, 6, 40, 0, (0, 0, 30, 30), "mix", "stage2"),
    ("s2-mt1-big", 40, 40, 0x1A2B3C4D, 4, 60, 1, (0, 0, 40, 40), "mix", "stage2"),
    # ⚠️ 全水的那一例：一格绿地都没有 → 前沿为空 → rounds = 0，
    # 整个函数一个随机数都不抽。0x59B81C 那条 `jle` 的守卫。
    ("s2-allwater", 10, 10, 0x12345678, 2, 0, 0, (0, 0, 10, 10), "mix", "stage2"),
    # ── 合成地形：前沿的形状与规模都攥在手里 ──
    # 一片未定砖里挖个水湖，绿地是沿岸那一圈 → 前沿贴着湖边一圈
    ("syn-lake", 12, 12, 0x11111111, 2, 0, 1, (0, 0, 12, 12), "mix", "water-lake"),
    # 全是绿地：四个正邻全是绿地 → 前沿只剩贴边那些出图的邻格（哑元！）
    ("syn-all-green", 8, 8, 0x22222222, 2, 0, 1, (0, 0, 8, 8), "mix",
     "green-all"),
    # 绿地条带：前沿又多又碎，rounds 上得去
    ("syn-bands", 14, 14, 0x33333333, 2, 0, 1, (0, 0, 14, 14), "mix",
     "green-stripe"),
    ("syn-bands-b", 20, 20, 0x0BADC0DE, 2, 0, 1, (0, 0, 20, 20), "mix",
     "green-stripe"),
    # 只在中心摆一格绿地：前沿 = 4（含出图退化），rounds = 1
    ("syn-one-green", 8, 8, 0x44444444, 2, 0, 1, (0, 0, 8, 8), "mix",
     "green-one"),
    # 两格绿地：前沿 7~8，rounds = 2，能看出「保序前移」的下标含义
    ("syn-two-green", 8, 8, 0x55555555, 2, 0, 1, (0, 0, 8, 8), "mix",
     "green-two"),
    # 棋盘：几乎每一格都在前沿里，rounds 会被 1000 的上限截住（大图）
    ("syn-checker", 26, 26, 0x66666666, 2, 0, 1, (0, 0, 26, 26), "mix",
     "green-checker"),
    # 30x30 的棋盘：前沿 3500 上下 → rounds 被 0x59B80F 的 1000 上限截住
    ("syn-cap1000", 30, 30, 0x99999999, 2, 0, 1, (0, 0, 30, 30), "mix",
     "green-checker"),
    # 过渡砖 ClearToGreenLat：IsGreen 的第二条判据（区间 [494, 510)）
    ("syn-lat", 10, 10, 0x77777777, 2, 0, 1, (0, 0, 10, 10), "mix", "green-lat"),
    # 贴着菱形四个尖角摆绿地：出图邻格最多，专盯哑元那条路
    ("syn-corners", 10, 10, 0x88888888, 2, 0, 1, (0, 0, 10, 10), "mix",
     "green-corner"),
]

HEAD = (u"# 绿地扩张 0x59B740 整函数模拟"
        u"执行基准\n"
        u"# 生成脚本 code/randmap/dump_green_spread_golden.py\n"
        u"# IN  x y tile subtile level\n"
        u"# OUT x y tile subtile level\n"
        u"# WaterSet=314 ShorePieces=89 GreenTile=493 ClearToGreenLat=494"
        u"（temperate）\n")


def build(o, terrain):
    if terrain == "stage2":
        o.stage2()
        return
    if terrain == "water-lake":
        o.synth("water-lake")
        # 湖岸那一圈摆成绿地（真实流程里是阶段 2 收尾刷出来的）
        cx, cy = o.w + o.h // 2, o.h + o.w // 2
        o.paint_green(lambda x, y: abs(x - cx) + abs(y - cy) == 6)
        return
    # 底色铺「未定砖」（tile 0）—— 前沿收的就是这种格子，铺 tile 1 的话
    # 一格都收不进来（0x486380 只认 0 与 0xFFFF）。
    for (x, y) in o._diamond():
        o.set_cell(x, y, tile=0, subtile=0, level=0)
    # 菱形的真正中心：x-y == 0 且 x+y == W+H
    cx = cy = (o.w + o.h) // 2
    if terrain == "green-all":
        o.paint_green(lambda x, y: True)
    elif terrain == "green-stripe":
        o.paint_green(lambda x, y: (x + y) % 7 == 0)
    elif terrain == "green-one":
        o.paint_green(lambda x, y: (x, y) == (cx, cy))
    elif terrain == "green-two":
        o.paint_green(lambda x, y: (x, y) in ((cx, cy), (cx + 3, cy + 1)))
    elif terrain == "green-checker":
        o.paint_green(lambda x, y: (x + y) % 2 == 0)
    elif terrain == "green-corner":
        # 菱形的四条边上（到中心的等距曼哈顿距离最大的那些格）
        o.paint_green(lambda x, y: (x + y) in (o.w + 1, o.w + 2 * o.h)
                      or abs(x - y) in (o.w - 1, o.w - 2))
    elif terrain == "green-lat":
        # 过渡砖：ClearToGreenLat = 494，取区间里的几块
        for (x, y) in o._diamond():
            if (x + y) % 5 == 0:
                o.set_cell(x, y, tile=494 + ((x + y) // 5) % 16, subtile=0,
                           level=0)
    else:
        raise ValueError(terrain)


def main():
    out = []
    for (name, w, h, seed, players, water, mt, usable, profile,
         terrain) in CASES:
        o = GreenOracle(w, h, seed, players, water, mt, usable, profile)
        build(o, terrain)
        before = o.snap()
        d_in = o.dummy()
        o.reseed(seed)
        calls = o.spread()
        after = o.snap()
        d_out = o.dummy()
        head = ("CASE %s %d %d seed=0x%08X mt=%d tiles=%s terrain=%s"
                " -> rng=%d din=%d,%d,%d dout=%d,%d,%d"
                % (name, w, h, seed, mt, profile, terrain, calls,
                   d_in[0], d_in[1], d_in[2], d_out[0], d_out[1], d_out[2]))
        out.append(head)
        for (x, y) in sorted(before):
            t, st, lv = before[(x, y)]
            out.append("  IN %d %d %d %d %d" % (x, y, t, st, lv))
        for (x, y) in sorted(after):
            t, st, lv = after[(x, y)]
            out.append("  OUT %d %d %d %d %d" % (x, y, t, st, lv))
        print(head)
        sys.stdout.flush()
    path = (sys.argv[1] if len(sys.argv) > 1
            else "../../memory/data/rmg/green_spread_golden.txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write(HEAD)
        f.write("\n".join(out) + "\n")
    print("wrote %s (%d lines)" % (path, len(out)))


if __name__ == "__main__":
    main()
