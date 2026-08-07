# -*- coding: utf-8 -*-
"""涂块核心 0x59BBC0 的整函数模拟执行 —— 取「陆地生长顺序」的黄金基准。

这是本模块最重的一个 oracle：不再是取一个返回值，而是把原版的整条主循环跑起来，
逐格记录它把哪些格子变成了陆地、按什么顺序。陆地形状就是这串顺序。

伪造出来的运行环境（全部只读分析 gamemd.exe，不修改原文件）：
  MapClass::Instance 0x87F7E8  —— +0xF4/+0xF8 尺寸、+0x13C 格子指针数组
  工作数组 [0xABED10]、边界 [0xABED04]/[0xABED08]、side [0x89C2DC]
  全图矩形 [0xABE2F0..FC]、Randomizer [0xABE890]、正态对象 [0xABDFB8]
拦截（Unicorn 代码钩子，等价于把这几个调用换成桩）：
  malloc 0x7C8E17 / free 0x7C8B3D  —— 换成 bump 分配器
  IsPaintable 0x4865D0             —— 恒真（真实实现依赖 CellClass 一堆未还原字段）
  岸线平滑 0x57A0C0                —— 恒真（避免整块回滚，好让主循环的产物留下来）

⚠️ 诚实标注：中心游走用的 NormalDeviate 内部走 x87 fyl2x，而 Unicorn 的 fyl2x
不理会 FPU 控制字（见 emu_oracle.py 抬头）。因此本基准在「正态偏差」这一环上
是宿主 long double 的结果，与真 CPU 可能差 1 ulp。基准仍然有效——它检验的是
主循环的结构与取数顺序；若复刻与基准只在极少数样本上分岔，需要按 fast-math.md
第四节的办法单独判定是不是这 1 ulp 造成的。
"""
import struct
import sys

from emu_oracle import load_sections, IMAGE_BASE, MAGIC_RET

EXE = r"E:\YRLauncher\gamemd.exe"

SCRATCH = 0x40000000
SCRATCH_SIZE = 0x4000000
STACK_BASE = 0x20000000
STACK_SIZE = 0x100000

CELL_SIZE = 0x180
WORK_SIZE = 0x50

A_MALLOC = 0x7C8E17
A_FREE = 0x7C8B3D
A_PAINTABLE = 0x4865D0
A_SMOOTH = 0x57A0C0
A_RANDOM = 0x65C780
A_RNGCTOR = 0x65C6D0
A_PAINT = 0x59BBC0
A_MAIN_PAINT = 0x59C02B
A_DRAIN_PAINT = 0x59C3AB


class PaintOracle(object):
    def __init__(self, w, h, seed):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_FPCW

        data, base, image_size, secs = load_sections(EXE)
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(IMAGE_BASE, (image_size + 0xFFF) & ~0xFFF)
        for name, vaddr, vsize, roff, rsize in secs:
            if rsize:
                uc.mem_write(IMAGE_BASE + vaddr, data[roff:roff + rsize])
        uc.mem_map(STACK_BASE, STACK_SIZE)
        uc.mem_map(SCRATCH, SCRATCH_SIZE)
        uc.mem_map(MAGIC_RET & ~0xFFF, 0x1000)
        uc.reg_write(UC_X86_REG_FPCW, 0x0E7F)
        self.uc = uc
        self.w, self.h = w, h
        self.side = w + h + 1
        self.brk = SCRATCH
        self.painted = []
        self.rng_calls = 0

        self.cellptr = self._bump(0x40000 * 4)
        self.work = self._bump(self.side * self.side * WORK_SIZE)
        for (x, y) in self._diamond():
            c = self._bump(CELL_SIZE)
            self.wr32(self.cellptr + ((y << 9) + x) * 4, c)
            self.wr32(c + 0x24, (y << 16) | x)

        self.wr32(0x87F7E8 + 0xEC, 0)
        self.wr32(0x87F7E8 + 0xF0, 0)
        self.wr32(0x87F7E8 + 0xF4, w)
        self.wr32(0x87F7E8 + 0xF8, h)
        self.wr32(0x87F7E8 + 0x13C, self.cellptr)
        self.wr32(0x89C2DC, self.side)
        self.wr32(0xABED04, w)
        self.wr32(0xABED08, w + 2 * h)
        self.wr32(0xABED10, self.work)
        adj = [(0, -1), (1, -1), (1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1)]
        for i, (dx, dy) in enumerate(adj):
            uc.mem_write(0x89F688 + i * 4, struct.pack("<hh", dx, dy))
        for off in (0x00, 0x04, 0x08, 0x0C):
            self.wr32(0xABDFB8 + off, 0)
        self.wr32(0xABDFB8 + 0x10, 0x598000)
        self._call(A_RNGCTOR, 0xABE890, (seed,))

        uc.hook_add(UC_HOOK_CODE, self._h_malloc, begin=A_MALLOC, end=A_MALLOC)
        uc.hook_add(UC_HOOK_CODE, self._h_free, begin=A_FREE, end=A_FREE)
        # 句柄留着：区域阶段的 oracle 要把这个桩摘掉——0x4865D0 同时也是
        # 「这一格是水或岸」，区域切分靠它起种，不能恒真
        self.h_paintable = uc.hook_add(UC_HOOK_CODE, self._h_true0,
                                       begin=A_PAINTABLE, end=A_PAINTABLE)
        # 句柄留着：岸线自己的 oracle 要把这个桩摘掉，好让真身跑起来
        self.h_smooth = uc.hook_add(UC_HOOK_CODE, self._h_true8,
                                    begin=A_SMOOTH, end=A_SMOOTH)
        uc.hook_add(UC_HOOK_CODE, self._h_rng, begin=A_RANDOM, end=A_RANDOM)
        uc.hook_add(UC_HOOK_CODE, self._h_pmain, begin=A_MAIN_PAINT, end=A_MAIN_PAINT)
        uc.hook_add(UC_HOOK_CODE, self._h_pdrain, begin=A_DRAIN_PAINT, end=A_DRAIN_PAINT)

    def _bump(self, n):
        p = self.brk
        self.brk = (self.brk + n + 15) & ~15
        assert self.brk < SCRATCH + SCRATCH_SIZE, "scratch exhausted"
        self.uc.mem_write(p, b"\0" * n)
        return p

    def wr32(self, a, v):
        self.uc.mem_write(a, struct.pack("<I", v & 0xFFFFFFFF))

    def rd32(self, a):
        return struct.unpack("<I", self.uc.mem_read(a, 4))[0]

    def _diamond(self):
        w, h = self.w, self.h
        out = []
        for y in range(0, w + 2 * h + 2):
            for x in range(0, w + 2 * h + 2):
                if x + y > w and x - y < w and y - x < w and x + y <= w + 2 * h:
                    out.append((x, y))
        return out

    def _ret(self, uc, pop):
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EIP
        esp = uc.reg_read(UC_X86_REG_ESP)
        uc.reg_write(UC_X86_REG_EIP, self.rd32(esp))
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + pop)

    def _h_malloc(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX
        n = self.rd32(uc.reg_read(UC_X86_REG_ESP) + 4)
        uc.reg_write(UC_X86_REG_EAX, self._bump(n))
        self._ret(uc, 0)

    def _h_free(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_EAX
        uc.reg_write(UC_X86_REG_EAX, 0)
        self._ret(uc, 0)

    def _h_true0(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_EAX
        uc.reg_write(UC_X86_REG_EAX, 1)
        self._ret(uc, 0)

    def _h_true8(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_EAX
        uc.reg_write(UC_X86_REG_EAX, 1)
        self._ret(uc, 8)

    def _h_rng(self, uc, address, size, ud):
        self.rng_calls += 1

    def _h_pmain(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_EAX
        self._log(uc.reg_read(UC_X86_REG_EAX))

    def _h_pdrain(self, uc, address, size, ud):
        from unicorn.x86_const import UC_X86_REG_EBX
        self._log(uc.reg_read(UC_X86_REG_EBX))

    def _log(self, cellptr):
        v = self.rd32(cellptr + 0x24)
        self.painted.append((v & 0xFFFF, (v >> 16) & 0xFFFF))

    def _call(self, addr, ecx, args=()):
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_EAX
        sp = STACK_BASE + STACK_SIZE - 0x4000
        for v in reversed(args):
            sp -= 4
            self.wr32(sp, v)
        sp -= 4
        self.wr32(sp, MAGIC_RET)
        self.uc.reg_write(UC_X86_REG_ESP, sp)
        self.uc.reg_write(UC_X86_REG_ECX, ecx)
        self.uc.emu_start(addr, MAGIC_RET)
        return self.uc.reg_read(UC_X86_REG_EAX)

    def paint(self, budget, region, start, ellipse, target, sigma, directional,
              full_bounds=None, stamp=1, level=4):
        seedobj = self._bump(0x400)
        self.wr32(seedobj + 0x308, stamp)
        self.wr32(seedobj + 0x30C, level)
        fb = full_bounds if full_bounds else (-1, -1, -1, -1)
        for i, v in enumerate(fb):
            self.wr32(0xABE2F0 + i * 4, v)
        pr = self._bump(16)
        for i, v in enumerate(region):
            self.wr32(pr + i * 4, v)
        # ⚠️ 起点是 int[2]（0x59BC72 mov eax,[ecx] / 0x59BC7C cmp [ecx+4]），
        # 而目标点是打包 int16（0x59B989 movsx word [eax+2]）——两者宽度不同
        ps = self._bump(8)
        self.uc.mem_write(ps, struct.pack("<ii", start[0], start[1]))
        pt = self._bump(8)
        self.uc.mem_write(pt, struct.pack("<hh", target[0], target[1]))
        slo, shi = struct.unpack("<II", struct.pack("<d", sigma))
        self.painted = []
        self.rng_calls = 0
        ret = self._call(A_PAINT, seedobj,
                         (budget, pr, ps, 1 if ellipse else 0, pt, slo, shi,
                          1 if directional else 0))
        return ret, list(self.painted), self.rng_calls, self.rd32(seedobj + 0x308)


CASES = [
    # 区域 == 全图边界：InRegion 走快捷路径恒真，是 MapType 投放最常见的一路
    (70, 70, 0x12345678, 400, (0, 0, 70, 70), (0, 0), 0, (0, 0), 0.75, 0, (0, 0, 70, 70)),
    (70, 70, 0x0BADF00D, 400, (0, 0, 70, 70), (60, 60), 0, (0, 0), 0.25, 0, (0, 0, 70, 70)),
    # 椭圆：这一支走等距逆变换回真实坐标，区域按真实坐标给
    (70, 70, 0x00000001, 400, (10, 10, 40, 40), (0, 0), 1, (0, 0), 0.75, 0, None),
    # 矩形：⚠️ 这一支拿等距坐标直接比，所以区域要按等距范围给（X∈(W,W+2H]）
    (70, 70, 0xDEADBEEF, 400, (60, 40, 60, 60), (0, 0), 0, (0, 0), 0.25, 0, None),
    # 方向性代价（优先级模式 ≠ 0）
    # 起点写死在区域内：随机起点常落到区域外，那时四个邻格全被 InRegion 拒掉，
    # 只涂出一格就收工——那是原版真实行为，但对拍价值低
    (70, 70, 0x5A5A5A5A, 600, (60, 40, 60, 60), (100, 60), 0, (110, 90), 0.75, 1, None),
    # 预算小、要走排空收尾；另一种尺寸
    (80, 60, 0x00C0FFEE, 60, (0, 0, 80, 60), (0, 0), 0, (0, 0), 0.25, 0, (0, 0, 80, 60)),
    # ---- 扩样：多种尺寸/形状/种子，覆盖排空收尾与两种优先级模式 ----
    (70, 70, 0x00000000, 400, (0, 0, 70, 70), (0, 0), 0, (0, 0), 0.75, 0, (0, 0, 70, 70)),
    (70, 70, 0xFFFFFFFF, 400, (0, 0, 70, 70), (0, 0), 0, (0, 0), 0.25, 0, (0, 0, 70, 70)),
    (120, 120, 0x13579BDF, 400, (0, 0, 120, 120), (0, 0), 0, (0, 0), 0.75, 0, (0, 0, 120, 120)),
    (60, 60, 0x2468ACE0, 400, (0, 0, 60, 60), (0, 0), 0, (0, 0), 0.25, 0, (0, 0, 60, 60)),
    # 椭圆 + 方向性代价同时开
    (70, 70, 0x0F0F0F0F, 500, (5, 5, 50, 50), (0, 0), 1, (100, 70), 0.75, 1, None),
    # 很扁的椭圆：各向异性系数取到较大值
    (70, 70, 0xABCDEF01, 400, (10, 30, 50, 8), (0, 0), 1, (0, 0), 0.25, 0, None),
    # 起点贴边：邻格大量落在菱形外
    (70, 70, 0x77777777, 400, (0, 0, 70, 70), (71, 1), 0, (0, 0), 0.75, 0, (0, 0, 70, 70)),
    # 大预算：把堆推深
    (70, 70, 0x1A2B3C4D, 3000, (0, 0, 70, 70), (0, 0), 0, (0, 0), 0.25, 0, (0, 0, 70, 70)),
]


def main():
    out = []
    for (w, h, seed, budget, region, start, ell, tgt, sigma, direc, fb) in CASES:
        o = PaintOracle(w, h, seed)
        ret, painted, calls, stamp = o.paint(budget, region, start, bool(ell),
                                             tgt, sigma, bool(direc), full_bounds=fb)
        fbs = "%d,%d,%d,%d" % fb if fb else "none"
        out.append("CASE %d %d 0x%08X %d %d,%d,%d,%d %d,%d %d %d,%d %r %d %s"
                   " -> ret=%d cells=%d rng=%d stamp=%d" %
                   (w, h, seed, budget, region[0], region[1], region[2], region[3],
                    start[0], start[1], ell, tgt[0], tgt[1], sigma, direc, fbs,
                    ret, len(painted), calls, stamp))
        out.append("  ORDER " + " ".join("%d:%d" % p for p in painted))
        print(out[-2])
        sys.stdout.flush()
    open("../../memory/data/rmg/blob_paint_golden.txt", "w", encoding="utf-8").write(
        u"# \u6d82\u5757\u6838\u5fc3 0x59BBC0 \u6574\u51fd\u6570\u6a21\u62df\u6267\u884c\u57fa\u51c6\n"
        u"# \u751f\u6210\u811a\u672c code/randmap/dump_blob_paint_golden.py\n"
        u"# ORDER \u884c\u6309\u987a\u5e8f\u5217\u51fa\u6bcf\u4e2a\u88ab\u6d82\u6210\u9646\u5730\u7684\u683c\u5b50 X:Y\n"
        + "\n".join(out) + "\n")


if __name__ == "__main__":
    main()
