# -*- coding: utf-8 -*-
"""贴砖器 0x57B440 的整函数模拟执行。

比前几个 oracle 多两件事：
  1) 要伪造一个 `TileTypeClass`（[0x880990]）。它有虚表——`+0x2C` 必须返回
     0x12（RTTI 校验），`+0x9C` 必须返回子块偏移表的基址。虚表函数直接
     指到 scratch 里两段手写机器码，比钩子干净。
  2) 要把 theater 的那一堆砖号全局摆好，否则 0x4863D0 / 0x578D80 两个
     地形保护谓词会拿 0 去比，判据全乱。数值取自
     memory/data/rmg/tileset_starts.txt 的 temperate 一列。

记录的基准是「整块贴完之后地图上发生的全部改动」：
  返回值、出参 ok、以及每个被写过的格子的 (x, y, tile, subtile, level)
  和工作数组 +0x38 的变化。
"""
import struct
import sys

from dump_blob_paint_golden import PaintOracle, WORK_SIZE, CELL_SIZE

A_PLACE = 0x57B440

# temperate 的砖号全局（tileset_starts.txt）
THEATER = {
    0xAA0738: 314,   # WaterSet
    0xABAD28: 89,    # ShorePieces
    0xAA073C: 539,   # WaterfallEast
    0xABB110: 547,   # WaterfallWest
    0xAA10A0: 543,   # WaterfallNorth
    0xAA1050: 414,   # WaterfallSouth
    0xAA101C: 148,   # WaterCliffs
    0xABC1D8: 29,    # RampBase
    0xAA0E28: 277,   # BridgeSet
    0xAA1020: 39,    # CliffSet
    0xABBEBC: 601,   # CliffRamps
    0xAA1058: 621,   # RampSmooth
    0xABAD24: -1,    # WaterCaves（temperate 未定义）
    0xABAD1C: -1,    # WoodBridgeSet
    0xABC2C8: 761,   # DestroyableCliffs
}


class PlaceOracle(PaintOracle):
    def __init__(self, w, h, seed=1):
        PaintOracle.__init__(self, w, h, seed)
        self.wr32(0x87F8DC, w)
        self.wr32(0x87F8E0, h)
        self.wr32(0x87F924, self.cellptr)
        for a, v in THEATER.items():
            self.wr32(a, v)
        # 哑元格子 0xABDC50 归零，免得上一个用例的残留影响这一个
        for off in range(0, CELL_SIZE, 4):
            self.wr32(0xABDC50 + off, 0)
        self.tt = None

    # ---- 伪造 TileTypeClass ----
    def make_tile(self, tile_number, tw, th, blocks):
        """blocks: [(present, height[, ramp]), ...]，长 tw*th，按 y*tw+x 排。

        第三项 ramp 是 .tmp 子块头 +0x2A，只有另一个压砖器 0x5A6C10 会读它
        （写进 CellClass +0x11C）；0x57B440 不看，缺省给 0。
        """
        obj = self._bump(0x310)
        vt = self._bump(0x100)
        # 两段桩：`mov eax, imm32; ret` —— 虚表调用是 thiscall 无参，直接 ret。
        # ⚠️ 子块偏移表要先分配，否则拿不到它的地址往桩里填。
        offs = self._bump(4 * tw * th + 0x10)
        f2c = self._bump(16)
        self.uc.mem_write(f2c, b"\xB8" + struct.pack("<I", 0x12) + b"\xC3")
        f9c = self._bump(16)
        self.uc.mem_write(f9c, b"\xB8" + struct.pack("<I", offs) + b"\xC3")
        self.wr32(vt + 0x2C, f2c)
        self.wr32(vt + 0x9C, f9c)
        self.wr32(obj, vt)
        self.wr32(obj + 0x294, tile_number)
        self.wr32(obj + 0x2E4, tw)
        self.wr32(obj + 0x2E8, th)
        # 子块：0x57B557 读 [base + k*4 + 0x10]，非零才贴；+0x28 是高度增量
        for k, blk_spec in enumerate(blocks):
            present, height = blk_spec[0], blk_spec[1]
            ramp = blk_spec[2] if len(blk_spec) > 2 else 0
            if not present:
                self.wr32(offs + 0x10 + k * 4, 0)
                continue
            blk = self._bump(0x40)
            self.uc.mem_write(blk + 0x28, bytes([height & 0xFF]))
            self.uc.mem_write(blk + 0x2A, bytes([ramp & 0xFF]))
            self.wr32(offs + 0x10 + k * 4, blk)
        self.wr32(0x880990, obj)
        self.tt = obj
        return obj

    # ---- 地图状态 ----
    def cell(self, x, y):
        return self.rd32(self.cellptr + ((y << 9) + x) * 4)

    def set_cell(self, x, y, tile=None, subtile=None, level=None):
        c = self.cell(x, y)
        if not c:
            return
        if tile is not None:
            self.wr32(c + 0x38, tile)
        if subtile is not None:
            self.uc.mem_write(c + 0x11A, bytes([subtile & 0xFF]))
        if level is not None:
            self.uc.mem_write(c + 0x11B, bytes([level & 0xFF]))

    def set_work_blob(self, x, y, v):
        self.wr32(self.work + (y * self.side + x) * WORK_SIZE + 0x38, v)

    def snapshot(self):
        out = {}
        for (x, y) in self._diamond():
            c = self.cell(x, y)
            if not c:
                continue
            t = self.rd32(c + 0x38)
            st = self.uc.mem_read(c + 0x11A, 1)[0]
            lv = self.uc.mem_read(c + 0x11B, 1)[0]
            wb = self.rd32(self.work + (y * self.side + x) * WORK_SIZE + 0x38)
            out[(x, y)] = (t, st, lv, wb)
        return out

    def place(self, at, allow_lo, allow_hi, base_level, blob_id, no_track):
        """at = (x, y) 真实坐标，写进 [0x88095C]。"""
        self.uc.mem_write(0x88095C, struct.pack("<hh", at[0], at[1]))
        okp = self._bump(4)
        self.wr32(okp, 1)
        before = self.snapshot()
        # ret 0x18 = 6 个 dword，压栈顺序与 0x57B1B3 起一致
        ret = self._call(A_PLACE, 0x87F7E8,
                         (allow_lo & 0xFFFFFFFF, allow_hi & 0xFFFFFFFF,
                          base_level & 0xFFFFFFFF, blob_id & 0xFFFFFFFF,
                          okp, 1 if no_track else 0))
        ok = self.uc.mem_read(okp, 1)[0]
        after = self.snapshot()
        diff = sorted((k, before[k], after[k]) for k in after if before[k] != after[k])
        return ret & 0xFF, ok, diff


# 每个用例：名字，砖号，砖宽高，子块 (present, height)，
# 预置格子 [(x,y,tile,subtile,level)]，预置工作归属 [(x,y,blob)]，
# 落点，allow_lo, allow_hi, base_level, blob_id, no_track
CASES = [
    ("plain-2x2", 89, 2, 2, [(1, 0)] * 4, [], [], (10, 20), 0, 0, 3, 5, 0),
    ("hole", 89, 2, 2, [(1, 0), (0, 0), (1, 0), (1, 0)], [], [], (10, 20), 0, 0, 3, 5, 0),
    ("heights", 89, 2, 2, [(1, 0), (1, 4), (1, 2), (1, 0)], [], [], (10, 20), 0, 0, 3, 5, 0),
    ("3x3-clip", 89, 3, 3, [(1, 0)] * 9, [], [], (1, 20), 0, 0, 3, 5, 0),
    ("blocked-outside-allow", 89, 2, 2, [(1, 0)] * 4,
     [(11, 20, 500, 0, 0)], [], (10, 20), 0, 0, 3, 5, 0),
    ("inside-allow", 89, 2, 2, [(1, 0)] * 4,
     [(11, 20, 500, 0, 0)], [], (10, 20), 400, 600, 3, 5, 0),
    ("notrack-same-facing", 89, 2, 2, [(1, 0)] * 4,
     [(10, 20, 89, 0, 0)], [], (10, 20), 0, 0, 3, 5, 1),
    ("notrack-facing-clash", 89, 2, 2, [(1, 0)] * 4,
     [(10, 20, 105, 0, 0)], [], (10, 20), 0, 0, 3, 5, 1),
    ("notrack-allowed", 89, 2, 2, [(1, 0)] * 4,
     [(10, 20, 89, 0, 0)], [], (10, 20), 89, 130, 3, 5, 1),
    ("otherblob-same-family", 90, 2, 2, [(1, 0)] * 4,
     [(11, 20, 89, 0, 0)], [(11, 20, 7)], (10, 20), 0, 0, 3, 5, 0),
    ("otherblob-diff-family", 92, 2, 2, [(1, 0)] * 4,
     [(11, 20, 89, 0, 0)], [(11, 20, 7)], (10, 20), 0, 0, 3, 5, 0),
    ("blob-minus-one", 89, 2, 2, [(1, 0)] * 4,
     [(11, 20, 500, 0, 0)], [], (10, 20), 0, 0, 3, -1, 0),
    ("silent-fail", 89, 2, 2, [(1, 0)] * 4,
     [(11, 20, 500, 0, 0)], [(11, 20, 5)], (10, 20), 0, 0, 3, 5, 0),
    ("protected-cliff-cell", 89, 2, 2, [(1, 0)] * 4,
     [(11, 20, 40, 0, 0)], [(11, 20, 5)], (10, 20), 0, 0, 3, 5, 0),
    ("protected-by-newtile", 40, 2, 2, [(1, 0)] * 4,
     [(11, 20, 100, 0, 0)], [(11, 20, 5)], (10, 20), 0, 0, 3, 5, 0),
    ("shore-over-shore", 95, 2, 2, [(1, 0)] * 4,
     [(11, 20, 100, 0, 0)], [(11, 20, 5)], (10, 20), 0, 0, 3, 5, 0),
    ("waterfall-east", 89, 2, 2, [(1, 0)] * 4,
     [(11, 20, 540, 1, 0)], [(11, 20, 5)], (10, 20), 0, 0, 3, 5, 0),
    ("waterfall-edge-sub0", 89, 2, 2, [(1, 0)] * 4,
     [(11, 20, 539, 0, 0)], [(11, 20, 5)], (10, 20), 0, 0, 3, 5, 0),
    ("waterfall-edge-sub2", 89, 2, 2, [(1, 0)] * 4,
     [(11, 20, 539, 2, 0)], [(11, 20, 5)], (10, 20), 0, 0, 3, 5, 0),
    ("1x1", 89, 1, 1, [(1, 0)], [], [], (12, 18), 0, 0, 7, 3, 0),
]


def main():
    out = []
    for (name, tn, tw, th, blocks, cells, works,
         at, lo, hi, lvl, blob, notrack) in CASES:
        o = PlaceOracle(20, 20)
        o.make_tile(tn, tw, th, [(bool(p), hgt) for p, hgt in blocks])
        for (x, y, t, st, lv) in cells:
            o.set_cell(x, y, tile=t, subtile=st, level=lv)
        for (x, y, b) in works:
            o.set_work_blob(x, y, b)
        ret, ok, diff = o.place(at, lo, hi, lvl, blob, bool(notrack))
        out.append("CASE %s tile=%d size=%dx%d at=%d,%d allow=%d,%d level=%d "
                   "blob=%d notrack=%d -> ret=%d ok=%d"
                   % (name, tn, tw, th, at[0], at[1], lo, hi, lvl, blob,
                      notrack, ret, ok))
        for (x, y), b, a in diff:
            out.append("  CELL %d %d %d %d %d %d" % (x, y, a[0], a[1], a[2],
                                                     a[3] & 0xFFFFFFFF))
    text = "\n".join(out) + "\n"
    if len(sys.argv) > 1:
        with open(sys.argv[1], "w", encoding="utf-8") as f:
            f.write("## 贴砖器 0x57B440 黄金基准（生成脚本 "
                    "code/randmap/dump_place_tile_golden.py）\n")
            f.write("## 地图 20x20，theater 取 temperate 的砖号全局\n")
            f.write(text)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
