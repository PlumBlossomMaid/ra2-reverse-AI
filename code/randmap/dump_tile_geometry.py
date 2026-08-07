# -*- coding: utf-8 -*-
u"""从 theater 的 .tmp 砖文件里取出**几何元数据**，给对拍夹具当真实输入。

在此之前，岸线 / 阶段 2 / 水面细化三份基准用的都是"合成砖尺寸"
（`shore_tile_size`：1x1 / 2x1 / 1x2 / 2x2 轮着来）。合成尺寸能把贴砖器的
分支压全，但它不是原版真值——真跑一张图时砖有 2x2 也有 3x2。
这个脚本把真值取出来，好让基准既有"压分支"的一套，也有"照实情"的一套。

.tmp 文件头（由 0x546B10-0x546B84 的装载循环实锤）：
    dword[0]  BlocksX   -> TileTypeClass +0x2E4，**只取低字节**（0x546B46 `mov al,[ebx]`）
    dword[1]  BlocksY   -> +0x2E8，同样只取低字节
    dword[2]  单块像素宽（60）
    dword[3]  单块像素高（30）
    dword[4..] 子块偏移表，共 BlocksX*BlocksY 项；**0 = 空洞**
               0x546B66 那个循环把非零且小于映像基址的值原地加上基址，
               换算成绝对指针——所以后面 `[esi+0x28]` 读的是子块头。
子块头里本项目要的两个字节：
    +0x28 Height    贴砖器 0x57B440 的逐格高度增量
    +0x2A RampType  压砖器 0x5A6C10 写进 CellClass +0x11C（0x57B440 不写）

⚠️ 只导出 RMG 已复刻部分真正会读的两组砖（WaterSet 14 块 + ShorePieces 42 块，
两种气候共 112 条），且只导出几何数值——不含任何像素、调色板或音频。
输出是我们自己算出来的派生结果，不是搬运原版素材。
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "mix"))
from mix_read import open_mix, MixFile   # noqa: E402

GAME = os.environ.get("RA2_GAME_DIR", r"E:\YRLauncher")

# (气候名, theater ini, .tmp 扩展名, 砖库 mix 的搜索顺序——md 覆盖包在前)
THEATERS = [
    ("temperate", "temperatmd.ini", "tem", ["isotemmd.mix", "isotemp.mix"]),
    ("snow", "snowmd.ini", "sno", ["isosnomd.mix", "isosnow.mix"]),
]

# 要导出的组：ini [General] 里的键名 -> 便于阅读的短名
GROUPS = [("ShorePieces", "shore"), ("WaterSet", "water")]


def parse_ini(blob):
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


def tile_geometry(blob):
    u"""返回 (w, h, [(present, height, ramp), ...])，子块按 y * w + x 排。"""
    bx, by = struct.unpack_from("<ii", blob, 0)
    w, h = bx & 0xFF, by & 0xFF          # 0x546B46/0x546B50 只取低字节
    offs = struct.unpack_from("<%di" % (bx * by), blob, 0x10)
    cells = []
    for o in offs:
        if o == 0:
            cells.append((0, 0, 0))
        else:
            cells.append((1, blob[o + 0x28], blob[o + 0x2A]))
    return w, h, cells


def main():
    ra2 = open_mix(os.path.join(GAME, "ra2.mix"))
    ra2md = open_mix(os.path.join(GAME, "ra2md.mix"))
    localmd = MixFile(ra2md.get("localmd.mix"))

    out = []
    for (theater, ini_name, ext, arc_names) in THEATERS:
        ini = parse_ini(localmd.get(ini_name))
        arcs = []
        for a in arc_names:
            blob = ra2md.get(a) or ra2.get(a)
            if blob is not None:
                arcs.append(MixFile(blob))

        # 起始砖号 = 前面所有组的 TilesInSet 之和（0x545C6B 的前缀和）
        starts, total = {}, 0
        i = 0
        while ("TileSet%04d" % i) in ini:
            starts[i] = total
            total += int(ini["TileSet%04d" % i].get("tilesinset", 0))
            i += 1

        for (key, short) in GROUPS:
            gid = int(ini["General"][key.lower()])
            sec = ini["TileSet%04d" % gid]
            base = starts[gid]
            count = int(sec["tilesinset"])
            fname = sec["filename"]
            for k in range(count):
                name = "%s%02d.%s" % (fname, k + 1, ext)
                blob = None
                for a in arcs:
                    blob = a.get(name)
                    if blob is not None:
                        break
                if blob is None:
                    raise SystemExit("missing %s" % name)
                w, h, cells = tile_geometry(blob)
                out.append("TILE %s %s %d %s %d %d"
                           % (theater, short, base + k, name.lower(), w, h))
                for j, (p, hg, rp) in enumerate(cells):
                    out.append("  SUB %d %d %d %d" % (j, p, hg, rp))

    path = (sys.argv[1] if len(sys.argv) > 1
            else "../../memory/data/rmg/tile_geometry.txt")
    head = (u"# .tmp \u7816\u6587\u4ef6\u7684\u51e0\u4f55\u5143\u6570\u636e"
            u"\uff08\u751f\u6210\u811a\u672c code/randmap/dump_tile_geometry.py\uff09\n"
            u"# TILE <theater> <group> <tileNumber> <file> <w> <h>\n"
            u"#   SUB <k> <present> <height> <ramp>\n"
            u"# \u53ea\u542b WaterSet \u4e0e ShorePieces \u4e24\u7ec4\uff1b"
            u"\u65e0\u50cf\u7d20/\u8c03\u8272\u677f\uff0c\u5747\u4e3a\u6d3e\u751f\u6570\u503c\n")
    with open(path, "w", encoding="utf-8") as f:
        f.write(head)
        f.write("\n".join(out) + "\n")
    print("wrote %s (%d lines)" % (path, len(out)))


if __name__ == "__main__":
    main()
