// 测试 FastSqrt 的 double->float 转换是否受 _controlfp 影响
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <float.h>

int main() {
  // 一个会在 double->float 截断时产生差异的值
  // 0.1 的 double 表示比 0.1 大一点：0x3FB999999999999A
  // 截断到 float（23 位尾数）：
  //   就近舍入: 0x3DCCCCCD (比 0.1 大)
  //   向零截断: 0x3DCCCCCC (比 0.1 小)
  double x = 0.1;
  unsigned int saved = _controlfp(0, 0);
  
  _controlfp(_RC_NEAR, _MCW_RC);
  float f_near = static_cast<float>(x);
  uint32_t bits_near;
  memcpy(&bits_near, &f_near, sizeof(bits_near));
  
  _controlfp(_RC_CHOP, _MCW_RC);
  float f_chop = static_cast<float>(x);
  uint32_t bits_chop;
  memcpy(&bits_chop, &f_chop, sizeof(bits_chop));
  
  _controlfp(saved, _MCW_RC);
  
  printf("0.1 as double: 0x%016llX\n", *(uint64_t*)&x);
  printf("static_cast<float>(0.1) near: 0x%08X\n", bits_near);
  printf("static_cast<float>(0.1) chop: 0x%08X\n", bits_chop);
  printf("diff: %s\n", bits_near != bits_chop ? "YES" : "NO");
  
  if (bits_near != bits_chop) {
    printf("✓ _controlfp 影响 cvtsd2ss → FastSqrt 在 chop 模式下正确\n");
  } else {
    printf("✗ _controlfp 不影响 cvtsd2ss → FastSqrt 可能用就近舍入！\n");
  }
  
  return 0;
}