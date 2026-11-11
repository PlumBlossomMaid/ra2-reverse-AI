// Matrix3D 族 0x5AExxx —— 与原版 Unicorn 指令级模拟逐位对拍
//
// golden 数据由 code/randmap/dump_matrix_golden.py 生成（原版机器码在
// FPCW=0x0E7F 下模拟），本测试在同一舍入环境下重放并逐位比较 float 位形。
//
// 覆盖：Identity(0x5AE860) / Construct(0x5AE6F0) / Scale(0x5AEA10) /
//       TranslateX/Y/Z(0x5AE980/0x5AE9B0/0x5AE9E0) /
//       RotateX/Y/Z(0x5AEF60/0x5AF080/0x5AF1A0) / RotateVector(0x5AF4D0)
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "matrix3d.h"
#include "scaled_random.h"   // FpuChopScope（PC=53 + RC=chop）

#include "matrix3d_golden.inc"   // memory/data/rmg（CMAKE include path）

namespace {

float F32(uint32_t bits) {
  float v;
  std::memcpy(&v, &bits, sizeof v);
  return v;
}

// golden 行布局参数化：in[12] | param[np] | out[no]
void LoadIn(const std::uint32_t* row, float* m) {
  for (int i = 0; i < 12; ++i) m[i] = F32(row[i]);
}

}  // namespace

TEST(Matrix3D, IdentityMatchesOriginal) {
  ra2::rmg::FpuChopScope fpu;
  for (const auto& row : kGoldenIdent) {
    ra2::matrix::Matrix3D mat;
    mat.m[0] = 123.45f;              // 毒化，证明函数真的写了全 12 个元素
    mat.m[1] = -0.001f;
    mat.m[2] = 7.7f;
    mat.m[3] = 999.0f;
    mat.m[4] = 0.5f;
    mat.m[5] = -2.0f;
    mat.m[6] = 3.25f;
    mat.m[7] = -0.125f;
    mat.m[8] = 6.6f;
    mat.m[9] = 1e30f;
    mat.m[10] = -4.5f;
    mat.m[11] = 0.007f;
    ra2::matrix::Identity(mat);
    for (int i = 0; i < 12; ++i) {
      EXPECT_EQ(mat.m[i], F32(row[12 + i])) << "elem[" << i << "]";
    }
  }
}

TEST(Matrix3D, ConstructMatchesOriginal) {
  ra2::rmg::FpuChopScope fpu;
  for (const auto& row : kGoldenCtor) {
    ra2::matrix::Matrix3D mat;
    LoadIn(row, mat.m);
    ra2::matrix::Construct(mat, F32(row[12]), F32(row[13]));
    for (int i = 0; i < 12; ++i) {
      EXPECT_EQ(mat.m[i], F32(row[12 + 2 + i])) << "elem[" << i << "]";
    }
  }
}

TEST(Matrix3D, ScaleMatchesOriginal) {
  ra2::rmg::FpuChopScope fpu;
  for (const auto& row : kGoldenScale) {
    ra2::matrix::Matrix3D mat;
    LoadIn(row, mat.m);
    ra2::matrix::Scale(mat, F32(row[12]));
    for (int i = 0; i < 12; ++i) {
      EXPECT_EQ(mat.m[i], F32(row[13 + i])) << "elem[" << i << "]";
    }
  }
}

TEST(Matrix3D, TranslateXMatchesOriginal) {
  ra2::rmg::FpuChopScope fpu;
  for (const auto& row : kGoldenTrX) {
    ra2::matrix::Matrix3D mat;
    LoadIn(row, mat.m);
    ra2::matrix::TranslateX(mat, F32(row[12]));
    for (int i = 0; i < 12; ++i) {
      EXPECT_EQ(mat.m[i], F32(row[13 + i])) << "elem[" << i << "]";
    }
  }
}

TEST(Matrix3D, TranslateYMatchesOriginal) {
  ra2::rmg::FpuChopScope fpu;
  for (const auto& row : kGoldenTrY) {
    ra2::matrix::Matrix3D mat;
    LoadIn(row, mat.m);
    ra2::matrix::TranslateY(mat, F32(row[12]));
    for (int i = 0; i < 12; ++i) {
      EXPECT_EQ(mat.m[i], F32(row[13 + i])) << "elem[" << i << "]";
    }
  }
}

TEST(Matrix3D, TranslateZMatchesOriginal) {
  ra2::rmg::FpuChopScope fpu;
  for (const auto& row : kGoldenTrZ) {
    ra2::matrix::Matrix3D mat;
    LoadIn(row, mat.m);
    ra2::matrix::TranslateZ(mat, F32(row[12]));
    for (int i = 0; i < 12; ++i) {
      EXPECT_EQ(mat.m[i], F32(row[13 + i])) << "elem[" << i << "]";
    }
  }
}

TEST(Matrix3D, RotateXMatchesOriginal) {
  ra2::rmg::FpuChopScope fpu;
  for (const auto& row : kGoldenRotX) {
    ra2::matrix::Matrix3D mat;
    LoadIn(row, mat.m);
    ra2::matrix::RotateX(mat, F32(row[12]));
    for (int i = 0; i < 12; ++i) {
      EXPECT_EQ(mat.m[i], F32(row[13 + i])) << "elem[" << i << "]";
    }
  }
}

TEST(Matrix3D, RotateYMatchesOriginal) {
  ra2::rmg::FpuChopScope fpu;
  for (const auto& row : kGoldenRotY) {
    ra2::matrix::Matrix3D mat;
    LoadIn(row, mat.m);
    ra2::matrix::RotateY(mat, F32(row[12]));
    for (int i = 0; i < 12; ++i) {
      EXPECT_EQ(mat.m[i], F32(row[13 + i])) << "elem[" << i << "]";
    }
  }
}

TEST(Matrix3D, RotateZMatchesOriginal) {
  ra2::rmg::FpuChopScope fpu;
  for (const auto& row : kGoldenRotZ) {
    ra2::matrix::Matrix3D mat;
    LoadIn(row, mat.m);
    ra2::matrix::RotateZ(mat, F32(row[12]));
    for (int i = 0; i < 12; ++i) {
      EXPECT_EQ(mat.m[i], F32(row[13 + i])) << "elem[" << i << "]";
    }
  }
}

TEST(Matrix3D, RotateVectorMatchesOriginal) {
  ra2::rmg::FpuChopScope fpu;
  for (const auto& row : kGoldenRotVec) {
    ra2::matrix::Matrix3D mat;
    LoadIn(row, mat.m);
    const float in[3] = {F32(row[12]), F32(row[13]), F32(row[14])};
    float out[3] = {0.0f, 0.0f, 0.0f};
    ra2::matrix::RotateVector(mat, in, out);
    for (int i = 0; i < 3; ++i) {
      EXPECT_EQ(out[i], F32(row[15 + i])) << "out[" << i << "]";
    }
  }
}