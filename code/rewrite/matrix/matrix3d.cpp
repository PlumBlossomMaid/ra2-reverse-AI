#include "matrix3d.h"

#include "../rmg/scaled_random.h"   // FpuChopScope, ChopFloat
#include "../rmg/fast_math.h"       // Sin / Cos（原版查表）

namespace ra2::matrix {
namespace {

using ra2::rmg::ChopFloat;
using ra2::rmg::Cos;
using ra2::rmg::Sin;

// 原版 x87 `fstp dword`（RC=chop）窄化到 float32 的复刻。查表 Sin/Cos 返回的
// 本就是 float 位形（无损扩展成 double），窄化通常无数值影响，但写回矩阵的
// 中间结果是真的要向零截断的，必须 ChopFloat。
inline float Store(float v) { return v; }  // 由调用方决定窄化路径

}  // namespace

void Identity(Matrix3D& mat) {
  // 0x5AE860：mov edx,0x3F800000 / xor eax / 对角 {0,5,10}=1，其余 0。
  const float one = 1.0f, zero = 0.0f;
  mat.m[0] = one; mat.m[1] = zero; mat.m[2] = zero; mat.m[3] = zero;
  mat.m[4] = zero; mat.m[5] = one; mat.m[6] = zero; mat.m[7] = zero;
  mat.m[8] = zero; mat.m[9] = zero; mat.m[10] = one; mat.m[11] = zero;
}

void Construct(Matrix3D& mat, float arg1, float arg2) {
  // 0x5AE6F0：单位阵 → RotateZ(arg1) → RotateX(arg2) → RotateZ(-arg1)。
  // 三次调用内部各自重新取 sin/cos（栈局部窄化），照抄顺序。
  Identity(mat);
  RotateZ(mat, arg1);
  RotateX(mat, arg2);
  RotateZ(mat, -arg1);
}

void Scale(Matrix3D& mat, float s) {
  // 0x5AEA10：只乘旋转 3×3（列主序 {0,4,8 | 1,5,9 | 2,6,10}），平移列不动。
  const double d = static_cast<double>(s);
  mat.m[0] = ChopFloat(d * static_cast<double>(mat.m[0]));
  mat.m[4] = ChopFloat(d * static_cast<double>(mat.m[4]));
  mat.m[8] = ChopFloat(d * static_cast<double>(mat.m[8]));
  mat.m[1] = ChopFloat(d * static_cast<double>(mat.m[1]));
  mat.m[5] = ChopFloat(d * static_cast<double>(mat.m[5]));
  mat.m[9] = ChopFloat(d * static_cast<double>(mat.m[9]));
  mat.m[2] = ChopFloat(d * static_cast<double>(mat.m[2]));
  mat.m[6] = ChopFloat(d * static_cast<double>(mat.m[6]));
  mat.m[10] = ChopFloat(d * static_cast<double>(mat.m[10]));
}

void TranslateX(Matrix3D& mat, float d) {
  // 0x5AE980：m[3] 列 += d × X 列（m[0],m[4],m[8]）。每对为独立
  // `fld d; fmul col; fadd trans; fstp trans` = (d*col) + trans 一次加。
  const double dv = static_cast<double>(d);
  mat.m[3] = ChopFloat(dv * static_cast<double>(mat.m[0]) + mat.m[3]);
  mat.m[7] = ChopFloat(dv * static_cast<double>(mat.m[4]) + mat.m[7]);
  mat.m[11] = ChopFloat(dv * static_cast<double>(mat.m[8]) + mat.m[11]);
}

void TranslateY(Matrix3D& mat, float d) {
  const double dv = static_cast<double>(d);
  mat.m[3] = ChopFloat(dv * static_cast<double>(mat.m[1]) + mat.m[3]);
  mat.m[7] = ChopFloat(dv * static_cast<double>(mat.m[5]) + mat.m[7]);
  mat.m[11] = ChopFloat(dv * static_cast<double>(mat.m[9]) + mat.m[11]);
}

void TranslateZ(Matrix3D& mat, float d) {
  const double dv = static_cast<double>(d);
  mat.m[3] = ChopFloat(dv * static_cast<double>(mat.m[2]) + mat.m[3]);
  mat.m[7] = ChopFloat(dv * static_cast<double>(mat.m[6]) + mat.m[7]);
  mat.m[11] = ChopFloat(dv * static_cast<double>(mat.m[10]) + mat.m[11]);
}

void RotateX(Matrix3D& mat, float angle) {
  // 0x5AEF60：sin 先（fstp dword 窄化），cos 留栈（53 位）。
  const double c = Cos(static_cast<double>(angle));
  const double s = static_cast<double>(ChopFloat(Sin(static_cast<double>(angle))));
  // 三行 (Y,Z) 对，X 分量不动，平移列不动。
  for (int row = 0; row < 3; ++row) {
    const int o = row * 4;
    const double y = mat.m[o + 1];
    const double z = mat.m[o + 2];
    // Y' = Y·cos + Z·sin（顺序：Y·cos 先）；Z' = Z·cos − Y·sin（Z·cos 先）
    mat.m[o + 1] = ChopFloat(y * c + z * s);
    mat.m[o + 2] = ChopFloat(z * c - y * s);
  }
}

void RotateY(Matrix3D& mat, float angle) {
  // 0x5AF080：sin 先窄化，cos 留栈。
  const double c = Cos(static_cast<double>(angle));
  const double s = static_cast<double>(ChopFloat(Sin(static_cast<double>(angle))));
  for (int row = 0; row < 3; ++row) {
    const int o = row * 4;
    const double x = mat.m[o];
    const double z = mat.m[o + 2];
    // X' = X·cos − Z·sin（X·cos 先）；Z' = Z·cos + X·sin（Z·cos 先）
    mat.m[o] = ChopFloat(x * c - z * s);
    mat.m[o + 2] = ChopFloat(z * c + x * s);
  }
}

void RotateZ(Matrix3D& mat, float angle) {
  // 0x5AF1A0：**与 RotateX/Y 相反——cos 先被窄化、sin 留栈。**
  const double s = Sin(static_cast<double>(angle));
  const double c = static_cast<double>(ChopFloat(Cos(static_cast<double>(angle))));
  for (int row = 0; row < 3; ++row) {
    const int o = row * 4;
    const double x = mat.m[o];
    const double y = mat.m[o + 1];
    // X' = Y·sin + X·cos（顺序：Y·sin 先！）；Y' = Y·cos − X·sin（Y·cos 先）
    mat.m[o] = ChopFloat(y * s + x * c);
    mat.m[o + 1] = ChopFloat(y * c - x * s);
  }
}

void RotateVector(const Matrix3D& mat, const float in[3], float out[3]) {
  // 0x5AF4D0：行主序点积，三行累加顺序各不相同（不对称）。平移列不参与。
  //   out0 = (m1·v1 + m2·v2) + m0·v0
  //   out1 = (m5·v1 + m4·v0) + m6·v2
  //   out2 = (m9·v1 + m8·v0) + m10·v2
  const double v0 = in[0], v1 = in[1], v2 = in[2];
  double t = mat.m[1] * v1 + mat.m[2] * v2;
  out[0] = ChopFloat(t + mat.m[0] * v0);
  t = mat.m[5] * v1 + mat.m[4] * v0;
  out[1] = ChopFloat(t + mat.m[6] * v2);
  t = mat.m[9] * v1 + mat.m[8] * v0;
  out[2] = ChopFloat(t + mat.m[10] * v2);
}

}  // namespace ra2::matrix