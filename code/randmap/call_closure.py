# -*- coding: utf-8 -*-
"""按「指令可达性」把 150 个取数点归到生成流水线的各个阶段。

为什么不用 Ghidra 的调用图：
  MapGen 这段代码里有相当多的函数 Ghidra 根本没定义（没有直接引用、
  或被当成数据），调用图一走到那儿就断了，可达集会莫名其妙地少一大截。
  这里改成从阶段入口做**递归下降反汇编**，记录访问过的每一条指令地址，
  取数点落在被访问的指令上才算这个阶段用得到。不依赖任何函数边界。

  代价是会跟着 jmp 跨进别的函数（尾调用/共享 epilogue），属于过近似；
  但那毕竟是真实的控制转移，不会凭空多出取数点。

  ⚠ switch 跳转表必须跟：MapGen 里 `0x5A1350` 这种一次消耗 15 个随机数的函数，
  入口就是 `jmp dword ptr [edx*4 + 表基址]`，不解表就整段看不见。

输入：memory/data/rmg/rng_callsites_mapgen.txt
输出：memory/data/rmg/rng_ledger.txt
用法：python code/randmap/call_closure.py [exe路径]
"""
import io
import os
import re
import struct
import sys

from capstone import CS_ARCH_X86, CS_MODE_32, Cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM

_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO = os.path.dirname(os.path.dirname(_HERE))
SRC = os.path.join(_REPO, "memory", "data", "rmg", "rng_callsites_mapgen.txt")
OUT = os.path.join(_REPO, "memory", "data", "rmg", "rng_ledger.txt")

# 阶段入口逐条取自主流程 FUN_00598960 的反编译（调试日志字符串之间夹着的调用）
STAGES = [
    (u"1  Init random map",        [0x599650]),
    (u"2  Seeding water",          [0x5981F0, 0x59A6C0, 0x59C580, 0x59C630]),
    (u"3  Init regions",           [0x5AC290, 0x58CF90, 0x58E740, 0x58E9B0, 0x58D010]),
    (u"4  Making regions",         [0x58EBC0, 0x58EF10, 0x5A19E0, 0x578E60, 0x5A17F0]),
    (u"5  Recalc cell attributes", [0x59B740, 0x578350, 0x578290, 0x47D2B0]),
    (u"6  Creating starting pts",  [0x594B50, 0x5A1FB0]),
    (u"7  Adding tech buildings",  [0x5A95B0]),
    (u"8  Creating tiberium",      [0x5A23A0]),
    (u"9  Creating hills",         [0x5A35F0]),
    (u"10 Creating LATs, rocks",   [0x5A38C0, 0x5A3AE0, 0x5A4280]),
    (u"11 Cleanup",                [0x722D00, 0x722240, 0x5AD790, 0x568BB0, 0x42F7C0]),
    (u"12 Compute Radar Image",    [0x654490, 0x654650]),
]

# 已实锤的间接调用边（函数指针字段），递归下降跟不出来，只能人工补
#   0x5980C0 NormalDeviate 的 this = 全局 0xABDFB8，其 +0x10 字段在
#   0x58B79D（构造）和 0x5989BF（每次生成前重填）两处都写成 0x598000，
#   函数体里 `call dword ptr [esi+0x10]` 调两次 —— 见 docs/random-map/rng-ledger.md
INDIRECT = {0x5980C0: [0x598000]}

STOP = {"ret", "retf", "iret", "iretd", "hlt", "ud2", "jmp"}
COND = set("""jo jno js jns je jz jne jnz jb jnae jc jnb jae jnc jbe jna ja jnbe
jl jnge jge jnl jle jng jg jnle jp jpe jnp jpo jcxz jecxz loop loope loopne""".split())


class Text(object):
    def __init__(self, path):
        d = open(path, "rb").read()
        self.raw = d
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        nsec = struct.unpack_from("<H", d, pe + 6)[0]
        optsz = struct.unpack_from("<H", d, pe + 20)[0]
        self.base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
        self.secmap = []          # (文件偏移, 起始 VA, 长度)
        for i in range(nsec):
            o = pe + 24 + optsz + i * 40
            rva = struct.unpack_from("<I", d, o + 12)[0]
            praw = struct.unpack_from("<I", d, o + 20)[0]
            rsz = struct.unpack_from("<I", d, o + 16)[0]
            if rsz:
                self.secmap.append((praw, self.base + rva, rsz))
            if d[o:o + 8].rstrip(b"\0") == b".text":
                va = struct.unpack_from("<I", d, o + 12)[0]
                pr = struct.unpack_from("<I", d, o + 20)[0]
                rs = struct.unpack_from("<I", d, o + 16)[0]
                self.lo = self.base + va
                self.hi = self.lo + rs
                self.buf = d[pr:pr + rs]

    def slice_at(self, va, n=64):
        off = va - self.lo
        return self.buf[off:off + n]

    def func_start(self, va):
        """往回找 >=3 字节的 0x90/0xCC 对齐填充，其后第一字节即函数入口。"""
        a, run = va, 0
        for _ in range(0x2000):
            a -= 1
            if not (self.lo <= a < self.hi):
                return None
            if self.buf[a - self.lo] in (0x90, 0xCC):
                run += 1
                if run >= 3:
                    return a + run
            else:
                run = 0
        return None

    def refs(self, va):
        """全镜像里指向 va 的引用处：E8/E9 相对调用跳转 + 任意 4 字节绝对值。"""
        out = []
        for j in range(len(self.buf) - 5):
            if self.buf[j] in (0xE8, 0xE9):
                rel = struct.unpack_from("<i", self.buf, j + 1)[0]
                if self.lo + j + 5 + rel == va:
                    out.append(self.lo + j)
        imm = struct.pack("<I", va)
        i = 0
        while True:
            j = self.raw.find(imm, i)
            if j < 0:
                break
            i = j + 1
            v = self.off2va(j)
            if v is not None:
                out.append(v)
        return out

    def off2va(self, off):
        for lo_off, lo_va, size in self.secmap:
            if lo_off <= off < lo_off + size:
                return lo_va + (off - lo_off)
        return None

    def dword(self, va):
        off = va - self.lo
        if 0 <= off <= len(self.buf) - 4:
            return struct.unpack_from("<I", self.buf, off)[0]
        return None


def jump_table(text, ins):
    """`jmp/call dword ptr [reg*4 + imm32]` → 表里连续的 .text 指针。

    表末尾靠"下一个 dword 不是 .text 地址"自然收口——MSVC6 会把 byte 索引表
    紧跟在 dword 表后面，那些字节解出来必然不是合法代码地址。
    """
    op = ins.operands[0]
    if op.type == X86_OP_MEM or ins.mnemonic in ("jmp", "call"):
        m = getattr(op, "mem", None)
        if m is None or m.scale != 4 or m.disp == 0:
            return []
        out = []
        addr = m.disp & 0xFFFFFFFF
        for _ in range(256):
            v = text.dword(addr)
            if v is None or not (text.lo <= v < text.hi):
                break
            out.append(v)
            addr += 4
        return out
    return []


def walk(text, roots):
    """递归下降：返回访问过的指令地址集合。"""
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    seen = set()          # 指令地址
    blocks = set()        # 已排队的块起点
    stack = [r for r in roots]
    while stack:
        pc = stack.pop()
        if pc in blocks or not (text.lo <= pc < text.hi):
            continue
        blocks.add(pc)
        stack.extend(INDIRECT.get(pc, ()))
        cur = pc
        while text.lo <= cur < text.hi:
            code = text.slice_at(cur, 32)
            ins = None
            for ins in md.disasm(code, cur, count=1):
                break
            if ins is None:
                break
            if ins.address in seen:
                break            # 撞上已走过的路径
            seen.add(ins.address)
            m = ins.mnemonic
            op = ins.operands[0] if ins.operands else None
            if m == "call":
                if op is not None and op.type == X86_OP_IMM:
                    stack.append(op.imm)
                elif op is not None and op.type == X86_OP_MEM:
                    stack.extend(jump_table(text, ins))
                # 其余间接调用（call reg / call [ecx+n] 虚表）：跟不了，如实跳过
            elif m in COND:
                if op is not None and op.type == X86_OP_IMM:
                    stack.append(op.imm)
            elif m in STOP:
                if m == "jmp":
                    if op is not None and op.type == X86_OP_IMM:
                        stack.append(op.imm)
                    elif op is not None and op.type == X86_OP_MEM:
                        stack.extend(jump_table(text, ins))
                break
            cur = ins.address + ins.size
    return seen


def main(exe):
    text = Text(exe)

    sites = []   # (mov 处, kind, call 处)
    pat = re.compile(r"^0x([0-9A-F]{6})\s+(\w+)\s+call@0x([0-9A-F]{6})")
    for raw in io.open(SRC, "r", encoding="utf-8", errors="replace"):
        m = pat.match(raw.strip())
        if m and m.group(2) == "Random":
            sites.append((int(m.group(1), 16), int(m.group(3), 16)))

    f = io.open(OUT, "w", encoding="utf-8")
    f.write(u"## 随机数消耗总账：取数点 -> 生成阶段\n")
    f.write(u"## 取数点 %d 处（this == 0xABE890 的 Random 调用）\n" % len(sites))
    f.write(u"## 归属方式：从阶段入口递归下降反汇编，取数点落在访问到的指令上才算数\n\n")

    owner = {}
    for label, roots in STAGES:
        seen = walk(text, roots)
        hit = sorted(s for s, _ in sites if s in seen)
        f.write(u"### 阶段 %s\n访问指令 %d 条；取数点 %d 处\n" % (label, len(seen), len(hit)))
        f.write(u"  " + u" ".join(u"%06X" % a for a in hit) + u"\n\n")
        for a in hit:
            owner.setdefault(a, []).append(label.split()[0])

    miss = [s for s, _ in sites if s not in owner]
    f.write(u"### 归属统计\n")
    f.write(u"唯一归属 %d 处；多阶段共用 %d 处；无归属 %d 处\n\n"
            % (sum(1 for v in owner.values() if len(v) == 1),
               sum(1 for v in owner.values() if len(v) > 1), len(miss)))
    for a in sorted(owner):
        if len(owner[a]) > 1:
            f.write(u"  %06X  共用: %s\n" % (a, u"/".join(owner[a])))
    if miss:
        f.write(u"\n### 没有任何阶段能到达的取数点\n")
        f.write(u"给出所在函数（按 MSVC6 的 0x90 对齐填充往回找入口）以及"
                u"**全镜像**里指向该入口的引用数——\n"
                u"引用数为 0 就是链接器留下的死代码，一个随机数都不会消耗。\n\n")
        groups = {}
        for a in sorted(miss):
            groups.setdefault(text.func_start(a), []).append(a)
        fns = sorted(k for k in groups if k is not None)
        refs = dict((fn, text.refs(fn)) for fn in fns)
        # 传递性判死：入口没人引用 = 死；引用它的又全在已判死的函数体内 = 也死
        dead = set(fn for fn in fns if not refs[fn])
        changed = True
        while changed:
            changed = False
            for fn in fns:
                if fn in dead:
                    continue
                owners = [text.func_start(r) for r in refs[fn]]
                if owners and all(o in dead for o in owners):
                    dead.add(fn)
                    changed = True
        for fn in fns:
            f.write(u"  函数 %06X  引用 %d 处%s  取数点: %s\n"
                    % (fn, len(refs[fn]),
                       u"  ← 死代码" if fn in dead else u"  ⚠ 还有活引用",
                       u" ".join(u"%06X" % a for a in groups[fn])))
        ndead = sum(len(groups[fn]) for fn in dead)
        f.write(u"\n判定为死代码的函数 %d 个，占掉取数点 %d 处；"
                u"真正会被执行的取数点 %d 处\n"
                % (len(dead), ndead, len(sites) - ndead))
        if None in groups:
            f.write(u"  ⚠ 找不到入口: %s\n"
                    % u" ".join(u"%06X" % a for a in groups[None]))
    f.close()
    print("written: %s  (%d sites, %d unattributed)" % (OUT, len(sites), len(miss)))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else r"E:\YRLauncher\gamemd.exe")
