# -*- coding: utf-8 -*-
u"""导出 Matrix3D 族（4×3 = 12 floats 行主序矩阵操作）的对拍基准。

覆盖 8 个函数，全部是纯 IEEE 运算（fld/fmul/fadd/fsub/fstp + 查表 sin/cos），
Unicorn 在其已知可信面内（不涉及 fyl2x/fsin 超越指令）。

  Identity       @ 0x5AE860  ret 0   无参，写对角线 1.0
  Matrix3D ctor  @ 0x5AE6F0  ret 8   参数1=Z 角 / 参数2=X 角；行为 =
                                      单位阵 → RotateZ(p1) → RotateX(p2) → RotateZ(-p1)
  Scale          @ 0x5AEA10  ret 4   9 个旋转元素（跳过平移列 +0xC/+0x1C/+0x2C）乘标量
  TranslateX     @ 0x5AE980  ret 4   平移列 += d × 旋转列 X（每行取 +0/+0x10/+0x20）
  TranslateY     @ 0x5AE9B0  ret 4   平移列 += d × 旋转列 Y（+4/+0x14/+0x24）
  TranslateZ     @ 0x5AE9E0  ret 4   平移列 += d × 旋转列 Z（+8/+0x18/+0x28）
  RotateX        @ 0x5AEF60  ret 4   对每行 (Y,Z)：
                                      Y' = Y·cos + Z·sin；Z' = Z·cos − Y·sin
  RotateY        @ 0x5AF080  ret 4   对每行 (X,Z)：X' = X·cos − Z·sin；Z' = X·sin + Z·cos
  RotateZ        @ 0x5AF1A0  ret 4   对每行 (X,Y)：X' = X·cos + Y·sin；Y' = Y·cos − X·sin
  RotateVector   @ 0x5AF4D0  ret 8   out = 3×3 旋转部分 × in（三行点积，
                                      各行累加顺序不对称——照抄）

本脚本只输出 .inc 数组（16 进制 float 位形），测试逐位比较。
"""
import io
import math
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu_oracle import Oracle

ADDR_IDENT = 0x5AE860
ADDR_CTOR = 0x5AE6F0
ADDR_SCALE = 0x5AEA10
ADDR_TRX = 0x5AE980
ADDR_TRY = 0x5AE9B0
ADDR_TRZ = 0x5AE9E0
ADDR_ROTX = 0x5AEF60
ADDR_ROTY = 0x5AF080
ADDR_ROTZ = 0x5AF1A0
ADDR_ROTVEC = 0x5AF4D0

MAT = 0x30000000          # 矩阵 buffer（12 floats = 0x30）
VEC = 0x30000400          # RotateVector 源向量
OUT = 0x30000800          # RotateVector 目标向量
FPCW = 0x0E7F             # 原版 WinMain 缓存值：PC=53 + RC=chop


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def ashex(x):
    return struct.unpack("<I", struct.pack("<f", f32(x)))[0] & 0xFFFFFFFF


def mkvec(num):
    """确定性伪随机 float 输入矩阵 + 参数值。"""
    rng = random.Random(num)
    out = []
    for _ in range(12):
        # 跨度覆盖：正常值 / 小值 / 符号翻转 / 大值，避免全零或 NaN
        out.append(f32(rng.uniform(-0.9, 0.9) * (10 ** rng.uniform(-2, 1))))
    return out


def nvec():
    return [0.0] * 12


class M:
    def __init__(self):
        self.o = Oracle()
        self.uc = self.o.uc
        from unicorn.x86_const import (UC_X86_REG_ECX, UC_X86_REG_ESP,
                                       UC_X86_REG_FPCW)
        self.REG_ECX, self.REG_ESP, self.REG_FPCW = (UC_X86_REG_ECX,
                                                     UC_X86_REG_ESP,
                                                     UC_X86_REG_FPCW)
        self.sp0 = 0x20000000 + 0x100000 - 0x2000

    def call1(self, addr, mat, p):
        """ret 4 单参 thiscall：ECX=this，参数压栈，FPU 设为原版环境。"""
        sp = self.sp0 - 4
        self.uc.mem_write(sp, struct.pack("<f", f32(p)))
        self.uc.mem_write(MAT, struct.pack("<12f", *mat))
        sp -= 4
        self.uc.mem_write(sp, struct.pack("<I", 0x1F000000))
        self.uc.reg_write(self.REG_ESP, sp)
        self.uc.reg_write(self.REG_ECX, MAT)
        self.uc.reg_write(self.REG_FPCW, FPCW)
        self.uc.emu_start(addr, 0x1F000000)
        return list(struct.unpack("<12f", bytes(self.uc.mem_read(MAT, 0x30))))

    def call2(self, addr, mat, p1, p2):
        """ret 8 双参 thiscall。"""
        sp = self.sp0 - 4
        self.uc.mem_write(sp, struct.pack("<f", f32(p2)))
        sp -= 4
        self.uc.mem_write(sp, struct.pack("<f", f32(p1)))
        self.uc.mem_write(MAT, struct.pack("<12f", *mat))
        sp -= 4
        self.uc.mem_write(sp, struct.pack("<I", 0x1F000000))
        self.uc.reg_write(self.REG_ESP, sp)
        self.uc.reg_write(self.REG_ECX, MAT)
        self.uc.reg_write(self.REG_FPCW, FPCW)
        self.uc.emu_start(addr, 0x1F000000)
        return list(struct.unpack("<12f", bytes(self.uc.mem_read(MAT, 0x30))))

    def call_ident(self, mat=None):
        sp = self.sp0 - 4
        self.uc.mem_write(sp, struct.pack("<I", 0x1F000000))
        if mat is not None:
            self.uc.mem_write(MAT, struct.pack("<12f", *mat))
        self.uc.reg_write(self.REG_ESP, sp)
        self.uc.reg_write(self.REG_ECX, MAT)
        self.uc.reg_write(self.REG_FPCW, FPCW)
        self.uc.emu_start(ADDR_IDENT, 0x1F000000)
        return list(struct.unpack("<12f", bytes(self.uc.mem_read(MAT, 0x30))))

    def call_rotvec(self, mat, vin):
        """RotateVector：this=矩阵，参数1=out 指针，参数2=in 指针（ret 8）。"""
        self.uc.mem_write(MAT, struct.pack("<12f", *mat))
        self.uc.mem_write(VEC, struct.pack("<3f", *vin))
        sp = self.sp0
        sp -= 4
        self.uc.mem_write(sp, struct.pack("<I", VEC))     # 参数2 = in
        sp -= 4
        self.uc.mem_write(sp, struct.pack("<I", OUT))     # 参数1 = out
        sp -= 4
        self.uc.mem_write(sp, struct.pack("<I", 0x1F000000))
        self.uc.reg_write(self.REG_ESP, sp)
        self.uc.reg_write(self.REG_ECX, MAT)
        self.uc.reg_write(self.REG_FPCW, FPCW)
        self.uc.emu_start(ADDR_ROTVEC, 0x1F000000)
        return list(struct.unpack("<3f", bytes(self.uc.mem_read(OUT, 0xC))))


def emit(buf, name, rows, npar, npout):
    """rows: list of (params list, in matrix(12), out)"""
    buf.write(u"// %s —— in[12] | param[%d] | out[%d]，全部 uint32 float 位形\n"
              % (name, npar, npout))
    header = name + "  [][12 + %d + %d] = {\n" % (npar, npout)
    buf.write(u"static const std::uint32_t " + header)
    for r in rows:
        params, mat, out = r
        pl = [ashex(x) for x in params]
        il = [ashex(x) for x in mat]
        ol = npout
        if npout == 12:
            ol = [ashex(x) for x in out]
        elif npout == 3:
            ol = [ashex(x) for x in out]
        a = "0x%08X"
        buf.write(u"    {" + ", ".join(a % v for v in (il + pl + ol)) + u"},\n")
    buf.write(u"};\n\n")


def main():
    m = M()
    buf = io.StringIO()
    buf.write(u"// 由 code/randmap/dump_matrix_golden.py 生成——勿手改。\n")
    buf.write(u"// 所有值都是 float 位形（uint32）。用例索引与输入集固定。\n\n")

    # 统一输入集：确定性伪随机 + 特殊值
    seeds = [0, 1, 2, 3]
    mats = [mkvec(s) for s in seeds]
    # 旋转/缩放用特殊整数角度序列
    angles = [0.0, 1.0, math.pi / 4, math.pi / 2, math.pi, 2 * math.pi,
              -math.pi / 6, 0.3, -0.3, 100.0, -100.0, 1e-6]
    vecs = [(1.0, 2.0, 3.0), (-4.0, 0.5, 7.0), (0.0, 0.0, 0.0),
            (1e-4, -1e4, 3.0), (0.001, -0.002, 0.003)]

    rows_rotx, rows_roty, rows_rotz = [], [], []
    rows_scale, rows_trx, rows_try, rows_trz = [], [], [], []
    rows_ctor, rows_ident, rows_vec = [], [], []

    for mat in mats:
        for a in angles:
            rows_rotx.append(([a], mat, m.call1(ADDR_ROTX, mat, a)))
            rows_roty.append(([a], mat, m.call1(ADDR_ROTY, mat, a)))
            rows_rotz.append(([a], mat, m.call1(ADDR_ROTZ, mat, a)))
        for s in (1.0, -1.0, 0.0, 2.5, 0.001, -1000.0):
            rows_scale.append(([s], mat, m.call1(ADDR_SCALE, mat, s)))
        for d in (1.0, -1.0, 0.0, 0.5, 100.0):
            rows_trx.append(([d], mat, m.call1(ADDR_TRX, mat, d)))
            rows_try.append(([d], mat, m.call1(ADDR_TRY, mat, d)))
            rows_trz.append(([d], mat, m.call1(ADDR_TRZ, mat, d)))
        for zang, xang in ((0.0, 0.0), (math.pi / 2, 0.0), (0.0, math.pi / 2),
                           (0.3, -0.7), (math.pi, 1.0)):
            rows_ctor.append(([zang, xang], mat,
                              m.call2(ADDR_CTOR, mat, zang, xang)))
        # Identity 无参数，但保持统一 in[12] 布局（out 从第 12 格开始），
        # 与其它表一致，测试可统一按 [12+np+no] 解析。
        rows_ident.append(([], mat, m.call_ident(mat)))
        for v in vecs:
            rows_vec.append((list(v), mat, m.call_rotvec(mat, v)))

    emit(buf, u"kGoldenIdent", rows_ident, 0, 12)
    emit(buf, u"kGoldenCtor", rows_ctor, 2, 12)
    emit(buf, u"kGoldenScale", rows_scale, 1, 12)
    emit(buf, u"kGoldenTrX", rows_trx, 1, 12)
    emit(buf, u"kGoldenTrY", rows_try, 1, 12)
    emit(buf, u"kGoldenTrZ", rows_trz, 1, 12)
    emit(buf, u"kGoldenRotX", rows_rotx, 1, 12)
    emit(buf, u"kGoldenRotY", rows_roty, 1, 12)
    emit(buf, u"kGoldenRotZ", rows_rotz, 1, 12)
    emit(buf, u"kGoldenRotVec", rows_vec, 3, 3)

    out = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "..", "..", "memory", "data", "rmg", "matrix3d_golden.inc")
    out = os.path.normpath(out)
    with io.open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(buf.getvalue())
    print("written: " + out)
    print("rows: rotx=%d roty=%d rotz=%d scale=%d tr=%d/%d/%d ctor=%d ident=%d vec=%d"
          % (len(rows_rotx), len(rows_roty), len(rows_rotz), len(rows_scale),
             len(rows_trx), len(rows_try), len(rows_trz), len(rows_ctor),
             len(rows_ident), len(rows_vec)))


if __name__ == "__main__":
    main()