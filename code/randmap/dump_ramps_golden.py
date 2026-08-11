# -*- coding: utf-8 -*-
u"""坡道建造函数六连（四直坡 + 三转角）的逐字节对拍基准。

直接调原版建造函数（不经 0x590970 八分支调度），每个用例构造：
  1) 30×30 地图，全格 tile=0xFFFF(UNDECIDED)/level=0/ramp=0/overlay=-1
  2) 净空矩形内 owner 全设 low_id（0x594010 要求 owner ∈ {hi, low}）
  3) 区域表两个成员：hi（level=4）、low（level=0）
  4) 直接调用 0x591740 / 0x591D80 / 0x592440 / 0x593AF0 / 0x593550 /
     0x593030（__thiscall，ecx = hi 区域指针，栈参数 a/b/lowId）

观测：函数返回值 + RNG 消耗 + 全图终态（只存**变化格**，C++ 侧验证
"变化格全对上 + 未变化格保持输入"双向约束）。

用例坐标（30×30，菱形判定 x+y>W、x+y≤W+2H+2、|x−y|<W 全过）：
  东直坡（dy=a.Y-b.Y 沿 Y）：E1 dx=0（不取数）E2 dx=-2（取 1）
  北直坡（dx=a.X-b.X 沿 X）：N1 dy=0（不取数）N2 dy=-1（取 1）
  西直坡（dy=b.Y-a.Y 沿 Y）：W1 dx=0（不取数）W2 dx=-2（取 1）
  转角（0 取数）：NE/ES/SW 各一个 dx=dy=7
"""
import struct
import sys

from dump_making_regions_golden import MakingRegionsOracle, REGVEC, WORK_SIZE
from dump_regions_golden import sx

A_EAST = 0x591740      # BuildStraightEast
A_NORTH = 0x591D80     # BuildStraightNorth
A_WEST = 0x592440      # BuildStraightWest
A_CORNER_NE = 0x593AF0
A_CORNER_ES = 0x593550
A_CORNER_SW = 0x593030

UNDECIDED = 0xFFFF


class RampsOracle(MakingRegionsOracle):
    def __init__(self, seed):
        # 30×30、可用区全图、mt=3（MapSeedClass +0x3C）、RegionSize/Accessibility 随意。
        # ⚠️ 不要覆盖 self.side：StageOracle 按 side = w+h+1 = 61 分配 work
        # 数组（61² 格），菱形（h*(2w-1) = 1770 格）全部落在数组内；用 30 会
        # 让 set_owner/cell_state 的下标错位，Clearance 读到脏 owner。
        MakingRegionsOracle.__init__(self, 30, 30, seed, 2, 0, 3,
                                     (0, 0, 30, 30), 0, 90, "mix")
        # ⚠️ 建造函数直读的 TheaterClass 全局：_build_tiles 只实体化了砖表
        # （[0xA8ED2C]），没设这几个标量——0x591740 等函数的
        # `mov eax,[0xabc1f8]`（SlopeSetPieces→肩膀砖 tileIdx）会读到 bss 的 0，
        # 砖表[0]=0 → StampTile 崩溃；[0xABC1D8]（RampBase）为 0 会让直坡段
        # 砖号与 C++ 侧（temperate 29）不一致。值必须与 C++ 测试 TheaterData
        # 对齐：ramp_base=29、slope_set_pieces=200、paved_roads=300、
        # paved_road_ends=340。
        self.wr32(0xABC1D8, 29)    # RampBase（temperate 真实值）
        self.wr32(0xABC1F8, 200)   # SlopeSetPieces（合成值）
        self.wr32(0xABBEC8, 300)   # PavedRoads（合成值）
        self.wr32(0xABBEC4, 340)   # PavedRoadEnds（合成值）

    def make_region(self, rid, level):
        r = self._bump(0x200)
        self.wr32(r + 0x08, rid)                 # id
        self.wr32(r + 0x0C, 400)                 # cellCount（建造函数不读）
        self.wr32(r + 0x10, level)               # level（+0x10，this->level）
        self.uc.mem_write(r + 0x14, bytes([0]))  # wet
        self.wr32(r + 0x16, 0)                   # seedX/Y
        self.uc.mem_write(r + 0x1A, bytes([0]))  # finalized
        data = self.rd32(REGVEC + 0x04)
        if not data:
            data = self._bump(64)
            self.wr32(REGVEC + 0x04, data)
        n = self.rd32(REGVEC + 0x10)
        self.wr32(data + n * 4, r)
        self.wr32(REGVEC + 0x10, n + 1)
        self.wr32(REGVEC + 0x14, max(self.rd32(REGVEC + 0x14), n + 1))
        return r

    def set_owner(self, x, y, owner):
        self.wr32(self.work + (y * self.side + x) * WORK_SIZE + 0x38, owner)

    def set_cell_clean(self, x, y):
        # UNDECIDED 平地：IsPlain(0x486380) 恒真，建造函数直接贴砖
        self.set_cell(x, y, tile=UNDECIDED, subtile=0, level=0)
        self.uc.mem_write(self.cell(x, y) + 0x11C, bytes([0]))    # ramp
        self.wr32(self.cell(x, y) + 0x44, 0xFFFFFFFF)             # overlay -1
        self.uc.mem_write(self.cell(x, y) + 0x11E, bytes([0]))    # overlayData
        self.set_owner(x, y, 0xFFFFFFFF)

    def setup_map(self, hi_ptr, low_id, rect):
        # 菱形内 x/y < w+h = 60 < side = 61，work 索引恒有效（side²=3721 格、
        # 菱形 1770 格）。首版误用 side=30 导致 set_owner 下标错位——全部
        # ret=0 的根因。
        for (x, y) in self._diamond():
            self.set_cell_clean(x, y)
        # 净空矩形（含四角）内 owner 全设 low_id → 0x594010 通过
        x0, y0, w, h = rect
        for yy in range(y0, y0 + h):
            for xx in range(x0, x0 + w):
                if not (xx + yy > 30 and xx + yy <= 92 and
                        xx - yy < 30 and yy - xx < 30):
                    continue
                self.set_owner(xx, yy, low_id)

    def run_builder(self, fn, hi_ptr, a, b, low_id):
        a_buf = self._bump(8)
        self.uc.mem_write(a_buf, struct.pack("<hh", a[0], a[1]))
        b_buf = self._bump(8)
        self.uc.mem_write(b_buf, struct.pack("<hh", b[0], b[1]))
        self.rng_calls = 0
        ret = self._call(fn, hi_ptr, (a_buf, b_buf, low_id))
        return ret & 0xFF, self.rng_calls

    def cell_state(self, x, y):
        c = self.cell(x, y)
        r = self.work + (y * self.side + x) * WORK_SIZE
        return (self.rd32(c + 0x38),
                self.uc.mem_read(c + 0x11A, 1)[0],
                struct.unpack("b", self.uc.mem_read(c + 0x11B, 1))[0],
                self.uc.mem_read(c + 0x11C, 1)[0],
                sx(self.rd32(c + 0x44)),
                self.uc.mem_read(c + 0x11E, 1)[0],
                sx(self.rd32(r + 0x38)), sx(self.rd32(r + 0x3C)))


# 名字, 函数, hi_level, low_id(区域id), a, b, 净空矩形（原版 Clearance 语义）
# ⚠️ 直坡沿 Y 方向（东/西）的 Rect 是 {x, y, 跨X+11, dy+9}——w/h 与
#    沿 X 方向（南/北）相反，owner 铺设范围必须照原版矩形，否则 Clearance
#    查到未铺格的 0xFFFFFFFF 直接失败。
CASES = [
    ("east-dx0", A_EAST, 4, 0, (22, 28), (22, 20), (19, 16, 11, 17)),
    ("east-dx-2", A_EAST, 4, 0, (23, 28), (21, 20), (18, 16, 13, 17)),
    ("north-dy0", A_NORTH, 4, 0, (28, 22), (20, 22), (16, 15, 16, 11)),
    ("north-dy-1", A_NORTH, 4, 0, (28, 23), (20, 22), (16, 15, 16, 12)),
    ("west-dx0", A_WEST, 4, 0, (22, 22), (22, 30), (15, 18, 11, 17)),
    ("west-dx-2", A_WEST, 4, 0, (23, 22), (21, 30), (14, 18, 13, 17)),
    ("corner-ne", A_CORNER_NE, 4, 0, (22, 22), (30, 30), (15, 18, 18, 18)),
    ("corner-es", A_CORNER_ES, 4, 0, (30, 24), (22, 32), (15, 17, 18, 18)),
    ("corner-sw", A_CORNER_SW, 4, 0, (30, 30), (22, 22), (20, 15, 18, 18)),
]

HEAD = (u"# 坡道建造函数六连逐字节基准\n"
        u"# 生成脚本 code/randmap/dump_ramps_golden.py\n"
        u"# CASE name W H seed= hi= low= a=x,y b=x,y rng= ret=\n"
        u"# IN/OUT x y tile subtile level ramp overlay overlayData owner queued\n"
        u"# 只存变化格；未变化格必须保持输入（双向约束）\n")


def main():
    out = []
    for (name, fn, hi_level, low_id, a, b, rect) in CASES:
        o = RampsOracle(0x5EED0000 | (len(out) & 0xFFFF))
        hi_ptr = o.make_region(1, hi_level)      # id=1 高区
        o.make_region(0, 0)                      # id=0 低区
        o.setup_map(hi_ptr, low_id, rect)
        before = {}
        for (x, y) in o._diamond():
            t, st, lv, rp, ov, od, ow, q = o.cell_state(x, y)
            before[(x, y)] = (t, st, lv, rp, ov, od, ow, q)
        ret, rng = o.run_builder(fn, hi_ptr, a, b, low_id)
        head = ("CASE %s 30 30 seed=0x%08X hi=1 hi_lvl=%d low=%d"
                " a=%d,%d b=%d,%d -> rng=%d ret=%d"
                % (name, 0x5EED0000 | (len(out) & 0xFFFF), hi_level, low_id,
                   a[0], a[1], b[0], b[1], rng, ret))
        out.append(head)
        for (x, y) in sorted(before):
            t, st, lv, rp, ov, od, ow, q = before[(x, y)]
            out.append("  IN %d %d %d %d %d %d %d %d %d %d"
                       % (x, y, t, st, lv, rp, ov, od, ow, q))
        for (x, y) in sorted(before):
            cur = o.cell_state(x, y)
            if cur != before[(x, y)]:
                out.append("  OUT %d %d %d %d %d %d %d %d %d %d" %
                           ((x, y) + cur))
        print(head)
    path = (sys.argv[1] if len(sys.argv) > 1
            else "memory/data/rmg/ramps_golden.txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write(HEAD)
        f.write("\n".join(out) + "\n")
    print("wrote %s (%d lines)" % (path, len(out)))


if __name__ == "__main__":
    main()
