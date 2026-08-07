# -*- coding: utf-8 -*-
"""从 theater ini 算出 45 个 TileSet 全局各自的**起始砖号**。

`memory/data/rmg/tileset_globals.txt` 只解决了"全局 <-> ini 键名"这一半；
另一半是"键名给的组序号 -> 该组的起始砖号"。答案在 0x545C6B 那个循环里：
它按 TileSet0000、0001、…… 的顺序枚举，`ebx` 累加每组的 `TilesInSet`，
每轮开头 `ebx` 就是当前组的起始砖号，命中键名时原样写进全局。

也就是说 start(k) = sum(TilesInSet[0..k-1])，一条前缀和。

交叉验证（两条都对上了，见 docs/random-map/theater-data.md）：
  IsWater 0x485060 硬编码的区间宽度 0x0E == WaterSet 的 TilesInSet = 14
  岸砖清扫硬编码的 0x2A                == ShorePieces 的 TilesInSet = 42

⚠️ 本脚本只读 mix，不把 theater 数据写进仓库；输出的是**起始砖号表**，
是我们自己算的派生结果，不是原版素材。
"""
import io
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "mix"))
from mix_read import open_mix, MixFile   # noqa: E402

GAME = r"E:\YRLauncher"

# 只有温和与雪地两种气候会出现在 RMG 里（游戏内实测：气候下拉框就这两项）
THEATERS = [("temperate", "temperatmd.ini"), ("snow", "snowmd.ini")]

# 随机地图生成器真正会读的那些全局，按 tileset_globals.txt 的对应关系列出。
# 其余 30 来个全局同法可得，这里只列本项目用得到的。
KEYS = [
    ("0xAA0738", "WaterSet"),
    ("0xABAD28", "ShorePieces"),
    ("0xAA0E18", "GreenTile"),
    ("0xAA0748", "ClearToGreenLat"),
    ("0xAA10B0", "ClearTile"),
    ("0xABC2B8", "RoughTile"),
    ("0xABB104", "SandTile"),
    ("0xAA0E24", "ClearToSandLat"),
    ("0xABB10C", "Rocks"),
    ("0xAA073C", "WaterfallEast"),
    ("0xABB110", "WaterfallWest"),
    ("0xAA10A0", "WaterfallNorth"),
    ("0xAA1050", "WaterfallSouth"),
    ("0xAA101C", "WaterCliffs"),
    ("0xABC1D8", "RampBase"),
    ("0xAA0E28", "BridgeSet"),
    ("0xABC2CC", "BlackTile"),
]


def load_theater_inis():
    inner = open_mix(os.path.join(GAME, "ra2md.mix")).get("localmd.mix")
    m = MixFile(inner)
    return {name: m.get(fn) for name, fn in THEATERS}


def parse_ini(blob):
    """够用的 ini 解析：本项目只需要 [General] 与 [TileSetNNNN] 两类节。"""
    out, cur = {}, None
    for line in blob.decode("latin1").splitlines():
        line = line.split(";")[0].strip()
        m = re.match(r"\[(\w+)\]", line)
        if m:
            cur = m.group(1)
            out.setdefault(cur, {})
            continue
        if cur and "=" in line:
            k, v = line.split("=", 1)
            out[cur][k.strip().lower()] = v.strip()
    return out


def tile_starts(ini):
    """前缀和：start(k) = sum(TilesInSet[0..k-1])。"""
    ids = sorted(int(k[7:]) for k in ini if k.startswith("TileSet"))
    starts, acc = {}, 0
    for i in ids:
        starts[i] = acc
        acc += int(ini["TileSet%04d" % i].get("tilesinset", "0"))
    return starts, acc


def main():
    inis = load_theater_inis()
    lines = [
        u"## theater TileSet \u5168\u5c40\u7684**\u8d77\u59cb\u7816\u53f7**",
        u"## \u751f\u6210\u811a\u672c code/randmap/dump_tileset_starts.py",
        u"## start(k) = sum(TilesInSet[0..k-1])\uff0c\u4f9d\u636e 0x545C6B "
        u"\u90a3\u4e2a\u7d2f\u52a0\u5faa\u73af",
        u"",
    ]
    for name, _fn in THEATERS:
        ini = parse_ini(inis[name])
        starts, total = tile_starts(ini)
        gen = ini["General"]
        lines.append(u"### %s\uff08\u5171 %d \u5757\u7816\uff0c%d \u4e2a TileSet\uff09"
                     % (name, total, len(starts)))
        lines.append(u"")
        lines.append(u"| \u5168\u5c40 | ini \u952e | \u7ec4\u5e8f\u53f7 | "
                     u"\u8d77\u59cb\u7816\u53f7 | \u7ec4\u5185\u7816\u6570 | SetName |")
        lines.append(u"|---|---|---|---|---|---|")
        for glob, key in KEYS:
            v = gen.get(key.lower())
            if v is None:
                lines.append(u"| %s | %s | - | - | - | \uff08\u8be5 theater "
                             u"\u672a\u5b9a\u4e49\uff09|" % (glob, key))
                continue
            i = int(v)
            s = ini["TileSet%04d" % i]
            lines.append(u"| %s | %s | %d | %d | %s | %s |"
                         % (glob, key, i, starts[i], s.get("tilesinset", "0"),
                            s.get("setname", "")))
        lines.append(u"")
        print("%s: total=%d sets=%d water=%d shore=%d"
              % (name, total, len(starts),
                 starts[int(gen["waterset"])], starts[int(gen["shorepieces"])]))
    path = os.path.join(os.path.dirname(__file__), "..", "..",
                        "memory", "data", "rmg", "tileset_starts.txt")
    io.open(path, "w", encoding="utf-8").write(u"\n".join(lines) + u"\n")


if __name__ == "__main__":
    main()
