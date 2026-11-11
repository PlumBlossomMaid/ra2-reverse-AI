// Matrix3D 族（0x5AExxx）——4×3 行主序矩阵（12 floats：行 = {X,Y,Z,平移}）运算复刻
//
// 原版布局（反汇编偏移实证）：
//   +0x00 row0.X, +0x04 row0.Y, +0x08 row0.Z, +0x0C row0.平移
//   +0x10 row1.X, +0x14 row1.Y, +0x18 row1.Z, +0x1C row1.平移
//   +0x20 row2.X, +0x24 row2.Y, +0x28 row2.Z, +0x2C row2.平移
//   （与 YRpp Matrix3DStruct::Data[12] 一致）
//
// 数字纪律：
//   * FPCW=0x0E7F（PC=53 位 + RC=chop）——x87 栈上中间运算按 53 位双精度
//     向零截断，与 double 在 FpuChopScope 下的运算逐位一致；
//   * fstp dword 存回是向零截断的 float 窄化，用 ChopFloat 复刻；
//   * 旋转的正弦/余弦来自原版查表（0x4CACB0/0x4CAD00，ra2::rmg::Sin/Cos），
//     不是 libm；
//   * 每处的乘加**顺序**都照抄 x87 栈序：RC=chop 下 (a*b)+(c*d) 与
//     (c*d)+(a*b) 末位可能不同，顺序即语义。
//
// 已证怪癖（照抄）：
//   * RotateX/RotateY 先调 Sin（结果先 fstp dword 窄化）后调 Cos（留在 x87
//     栈，不窄化）；RotateZ 恰好相反——cos 窄化、sin 留栈。查表返回本就是
//     float 位形，此窄化对数值无影响，但调用顺序即语义（golden 实证）。
//   * RotateVector 三行点积的累加顺序各不相同且不对称（行 1：先 m[1]*v[1]
//     再 m[2]*v[2] 再 m[0]*v[0]；行 2/行 3：先 m5*v1 再 m4*v0 再 m6*v2），
//     照抄后与 Unicorn 逐位一致。
#pragma once

#include <cstdint>

namespace ra2::matrix {

// 4×3 行主序矩阵。内存布局与原版 Matrix3DStruct 完全一致。
struct Matrix3D {
  float m[12];
};

// 0x5AE860（ret 0，无参）：单位化。对角线（+0/+0x14/+0x28）= 1.0f，其余 0。
void Identity(Matrix3D& mat);

// 0x5AE6F0（ret 8，双参）：Matrix3D(arg1, arg2) 构造器。
//   单位阵 → RotateZ(arg1) → RotateX(arg2) → RotateZ(-arg1)
//   （反汇编：0x5AE721 push 参数1 → call 0x5AF1A0；0x5AE72D push 参数2 →
//    call 0x5AEF60；0x5AE735 fld/fchs 参数1 → call 0x5AF1A0）
void Construct(Matrix3D& mat, float arg1, float arg2);

// 0x5AEA10（ret 4）：9 个旋转元素（跳过平移列 +0xC/+0x1C/+0x2C）乘标量。
void Scale(Matrix3D& mat, float s);

// 0x5AE980 / 0x5AE9B0 / 0x5AE9E0（ret 4）：平移列 += d × 旋转列。
//   每个 (col, trans) 对都是独立的 `(d*m[col]) + m[trans]` 一次加，三对顺序照抄。
void TranslateX(Matrix3D& mat, float d);  // trans += d * row.X（+0/+0x10/+0x20）
void TranslateY(Matrix3D& mat, float d);  // trans += d * row.Y（+4/+0x14/+0x24）
void TranslateZ(Matrix3D& mat, float d);  // trans += d * row.Z（+8/+0x18/+0x28）

// 0x5AEF60（ret 4）：绕 X 轴旋转。每行 (Y,Z) 对更新，X 分量不动。
//   Y' = Y·cos + Z·sin（顺序：Y·cos 先，+ Z·sin）
//   Z' = Z·cos − Y·sin（顺序：Z·cos 先，− Y·sin）
void RotateX(Matrix3D& mat, float angle);

// 0x5AF080（ret 4）：绕 Y 轴旋转。每行 (X,Z) 对更新，Y 分量不动。
//   X' = X·cos − Z·sin（顺序：X·cos 先，− Z·sin）
//   Z' = Z·cos + X·sin（顺序：Z·cos 先，+ X·sin）
void RotateY(Matrix3D& mat, float angle);

// 0x5AF1A0（ret 4）：绕 Z 轴旋转。每行 (X,Y) 对更新，Z 分量不动。
//   **与 RotateX/Y 相反：cos 先被窄化、sin 留栈。**
//   X' = X·cos + Y·sin（顺序：Y·sin 先，+ X·cos）
//   Y' = Y·cos − X·sin（顺序：Y·cos 先，− X·sin）
void RotateZ(Matrix3D& mat, float angle);

// 0x5AF4D0（ret 8，双参）：3×3 旋转部分 × 向量。
//   out = (v·row0, v·row1, v·row2)（行主序点积），平移列不参与。
//   三个分量点积的累加顺序不对称，照抄（见头注释）。
//   参数 1 = out 指针，参数 2 = in 指针。
void RotateVector(const Matrix3D& mat, const float in[3], float out[3]);

}  // namespace ra2::matrix