# -*- coding: utf-8 -*-
u"""对拍夹具用的**砖尺寸剖面**：合成两套 + 真实一套。

三套剖面各有分工，谁也替代不了谁：

  mix  —— 岸砖 `1 + k%2` 宽、`1 + (k/2)%2` 高，四种尺寸都出现。
           故意让砖盖住右/下邻格，好把贴砖器"整块否定"的路径压出来。
  unit —— 一律 1x1，谁也遮不住谁；只有这样 0x57B0ED 那四条纯对角分支
           才能真正跑到（被 2x2 砖提前盖住就没机会了）。
  real —— 从 `memory/data/rmg/tile_geometry.txt` 读真实 `.tmp` 头。

真实数据里岸砖有 1x2/2x1/2x2/2x3/3x2/6x4/9x5 七种尺寸，
但**高度增量与 RampType 全是 0、且只有 shore41/42 有空洞**——
也就是说真实数据压不出「层高累加」与「空洞跳过」两条分支，
所以合成剖面必须留着。反过来真实数据能压出合成剖面给不出的
大砖（9x5 一块盖 45 格）与非对称尺寸。

另记两条从真实数据里读出来的交叉验证：
  · 水砖恰好是**前 8 块 2x2 + 后 6 块 1x1**，与 0x59C630 里
    `shape = n / 40 ∈ [0,7]`（大水面）与 `8 + n/40 ∈ [8,13]`（小浪花）
    的分工严丝合缝——反过来给「除数是 40 不是 10」再加一条独立证据。
  · 温和与雪地两个气候的这 56 块砖，**几何完全相同**（只有像素不同），
    所以一套 real 剖面对两个气候都成立。
"""
import os

_HERE = os.path.dirname(os.path.abspath(__file__))
GEOM_PATH = os.path.join(_HERE, "..", "..", "memory", "data", "rmg",
                         "tile_geometry.txt")

_CACHE = {}


def _load():
    u"""返回 {(group, k): (w, h, [(present, height, ramp), ...])}，k 为组内序号。"""
    if _CACHE:
        return _CACHE
    base = {}
    cur = None
    with open(GEOM_PATH, encoding="utf-8") as f:
        for line in f:
            p = line.split()
            if not p or p[0].startswith("#"):
                continue
            if p[0] == "TILE":
                theater, group, num = p[1], p[2], int(p[3])
                if theater != "temperate":     # 两气候几何相同，取一份即可
                    cur = None
                    continue
                base.setdefault(group, num)
                cur = (group, num - base[group])
                _CACHE[cur] = (int(p[5]), int(p[6]), [])
            elif p[0] == "SUB" and cur is not None:
                _CACHE[cur][2].append((int(p[2]) != 0, int(p[3]), int(p[4])))
    return _CACHE


def shore_spec(k, profile="mix"):
    u"""第 k 块岸砖（k = tile_number - ShorePieces）的 (w, h, blocks)。"""
    if profile == "real":
        return _load()[("shore", k)]
    if profile == "unit":
        w, h = 1, 1
    else:
        w, h = 1 + (k % 2), 1 + ((k // 2) % 2)
    return w, h, [(True, 0, 0)] * (w * h)


def water_spec(k, profile="mix"):
    u"""第 k 块水砖（k = tile_number - WaterSet）的 (w, h, blocks)。

    合成剖面的 RampType 取 `1 + (k*5 + j*3) % 17` —— 真实数据全是 0，
    那样 `CellClass +0x11C` 的写入就观测不到了，所以合成值必须非零。
    """
    if profile == "real":
        return _load()[("water", k)]
    w, h = (2, 2) if k < 8 else (1, 1)
    return w, h, [(True, 0, 1 + (k * 5 + j * 3) % 17) for j in range(w * h)]


if __name__ == "__main__":
    for prof in ("mix", "unit", "real"):
        s = [shore_spec(k, prof)[:2] for k in range(0x2A)]
        wt = [water_spec(k, prof)[:2] for k in range(0x0E)]
        print(prof, "shore", sorted(set(s)), "water", sorted(set(wt)))
