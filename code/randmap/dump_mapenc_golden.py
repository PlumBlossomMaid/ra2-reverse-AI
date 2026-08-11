# -*- coding: utf-8 -*-
"""地图编码 golden 基准：Unicorn 跑原版 LZO/LCW 压缩/解压取标准答案。

覆盖：
  - LZO 压缩入口  0x55BB90 (__fastcall ecx=src, edx=src_len; 栈 dst, &dst_len, work)
    （0x55BB90 内部对 <0xE 走快捷路径，>=0xE 走 0x55BC00 哈希压缩；work 为 16384*4 表）
  - LZO 解压核心  0x55C0E0 (__fastcall ecx=src, edx=src_len; 栈 dst, &dst_len)
  - LCW 压缩      0x551E50 (__fastcall ecx=src, edx=dst;   栈 src_len)
  - LCW 解压      0x551C60 (__fastcall ecx=src, edx=dst)

用法: python code/randmap/dump_mapenc_golden.py
输出: memory/data/rmg/mapenc_golden.txt
"""
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu_oracle import Oracle, IMAGE_BASE, HEAP_BASE, HEAP_SIZE

BUF = 0x31000000                     # 独立大缓冲映射（oracle 的 HEAP 只有 0x10000）
WORK = BUF + 0x00000                 # LZO 哈希表 16384 * 4
SRC = BUF + 0x10000
SRC2 = BUF + 0x30000                 # LZO 解压输入（避免污染 SRC）
DST = BUF + 0x90000
DLEN = BUF + 0x110000

ADDR_LZO_ENC = 0x55BB90
ADDR_LZO_DEC = 0x55C0E0
ADDR_LCW_ENC = 0x551E50
ADDR_LCW_DEC = 0x551C60


def call_fastcall(uc, addr, ecx, edx, stack_args=()):
    from unicorn.x86_const import (UC_X86_REG_ESP, UC_X86_REG_EAX,
                                   UC_X86_REG_ECX, UC_X86_REG_EDX)
    sp = 0x20000000 + 0x100000 - 0x2000
    for v in reversed(stack_args):
        sp -= 4
        uc.mem_write(sp, struct.pack("<I", v & 0xFFFFFFFF))
    sp -= 4
    uc.mem_write(sp, struct.pack("<I", 0x1F000000))  # 哨兵返回
    uc.reg_write(UC_X86_REG_ESP, sp)
    uc.reg_write(UC_X86_REG_ECX, ecx & 0xFFFFFFFF)
    uc.reg_write(UC_X86_REG_EDX, edx & 0xFFFFFFFF)
    uc.emu_start(addr, 0x1F000000)
    return uc.reg_read(UC_X86_REG_EAX)


def gen_samples():
    rng = random.Random(0x5EED)
    samples = []

    def add(name, data):
        samples.append((name, bytes(data)))

    # 边界：0-16 字节（LZO <0xE 快捷路径）
    for n in list(range(0, 18)) + [31, 32, 33, 63, 64, 65]:
        add("len%d" % n, bytes(rng.randrange(256) for _ in range(n)))

    # 全零/单值
    add("zero_100", b"\x00" * 100)
    add("zero_8192", b"\x00" * 8192)
    add("one_512", b"\x41" * 512)
    add("ff_8192", b"\xFF" * 8192)

    # 周期/重复模式
    add("pat_abcd", b"ABCD" * 1024)
    add("pat_0127", bytes(range(128)) * 64)
    add("pat_weird", bytes([0, 1, 0, 1, 0, 1, 2, 3]) * 300)

    # 类 cell 数据：11 字节/格的 ground/overlay/height 序列（真实地图模式）
    cell = bytearray()
    for i in range(0x200):  # 512 格
        g = (i * 2654435761) & 0xFFFF  # 伪随机 ground
        cell += struct.pack("<HHH", i, i & 0x1FF, g & 0xFFFF)
        cell += bytes([i & 0xFF, (i >> 3) & 0xFF, 0])  # bData[3]
        cell += bytes([(i >> 5) & 0x7F])  # height
        cell += bytes([i & 0xFF])  # bData2
    add("cells512", bytes(cell))

    # 真实块大小：8191/8192/8193（LZO 块边界）
    for base in (8191, 8192, 8193):
        blob = bytes(rng.randrange(256) for _ in range(base))
        add("rand%d" % base, blob)

    # 地形类：大片相同 ground + 渐变高度
    terrain = bytearray()
    for y in range(64):
        for x in range(64):
            ground = (x * 7 + y * 13) % 0x100
            terrain += struct.pack("<HHH", x, y, ground)
            terrain += bytes([(x + y) & 0xFF, 0, 0, 0, 0])
    add("terrain64", bytes(terrain))

    # 类 overlay：0x200x0x200 值域的大块（压缩比高的 map 真实数据）
    ovl = bytearray()
    for i in range(16384):
        ovl.append(rng.choice([0, 0, 0, 1, 2, 0xFF]))
    add("overlay16k", bytes(ovl))

    return samples


def main():
    out_path = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__)))), "memory", "data", "rmg", "mapenc_golden.txt")
    o = Oracle()
    uc = o.uc
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    uc.mem_map(BUF & ~0xFFF, 0x200000)  # 2MB 大缓冲
    lines = []
    samples = gen_samples()
    print("samples:", len(samples))

    for name, data in samples:
        n = len(data)
        lines.append("## %s len=%d" % (name, n))
        # 输入缓冲清零：0x551e50 会越界读 pcVar9[0x40]（TS 引擎缓冲总是 0x4000），
        # 0x55bb90 对 len<6 会读 src[0..5]；oracle 必须模拟"干净的大缓冲"
        uc.mem_write(SRC, b"\x00" * 0x8000)
        # LZO 压缩
        uc.mem_write(SRC, data)
        uc.mem_write(DST, b"\xEE" * (n * 2 + 64))
        uc.mem_write(DLEN, b"\xFF\xFF\xFF\xFF")
        uc.mem_write(WORK, b"\x00" * 0x10000)
        call_fastcall(uc, ADDR_LZO_ENC, SRC, n, (DST, DLEN, WORK))
        lzo_len = struct.unpack("<I", bytes(uc.mem_read(DLEN, 4)))[0]
        lzo_out = bytes(uc.mem_read(DST, lzo_len))
        lines.append("lzo_enc_len=%d" % lzo_len)
        lines.append("lzo_enc_hex=" + lzo_out.hex())

        # LZO 解压（解自己的输出，验证成对；输入放 SRC2 防污染）
        if lzo_len > 0:
            uc.mem_write(SRC2, lzo_out)
            uc.mem_write(DST, b"\xDD" * (n + 64))
            uc.mem_write(DLEN, b"\x00\x00\x00\x00")
            call_fastcall(uc, ADDR_LZO_DEC, SRC2, lzo_len, (DST, DLEN))
            dec_len = struct.unpack("<I", bytes(uc.mem_read(DLEN, 4)))[0]
            dec_out = bytes(uc.mem_read(DST, dec_len))
            lines.append("lzo_dec_len=%d ok=%s" % (dec_len, "YES" if dec_out == data else "NO"))

        # LCW 压缩（模拟 Pipe 缓冲：0x4000，块 0x2000，尾部清零）
        uc.mem_write(SRC, b"\x00" * 0x4000)
        uc.mem_write(SRC, data)
        uc.mem_write(DST, b"\xEE" * (n + 64))
        lcw_len = call_fastcall(uc, ADDR_LCW_ENC, SRC, DST, (n,))
        lcw_out = bytes(uc.mem_read(DST, lcw_len))
        lines.append("lcw_enc_len=%d" % lcw_len)
        lines.append("lcw_enc_hex=" + lcw_out.hex())

        # LCW 解压（解自己的输出）
        if lcw_len > 0:
            uc.mem_write(SRC, lcw_out)
            uc.mem_write(DST, b"\xDD" * (n + 64))
            lcw_dec_len = call_fastcall(uc, ADDR_LCW_DEC, SRC, DST)
            dec_out = bytes(uc.mem_read(DST, lcw_dec_len))
            lines.append("lcw_dec_len=%d ok=%s" % (lcw_dec_len, "YES" if dec_out == data else "NO"))

    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("written:", out_path, "(%d lines)" % len(lines))


if __name__ == "__main__":
    main()
