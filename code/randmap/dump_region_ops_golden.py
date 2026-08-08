# -*- coding: utf-8 -*-
u"""两个通用区域算子 0x5A0410 Erode / 0x5A0160 Dilate 的整函数模拟执行。

两者都是「MapType 3/4 的河湖支路」与阶段 4「Making regions」的公共积木，
内部会真的调 0x5A0700 CollectBorderCells、0x5657A0 GetCellAt、
0x486380 未定砖谓词、0x4865D0 水岸谓词——一个桩都不留。

环境直接沿用 RegionOracle（它已经摘掉了 0x4865D0 的恒真桩、摆好了
ClearToGreenLat 与静态 MapSeedClass 实例 0xABDFD8）。

⚠️ 这一段**不取随机数**（两个函数里没有任何 0x4CA1D0 调用），
所以基准里不记取数个数，只记终态。

基准记：
  IN  行 —— 输入（每格 tile / subtile / level / 工作记录 +0x38 / +0x3C）
  OUT 行 —— 终态（同上五项）
  外加返回值与共享哑元坐标 [0xABDC74]
"""
import io
import struct
import sys

from dump_regions_golden import RegionOracle, SEEDOBJ, sx
from dump_shore_smooth_golden import WATER, SHORE, UNDECIDED
from dump_blob_paint_golden import WORK_SIZE

A_ERODE = 0x5A0410
A_DILATE = 0x5A0160


class OpsOracle(RegionOracle):
    def __init__(self, w, h):
        RegionOracle.__init__(self, w, h, 0x1234, 2, 0, 1, (0, 0, w, h), "mix")

    # ---- 输入：地形 ----
    def terrain(self, kind):
        cx = self.w + self.h // 2
        cy = self.h + self.w // 2
        for (x, y) in self._diamond():
            d = abs(x - cx) + abs(y - cy)
            if kind == "undecided":
                tile, lvl = UNDECIDED, 0
            elif kind == "solid":
                # 全是普通砖：Dilate 里「无主但不是未定砖」那条失败路
                tile, lvl = 1, 0
            elif kind == "lake":
                tile, lvl = (WATER if d < 5 else UNDECIDED), 0
            elif kind == "shore-ring":
                # 中心水、外面一圈岸砖、再外面未定砖：flag 那条
                # 「上一号区域 + 水或岸」才走得到
                tile = WATER if d < 4 else (SHORE + 3 if d < 6 else UNDECIDED)
                lvl = 0
            elif kind == "wall":
                # 一圈砌死的普通砖，Dilate 撞上去必失败
                tile = 1 if 6 <= d <= 7 else UNDECIDED
                lvl = 0
            elif kind == "levels":
                tile, lvl = UNDECIDED, ((x + y) // 4) % 5
            else:
                raise ValueError(kind)
            self.set_cell(x, y, tile=tile, subtile=0, level=lvl)

    # ---- 输入：工作数组的归属 +0x38 与戳 +0x3C ----
    def owners(self, kind):
        cx = self.w + self.h // 2
        cy = self.h + self.w // 2
        for (x, y) in self._diamond():
            d = abs(x - cx) + abs(y - cy)
            if kind == "disk":
                v = 1 if d < 6 else 0
            elif kind == "disk-in-2":
                # 内盘 2、外环 1：Dilate 的 owner - 1 那条分支要用
                v = 2 if d < 4 else (1 if d < 6 else 0)
            elif kind == "donut":
                v = 1 if 3 <= d < 7 else 0
            elif kind == "two-blobs":
                v = 1 if (abs(x - cx + 5) + abs(y - cy) < 4 or
                          abs(x - cx - 5) + abs(y - cy) < 4) else 0
            elif kind == "stripe":
                v = 1 if abs(y - cy) <= 1 else 0
            elif kind == "disk-and-foe":
                v = 1 if d < 4 else (5 if 6 <= d <= 7 else 0)
            elif kind == "all1":
                v = 1
            elif kind == "none":
                v = 0
            else:
                raise ValueError(kind)
            self.wr32(self.work + (y * self.side + x) * WORK_SIZE + 0x38, v)

    def stamps(self, v):
        u"""预先给 +0x3C 灌一个非零残留：rings <= 1 时原版**不清**它，
        这条差异只有靠脏戳才验得出来。"""
        for (x, y) in self._diamond():
            self.wr32(self.work + (y * self.side + x) * WORK_SIZE + 0x3C, v)

    def set_level30c(self, v):
        self.wr32(SEEDOBJ + 0x30C, v & 0xFFFFFFFF)

    # ---- 快照 ----
    def snap(self):
        out = {}
        for (x, y) in self._diamond():
            c = self.cell(x, y)
            r = self.work + (y * self.side + x) * WORK_SIZE
            out[(x, y)] = (self.rd32(c + 0x38),
                           self.uc.mem_read(c + 0x11A, 1)[0],
                           struct.unpack("b", self.uc.mem_read(c + 0x11B, 1))[0],
                           sx(self.rd32(r + 0x38)), sx(self.rd32(r + 0x3C)))
        return out

    def erode(self, owner, rings, new_owner):
        self.rng_calls = 0
        r = self._call(A_ERODE, SEEDOBJ,
                       (owner & 0xFFFFFFFF, rings & 0xFFFFFFFF,
                        new_owner & 0xFFFFFFFF))
        assert self.rng_calls == 0, "Erode 不该取随机数"
        return r & 0xFF

    def dilate(self, owner, rings, rx, ry, rw, rh, flag, cell_val):
        self.rng_calls = 0
        args = (owner, rings, rx, ry, rw, rh, flag, cell_val)
        r = self._call(A_DILATE, SEEDOBJ,
                       tuple(v & 0xFFFFFFFF for v in args))
        assert self.rng_calls == 0, "Dilate 不该取随机数"
        return r & 0xFF


# ── 用例 ──
# Erode: 名字, W, H, 地形, 归属, 脏戳, level30c, owner, rings, newOwner
ERODE_CASES = [
    ("e-disk-r1", 10, 10, "undecided", "disk", 0, 4, 1, 1, 0),
    ("e-disk-r2", 10, 10, "undecided", "disk", 0, 4, 1, 2, 0),
    ("e-disk-r3", 10, 10, "lake", "disk", 0, 3, 1, 3, 0),
    ("e-disk-r9", 10, 10, "lake", "disk", 0, 2, 1, 9, 0),
    # 剥成非零编号：下一圈的「owner 不是本块就跳过」才验得准
    ("e-disk-new7", 10, 10, "undecided", "disk", 0, 5, 1, 3, 7),
    # rings <= 1 不清戳：脏戳必须原样留在终态里
    ("e-dirty-r1", 9, 9, "levels", "disk", 0x55, 6, 1, 1, 0),
    # rings > 1 清戳：同样的脏戳这次会被抹平
    ("e-dirty-r3", 9, 9, "levels", "disk", 0x55, 6, 1, 3, 0),
    ("e-donut-r2", 12, 12, "undecided", "donut", 0, 1, 1, 2, 0),
    ("e-two-blobs", 12, 10, "undecided", "two-blobs", 0, 0, 1, 2, 0),
    ("e-stripe-r2", 12, 10, "undecided", "stripe", 0, 7, 1, 2, 0),
    # 全图一块：CollectBorderCells 只捡得到贴着菱形边的那一圈
    ("e-all1-r2", 8, 8, "undecided", "all1", 0, 4, 1, 2, 0),
    # rings = 0：一格都不剥，但函数照样返回 true
    ("e-r0", 8, 8, "undecided", "disk", 0x33, 4, 1, 0, 0),
    # 负圈数：rings <= 0 的循环压根不进
    ("e-rneg", 8, 8, "undecided", "disk", 0x33, 4, 1, -2, 0),
    # 没有这个编号：边界向量是空的
    ("e-noowner", 8, 8, "undecided", "none", 0, 4, 1, 3, 0),
    # 剥成自己：+0x38 没变，于是下一圈会把同一批格子再收一遍——
    # 全靠 +0x3C 的戳挡住；戳值是 ring + 1，逐圈递增
    ("e-selfnew", 10, 10, "undecided", "disk", 0, 4, 1, 3, 1),
    # 负号编号与负层高
    ("e-negnew", 10, 10, "undecided", "disk", 0, -3, 1, 2, -1),
]

# Dilate: 名字, W, H, 地形, 归属, level30c, owner, rings, rect, flag, cellVal
DILATE_CASES = [
    ("d-disk-r2", 12, 12, "undecided", "disk", 4, 1, 2, (0, 0, 64, 64), 0, 0),
    ("d-disk-r1", 12, 12, "undecided", "disk", 4, 1, 1, (0, 0, 64, 64), 0, 0),
    ("d-disk-r5", 12, 12, "undecided", "disk", 4, 1, 5, (0, 0, 64, 64), 0, 0),
    # rings = 0：只建了个空向量就走人
    ("d-r0", 12, 12, "undecided", "disk", 4, 1, 0, (0, 0, 64, 64), 0, 0),
    # 矩形夹死：只往一个角长
    ("d-rect", 12, 12, "undecided", "disk", 4, 1, 3, (18, 18, 8, 8), 0, 0),
    # 矩形宽高为 0：一格都进不去，但**不算失败**（continue 而不是 return）
    ("d-rect-empty", 12, 12, "undecided", "disk", 4, 1, 2, (0, 0, 0, 0), 0, 0),
    # 撞上普通砖的墙：无主 + 不是未定砖 → 当场返回 false
    ("d-wall-false", 12, 12, "wall", "disk", 4, 1, 4, (0, 0, 64, 64), 0, 0),
    # 全图普通砖：第一圈第一个邻格就失败
    ("d-solid-false", 10, 10, "solid", "disk", 4, 1, 2, (0, 0, 64, 64), 0, 0),
    # 旁边坐着别人（编号 5）：隔离测试失败
    ("d-foe-false", 12, 12, "undecided", "disk-and-foe", 4, 1, 4,
     (0, 0, 64, 64), 0, 0),
    # flag = 1 且外环编号正好是 owner-1：水/岸也收，且给未定砖压层高
    ("d-flag-lvl", 12, 12, "shore-ring", "disk-in-2", 4, 2, 3,
     (0, 0, 64, 64), 1, 6),
    ("d-flag-lvl2", 12, 12, "shore-ring", "disk-in-2", 4, 2, 5,
     (0, 0, 64, 64), 1, -2),
    # 同样的局面但 flag = 0：外环编号 1 既不是 0 也不是 owner → 立刻 false
    ("d-noflag-false", 12, 12, "shore-ring", "disk-in-2", 4, 2, 3,
     (0, 0, 64, 64), 0, 6),
    # 湖：中心是水砖、外面未定砖，owner 1 是那圈甜甜圈
    ("d-donut", 12, 12, "lake", "donut", 4, 1, 2, (0, 0, 64, 64), 0, 0),
    # 层高地形 + flag：只有原本是未定砖的格子才会被压层高
    ("d-levels", 10, 10, "levels", "disk", 4, 1, 3, (0, 0, 64, 64), 1, 9),
    # 没有这个编号：边界向量为空，rings 圈全是空转，返回 true
    ("d-noowner", 10, 10, "undecided", "none", 4, 1, 3, (0, 0, 64, 64), 0, 0),
    # 两团同号：两边一起长
    ("d-two-blobs", 12, 10, "undecided", "two-blobs", 4, 1, 2,
     (0, 0, 64, 64), 0, 0),
    # 贴边的一整块：往外长会被菱形判定挡住——挡住是 continue，不是失败
    ("d-all1", 8, 8, "undecided", "all1", 4, 1, 2, (0, 0, 64, 64), 0, 0),
]


HEAD = (u"# 0x5A0410 Erode / 0x5A0160 Dilate 整函数模拟执行基准\n"
        u"# 生成脚本 code/randmap/dump_region_ops_golden.py\n"
        u"# IN/OUT 行 = x y tile subtile level owner queued\n"
        u"# WaterSet=314 ShorePieces=89 GreenTile=493 ClearToGreenLat=494\n")


def emit(out, head, before, after):
    out.append(head)
    for k in sorted(before):
        out.append("  IN %d %d %d %d %d %d %d" % (k + before[k]))
    for k in sorted(after):
        out.append("  OUT %d %d %d %d %d %d %d" % (k + after[k]))
    print(head)
    sys.stdout.flush()


def main():
    out = []
    for (name, w, h, terr, own, stamp, lvl, owner, rings, newown) \
            in ERODE_CASES:
        o = OpsOracle(w, h)
        o.terrain(terr)
        o.owners(own)
        o.stamps(stamp)
        o.set_level30c(lvl)
        before = o.snap()
        ret = o.erode(owner, rings, newown)
        after = o.snap()
        dummy = o.rd32(0xABDC50 + 0x24)
        emit(out, "CASE ERODE %s %d %d terrain=%s owners=%s stamp=%d"
                  " level30c=%d owner=%d rings=%d new=%d -> ret=%d"
                  " dummy=%d,%d"
                  % (name, w, h, terr, own, stamp, lvl, owner, rings, newown,
                     ret, dummy & 0xFFFF, (dummy >> 16) & 0xFFFF),
             before, after)

    for (name, w, h, terr, own, lvl, owner, rings, rect, flag, cv) \
            in DILATE_CASES:
        o = OpsOracle(w, h)
        o.terrain(terr)
        o.owners(own)
        o.set_level30c(lvl)
        before = o.snap()
        ret = o.dilate(owner, rings, rect[0], rect[1], rect[2], rect[3],
                       flag, cv)
        after = o.snap()
        dummy = o.rd32(0xABDC50 + 0x24)
        emit(out, "CASE DILATE %s %d %d terrain=%s owners=%s owner=%d"
                  " rings=%d rect=%d,%d,%d,%d flag=%d cellval=%d -> ret=%d"
                  " dummy=%d,%d"
                  % (name, w, h, terr, own, owner, rings, rect[0], rect[1],
                     rect[2], rect[3], flag, cv, ret,
                     dummy & 0xFFFF, (dummy >> 16) & 0xFFFF),
             before, after)

    path = (sys.argv[1] if len(sys.argv) > 1
            else "../../memory/data/rmg/region_ops_golden.txt")
    with io.open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(HEAD)
        f.write(u"\n".join(out) + u"\n")
    print("wrote %s (%d lines)" % (path, len(out)))


if __name__ == "__main__":
    main()
