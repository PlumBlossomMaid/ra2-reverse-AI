# -*- coding: utf-8 -*-
"""把 place_blobs_golden.txt 编成 C++ 用例表（test/place_blobs_golden.inc）。"""
import io
import re

SRC = "../../memory/data/rmg/place_blobs_golden.txt"
DST = "../rewrite/rmg/test/place_blobs_golden.inc"

HEAD = (u"// \u672c\u6587\u4ef6\u7531 code/randmap/gen_place_blobs_inc.py \u751f\u6210"
        u"\uff0c\u52ff\u624b\u6539\u3002\n"
        u"// \u6570\u636e\u6e90\uff1amemory/data/rmg/place_blobs_golden.txt\n")

PAT = re.compile(
    r"CASE (\d+) (\d+) 0x([0-9A-F]{8}) mt=(\d+) players=(\d+) water=(\d+) "
    r"usable=(-?\d+),(-?\d+),(-?\d+),(-?\d+) -> cells=(\d+) rng=(\d+) stamp=(\d+)")


def chunks(s, n):
    """按空格切开，别把 "16:27" 从中间劈成两半。"""
    out, cur = [], ""
    for tok in s.split():
        if cur and len(cur) + 1 + len(tok) > n:
            out.append(cur)
            cur = tok
        else:
            cur = tok if not cur else cur + " " + tok
    if cur:
        out.append(cur)
    return out


def main():
    lines = io.open(SRC, encoding="utf-8").read().splitlines()
    out = [HEAD, "const PlaceCase kCases[] = {"]
    i = 0
    n = 0
    while i < len(lines):
        m = PAT.search(lines[i])
        if not m:
            i += 1
            continue
        g = m.groups()
        order = lines[i + 1].strip()[len("ORDER "):]
        out.append("    {%s, %s, 0x%su, %s, %s, %s, {%s, %s, %s, %s}, %s, %s, %s,"
                   % (g[0], g[1], g[2], g[3], g[4], g[5], g[6], g[7], g[8], g[9],
                      g[10], g[11], g[12]))
        for c in chunks(order, 100):
            out.append('     "%s "' % c.strip())
        out[-1] = out[-1][:-2] + '"},'
        n += 1
        i += 2
    out.append("};")
    io.open(DST, "wb").write(b"\xef\xbb\xbf" + ("\n".join(out) + "\n").encode("utf-8"))
    print("cases=%d" % n)


if __name__ == "__main__":
    main()
