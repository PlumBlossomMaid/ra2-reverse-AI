// 够用就好的无符号大整数：只为 .mix 头那两次 RSA 公钥运算服务。
//
// 模数 319 位、指数 65537，一张 mix 只算两块，性能完全无所谓，
// 所以取模用最朴素的移位-相减长除法，不引入任何第三方大数库。
#ifndef RA2_MIX_BIGNUM_H_
#define RA2_MIX_BIGNUM_H_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ra2 {
namespace mix {

class BigUint {
 public:
  BigUint() {}
  static BigUint FromBytesBE(const std::uint8_t* p, std::size_t n);
  static BigUint FromBytesLE(const std::uint8_t* p, std::size_t n);

  // 低位在前写出 n 字节；高于 n 字节的部分被丢弃（调用方保证放得下）。
  void ToBytesLE(std::uint8_t* out, std::size_t n) const;

  int BitLength() const;
  bool IsZero() const { return limbs_.empty(); }
  bool Bit(int i) const;

  // this * rhs
  BigUint Mul(const BigUint& rhs) const;
  // this mod m（m 不为 0）
  BigUint Mod(const BigUint& m) const;
  // 比较：<0 / 0 / >0
  int Cmp(const BigUint& rhs) const;

  // pow(this, e, m)
  BigUint ModPow(std::uint32_t e, const BigUint& m) const;

 private:
  void Trim();
  void ShiftLeft1();
  void SubInPlace(const BigUint& rhs);   // 要求 *this >= rhs

  std::vector<std::uint32_t> limbs_;     // 低位在前
};

}  // namespace mix
}  // namespace ra2

#endif  // RA2_MIX_BIGNUM_H_
