# -*- coding: utf-8 -*-
"""从 gamemd.exe 的 .rdata/.data 里按虚拟地址读浮点/整数常量。

用法：
  python code/randmap/read_const.py double 0x7e3808 0x7e4f50 ...
  python code/randmap/read_const.py float  0x7ed7c0
  python code/randmap/read_const.py dword  0x89c2dc
  python code/randmap/read_const.py bytes  0x89f688 32
"""
import struct
import sys

EXE = r"E:\YRLauncher\gamemd.exe"


def _sections(buf):
    pe = struct.unpack_from("<I", buf, 0x3C)[0]
    nsec = struct.unpack_from("<H", buf, pe + 6)[0]
    optsz = struct.unpack_from("<H", buf, pe + 20)[0]
    base = struct.unpack_from("<I", buf, pe + 24 + 28)[0]
    tbl = pe + 24 + optsz
    out = []
    for i in range(nsec):
        o = tbl + i * 40
        name = buf[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
        vsz, va, rsz, ro = struct.unpack_from("<IIII", buf, o + 8)
        # vsz 决定这一节在内存里占多长（VA 归属看它）；rsz 决定文件里实际有多少字节。
        # 早前这里写的是 max(vsz, rsz)，会把落在「虚拟尾巴」（.bss 那种文件里没有的部分）
        # 的 VA 也算进来，然后按 ro + (va - sva) 读出**别的节的内容**——静默给假数据。
        out.append((name, base + va, vsz if vsz else rsz, rsz, ro))
    return out


def va2off(secs, va):
    """VA → 文件偏移。落在「文件里不存在」的虚拟尾巴上返回 None（而不是读到隔壁节）。"""
    for name, sva, vsz, rsz, ro in secs:
        if sva <= va < sva + vsz:
            delta = va - sva
            if delta >= rsz:
                return None          # 未初始化数据，文件里没有
            return ro + delta
    return None


def main(argv):
    kind = argv[0]
    with open(EXE, "rb") as f:
        buf = f.read()
    secs = _sections(buf)
    if kind == "bytes":
        va = int(argv[1], 16)
        n = int(argv[2], 0) if len(argv) > 2 else 16
        off = va2off(secs, va)
        if off is None:
            print("0x%08X = <不在文件里（.bss 之类的虚拟尾巴）>" % va)
            return
        raw = buf[off:off + n]
        print("0x%08X = %s" % (va, raw.hex()))
        print("        = %s" % " ".join("%d" % b for b in bytearray(raw)))
        return
    for s in argv[1:]:
        va = int(s, 16)
        off = va2off(secs, va)
        if off is None:
            print("0x%08X = <不在任何节内>" % va)
            continue
        if kind == "double":
            raw = buf[off:off + 8]
            print("0x%08X = %-24r  raw=%s" % (va, struct.unpack("<d", raw)[0], raw[::-1].hex()))
        elif kind == "float":
            raw = buf[off:off + 4]
            print("0x%08X = %-24r  raw=%s" % (va, struct.unpack("<f", raw)[0], raw[::-1].hex()))
        else:
            raw = buf[off:off + 4]
            v = struct.unpack("<I", raw)[0]
            print("0x%08X = %d (0x%X)" % (va, v, v))


if __name__ == "__main__":
    main(sys.argv[1:])
