# -*- coding: utf-8 -*-
"""IsoMapPack5 编码 golden：Unicorn 跑原版 0x56B3F0 完整链（LZOPipe + BufferStream 写）

布局还原（汇编实锤）：
  - 0x56B3F0(MapClass, &EXT)：栈上构造 LZOPipe(方向0, 块0x2000)，FUN_00631cc0 把
    [LZOPipe+4] = &EXT（LZOPipe 兼作流管理器，+4=当前流）
  - LZOPipe::Put/Flush 写块：FUN_00631d10(ecx=LZOPipe) → [LZOPipe+4]=&EXT →
    [EXT vtable+0x10]=0x7BA3C0 BufferStream::Write → memcpy(EXT+0xC + EXT+0x18, data, n)
  - EXT+0x18 = 已写字节数（used）；EXT vtable+4 = Flush 槽（用空函数 0x631D3C）
  - 0x55C2B0 构造 new[0x4000]*2、压缩 work new[0x10000]：operator_new 在 Unicorn
    不可用（无系统调用），hook 0x7C8E17 返回伪堆递增地址，0x7C8B3D no-op

用法: python code/randmap/dump_isomap_golden.py
输出: memory/data/rmg/isomap_golden.txt
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu_oracle import Oracle, IMAGE_BASE, STACK_BASE, STACK_SIZE, MAGIC_RET

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_ESP, UC_X86_REG_EAX, UC_X86_REG_ECX,
                               UC_X86_REG_EDX, UC_X86_REG_EIP)

OBJ = 0x31000000          # LZOPipe 栈对象区（0x56B3F0 自己用栈，这里只映射给 hook 堆）
HEAP = 0x31100000         # hook 伪堆基址
VT = 0x33000000           # 外部流 vtable
EXT = 0x33001000          # 外部流对象
BUF = 0x33002000          # BufferStream 输出缓冲
CELLS = 0x33010000        # 512x512 CellClass* 数组（1MB）
CELLOBJ = 0x33110000      # CellClass 对象区

ADDR_PIPE_CTOR = 0x55C2B0
ADDR_PIPE_DTOR = 0x55C310
ADDR_PIPE_PUT = 0x55C350
ADDR_PIPE_GET = 0x55C5E0    # Flush（vtable+4）
ADDR_NEW = 0x7C8E17
ADDR_DELETE = 0x7C8B3D
ADDR_WRITE = 0x7BA3C0       # BufferStream::Write
ADDR_FLUSH_EMPTY = 0x631D3C  # xor eax,eax; ret
ADDR_SETSTREAM = 0x631CC0   # FUN_00631cc0(管理器, 流)
ADDR_ISOMAP = 0x56B3F0

heap_top = [HEAP]


def make_hooks(uc, trace=False):
    def hook(uc_, address, size, user_data):
        if address == ADDR_NEW:
            sz = struct.unpack("<I", uc_.mem_read(uc_.reg_read(UC_X86_REG_ESP) + 4, 4))[0]
            a = heap_top[0]
            heap_top[0] = (a + sz + 0xF) & ~0xF
            ret = struct.unpack("<I", uc_.mem_read(uc_.reg_read(UC_X86_REG_ESP), 4))[0]
            if trace:
                print("  [new %d -> 0x%X]" % (sz, a))
            uc_.reg_write(UC_X86_REG_EAX, a)
            uc_.reg_write(UC_X86_REG_ESP, uc_.reg_read(UC_X86_REG_ESP) + 4)
            uc_.reg_write(UC_X86_REG_EIP, ret)
        elif address == ADDR_DELETE:
            if trace:
                print("  [del]")
            ret = struct.unpack("<I", uc_.mem_read(uc_.reg_read(UC_X86_REG_ESP), 4))[0]
            uc_.reg_write(UC_X86_REG_ESP, uc_.reg_read(UC_X86_REG_ESP) + 4)
            uc_.reg_write(UC_X86_REG_EIP, ret)
        elif trace and address == ADDR_WRITE:
            ecx = uc_.reg_read(UC_X86_REG_ECX)
            data = struct.unpack("<I", uc_.mem_read(uc_.reg_read(UC_X86_REG_ESP) + 4, 4))[0]
            ln = struct.unpack("<I", uc_.mem_read(uc_.reg_read(UC_X86_REG_ESP) + 8, 4))[0]
            print("  [Write this=0x%X data=0x%X len=%d]" % (ecx, data, ln))
        elif trace and address == 0x631D10:
            ecx = uc_.reg_read(UC_X86_REG_ECX)
            cur = struct.unpack("<I", uc_.mem_read(ecx + 4, 4))[0]
            data = struct.unpack("<I", uc_.mem_read(uc_.reg_read(UC_X86_REG_ESP) + 4, 4))[0]
            ln = struct.unpack("<I", uc_.mem_read(uc_.reg_read(UC_X86_REG_ESP) + 8, 4))[0]
            print("  [631D10 ecx=0x%X cur=0x%X data=0x%X len=%d]" % (ecx, cur, data, ln))
        elif trace and address == ADDR_SETSTREAM:
            ecx = uc_.reg_read(UC_X86_REG_ECX)
            stream = struct.unpack("<I", uc_.mem_read(uc_.reg_read(UC_X86_REG_ESP) + 4, 4))[0]
            print("  [631CC0 ecx=0x%X stream=0x%X]" % (ecx, stream))
        elif trace and address == ADDR_ISOMAP:
            sp = uc_.reg_read(UC_X86_REG_ESP)
            vals = [struct.unpack("<I", uc_.mem_read(sp + i*4, 4))[0] for i in range(6)]
            print("  [56B3F0 入口 esp=0x%X [esp+0..0x14]=%s]" % (sp, [hex(v) for v in vals]))
    uc.hook_add(UC_HOOK_CODE, hook, None, ADDR_NEW, ADDR_NEW)
    uc.hook_add(UC_HOOK_CODE, hook, None, ADDR_DELETE, ADDR_DELETE)
    if trace:
        uc.hook_add(UC_HOOK_CODE, hook, None, ADDR_WRITE, ADDR_WRITE)
        uc.hook_add(UC_HOOK_CODE, hook, None, ADDR_SETSTREAM, ADDR_SETSTREAM)
        uc.hook_add(UC_HOOK_CODE, hook, None, 0x631D10, 0x631D10)
        uc.hook_add(UC_HOOK_CODE, hook, None, ADDR_ISOMAP, ADDR_ISOMAP)


def call_0x56b3f0(uc, map_obj, ext_ptr):
    sp = STACK_BASE + STACK_SIZE - 0x2000
    # 0x56B3F0 读 [esp_in + 0xC]（sub 0x34 + ret8 后 [esp+0x40]）；填多个槽位实验定位
    for i in range(6):
        uc.mem_write(sp + i * 4, struct.pack("<I", 0x1000 + i))  # 0x1000..0x1005
    uc.mem_write(sp, struct.pack("<I", MAGIC_RET))
    uc.mem_write(sp + 4, struct.pack("<I", ext_ptr & 0xFFFFFFFF))
    uc.reg_write(UC_X86_REG_ESP, sp)
    uc.reg_write(UC_X86_REG_ECX, map_obj)
    uc.emu_start(ADDR_ISOMAP, MAGIC_RET)
    return uc.reg_read(UC_X86_REG_EAX)


def setup_cells(uc):
    """在 CELLS 数组放 4 个对象：2 有效 2 过滤。返回对象清单"""
    objs = []

    def put(idx, coord, iso, h, lvl, unk):
        base = CELLOBJ + len(objs) * 0x200
        uc.mem_write(base, b"\x00" * 0x200)
        uc.mem_write(base + 0x24, struct.pack("<I", coord))
        uc.mem_write(base + 0x38, struct.pack("<I", iso))
        uc.mem_write(base + 0x119, bytes([unk]))
        uc.mem_write(base + 0x11A, bytes([h]))
        uc.mem_write(base + 0x11B, bytes([lvl]))
        uc.mem_write(CELLS + idx * 4, struct.pack("<I", base))
        objs.append((idx, coord, iso, h, lvl, unk, base))
        return base

    # 有效 1：(40,40) X+Y=80>74 ✓ |diff|=0<74 ✓ 80<=238 ✓
    put(40 * 512 + 40, 40 << 16 | 40, 0x00010001, 0x1A, 0x05, 0x77)
    # 有效 2：(60,50) X+Y=110 ✓
    put(50 * 512 + 60, 50 << 16 | 60, 0x00000002, 0x00, 0x01, 0x00)
    # 过滤 1：(30,30) X+Y=60 <= 74 → 跳过
    put(30 * 512 + 30, 30 << 16 | 30, 0x00000003, 0x10, 0x02, 0x11)
    # 过滤 2：(40,41) X+Y=81 ✓ 但 iso=0xFFFF 且 level=0 → 跳过
    put(41 * 512 + 40, 41 << 16 | 40, 0x0000FFFF, 0x20, 0x00, 0x22)
    # cells[0] = NULL（i==0 跳过）
    return objs


def main():
    o = Oracle()
    uc = o.uc
    try:
        uc.mem_map(OBJ & ~0xFFF, 0x100000)
        uc.mem_map(HEAP & ~0xFFF, 0x100000)
        uc.mem_map(VT & ~0xFFF, 0x400000)   # 覆盖 VT/EXT/BUF/CELLS/CELLOBJ
    except Exception:
        pass
    make_hooks(uc)

    # 外部流 vtable：+4 = Flush(空), +0x10 = Write
    uc.mem_write(VT + 4, struct.pack("<I", ADDR_FLUSH_EMPTY))
    uc.mem_write(VT + 0x10, struct.pack("<I", ADDR_WRITE))
    # 外部流对象
    uc.mem_write(EXT, struct.pack("<I", VT))
    uc.mem_write(EXT + 0x04, struct.pack("<I", 0))
    uc.mem_write(EXT + 0x08, struct.pack("<I", 0))
    uc.mem_write(EXT + 0x0C, struct.pack("<I", BUF))
    uc.mem_write(EXT + 0x10, struct.pack("<I", 0x8000))
    uc.mem_write(EXT + 0x14, struct.pack("<I", 0))
    uc.mem_write(EXT + 0x18, struct.pack("<I", 0))   # used

    # MapClass 全局 0x87F7E8
    MAP = 0x87F7E8
    W, H = 70, 70
    uc.mem_write(MAP + 0xF4, struct.pack("<I", W + 4))
    uc.mem_write(MAP + 0xF8, struct.pack("<I", H + 12))
    uc.mem_write(MAP + 0x13C, struct.pack("<I", CELLS))
    uc.mem_write(MAP + 0x140, struct.pack("<I", 0x40000))

    setup_cells(uc)

    ret = call_0x56b3f0(uc, MAP, EXT)
    used = struct.unpack("<I", bytes(uc.mem_read(EXT + 0x18, 4)))[0]
    out = bytes(uc.mem_read(BUF, used))
    print("0x56B3F0 返回 = %d, BufferStream used = %d" % (ret, used))
    print("块流 hex (%d B):" % used)
    print(out.hex())
    # 解析块：每块 [u16 csize][u16 usize][lzo]
    p = 0
    while p + 4 <= len(out):
        csize, usize = struct.unpack_from("<HH", out, p)
        p += 4
        data = out[p:p + csize]
        print("  块: csize=%d usize=%d lzo_hex=%s" % (csize, usize, data.hex()))
        p += csize
    if p != len(out):
        print("  !! 尾随 %d 字节未解析" % (len(out) - p))

    out_path = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__)))), "memory", "data", "rmg", "isomap_golden.txt")
    with open(out_path, "w") as f:
        f.write("## IsoMapPack5 0x56B3F0 编码 golden（W=70 H=70）\n")
        f.write("ret=%d used=%d\n" % (ret, used))
        f.write("blocks_hex=" + out.hex() + "\n")
    print("written:", out_path)


if __name__ == "__main__":
    main()
