# -*- coding: utf-8 -*-
"""基于 capstone 的轻量反汇编器：直接从 gamemd.exe 按 VA 反汇编，不依赖 Ghidra。

用法:
    python code/randmap/disasm.py 0x599650 0x120          # 从 VA 起反汇编 N 字节
    python code/randmap/disasm.py 0x599650 0x120 --ctx    # 只打印含指定寄存器/调用的行
"""
import struct
import sys

EXE = r"E:\YRLauncher\gamemd.exe"

KNOWN = {
    0x65C6D0: "Randomizer::Seed",
    0x65C780: "Randomizer::Random",
    0x65C7E0: "Randomizer::RandomRanged",
    0x734E60: "DebugLog(file,line)",
    0x47D2B0: "CellClass::RecalcAttributes",
    0x578350: "MapClass::CellIterator_Reset",
    0x578290: "MapClass::CellIterator_Next",
    0x641140: "PreviewClass::GeneratePreviewImage",
}


def load_pe(path):
    data = open(path, "rb").read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    secs = []
    for i in range(nsec):
        o = pe + 24 + optsz + i * 40
        name = data[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, vaddr, rsize, roff = struct.unpack_from("<IIII", data, o + 8)
        secs.append((name, vaddr, vsize, roff, rsize))
    return data, base, secs


def read_va(data, base, secs, va, n):
    for _, vaddr, vsize, roff, rsize in secs:
        rva = va - base
        if vaddr <= rva < vaddr + rsize:
            off = roff + (rva - vaddr)
            return data[off:off + n]
    raise ValueError("VA 0x%X 不在任何可读节内" % va)


def disasm(va, n, exe=EXE):
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    data, base, secs = load_pe(exe)
    code = read_va(data, base, secs, va, n)
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    out = []
    for ins in md.disasm(code, va):
        line = "%08x  %-24s %s %s" % (
            ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str)
        if ins.mnemonic in ("call", "jmp") and ins.op_str.startswith("0x"):
            t = int(ins.op_str, 16)
            if t in KNOWN:
                line += "   ; " + KNOWN[t]
        out.append(line)
    return out


def main():
    va = int(sys.argv[1], 0)
    n = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x80
    for line in disasm(va, n):
        print(line)


if __name__ == "__main__":
    main()
