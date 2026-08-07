#include "bignum.h"

namespace ra2 {
namespace mix {

void BigUint::Trim() {
  while (!limbs_.empty() && limbs_.back() == 0) limbs_.pop_back();
}

BigUint BigUint::FromBytesLE(const std::uint8_t* p, std::size_t n) {
  BigUint r;
  r.limbs_.assign((n + 3) / 4, 0);
  for (std::size_t i = 0; i < n; ++i) {
    r.limbs_[i / 4] |= static_cast<std::uint32_t>(p[i]) << (8 * (i % 4));
  }
  r.Trim();
  return r;
}

BigUint BigUint::FromBytesBE(const std::uint8_t* p, std::size_t n) {
  BigUint r;
  r.limbs_.assign((n + 3) / 4, 0);
  for (std::size_t i = 0; i < n; ++i) {
    const std::size_t k = n - 1 - i;   // 低位字节在末尾
    r.limbs_[i / 4] |= static_cast<std::uint32_t>(p[k]) << (8 * (i % 4));
  }
  r.Trim();
  return r;
}

void BigUint::ToBytesLE(std::uint8_t* out, std::size_t n) const {
  for (std::size_t i = 0; i < n; ++i) {
    const std::size_t li = i / 4;
    out[i] = li < limbs_.size()
                 ? static_cast<std::uint8_t>(limbs_[li] >> (8 * (i % 4)))
                 : 0;
  }
}

int BigUint::BitLength() const {
  if (limbs_.empty()) return 0;
  int n = static_cast<int>(limbs_.size() - 1) * 32;
  std::uint32_t t = limbs_.back();
  while (t) { ++n; t >>= 1; }
  return n;
}

bool BigUint::Bit(int i) const {
  const std::size_t li = static_cast<std::size_t>(i) / 32;
  if (li >= limbs_.size()) return false;
  return ((limbs_[li] >> (i % 32)) & 1u) != 0;
}

int BigUint::Cmp(const BigUint& rhs) const {
  if (limbs_.size() != rhs.limbs_.size()) {
    return limbs_.size() < rhs.limbs_.size() ? -1 : 1;
  }
  for (std::size_t i = limbs_.size(); i-- > 0;) {
    if (limbs_[i] != rhs.limbs_[i]) return limbs_[i] < rhs.limbs_[i] ? -1 : 1;
  }
  return 0;
}

BigUint BigUint::Mul(const BigUint& rhs) const {
  BigUint r;
  if (limbs_.empty() || rhs.limbs_.empty()) return r;
  r.limbs_.assign(limbs_.size() + rhs.limbs_.size(), 0);
  for (std::size_t i = 0; i < limbs_.size(); ++i) {
    std::uint64_t carry = 0;
    const std::uint64_t a = limbs_[i];
    for (std::size_t j = 0; j < rhs.limbs_.size(); ++j) {
      const std::uint64_t cur =
          static_cast<std::uint64_t>(r.limbs_[i + j]) + a * rhs.limbs_[j] + carry;
      r.limbs_[i + j] = static_cast<std::uint32_t>(cur);
      carry = cur >> 32;
    }
    std::size_t k = i + rhs.limbs_.size();
    while (carry) {
      const std::uint64_t cur = static_cast<std::uint64_t>(r.limbs_[k]) + carry;
      r.limbs_[k] = static_cast<std::uint32_t>(cur);
      carry = cur >> 32;
      ++k;
    }
  }
  r.Trim();
  return r;
}

void BigUint::ShiftLeft1() {
  std::uint32_t carry = 0;
  for (std::size_t i = 0; i < limbs_.size(); ++i) {
    const std::uint32_t nc = limbs_[i] >> 31;
    limbs_[i] = (limbs_[i] << 1) | carry;
    carry = nc;
  }
  if (carry) limbs_.push_back(carry);
}

void BigUint::SubInPlace(const BigUint& rhs) {
  std::int64_t borrow = 0;
  for (std::size_t i = 0; i < limbs_.size(); ++i) {
    std::int64_t cur = static_cast<std::int64_t>(limbs_[i]) - borrow;
    if (i < rhs.limbs_.size()) cur -= static_cast<std::int64_t>(rhs.limbs_[i]);
    if (cur < 0) { cur += (static_cast<std::int64_t>(1) << 32); borrow = 1; }
    else { borrow = 0; }
    limbs_[i] = static_cast<std::uint32_t>(cur);
  }
  Trim();
}

BigUint BigUint::Mod(const BigUint& m) const {
  if (m.IsZero()) return BigUint();
  if (Cmp(m) < 0) return *this;
  BigUint r;
  for (int i = BitLength(); i-- > 0;) {
    r.ShiftLeft1();
    if (Bit(i)) {
      if (r.limbs_.empty()) r.limbs_.push_back(1);
      else r.limbs_[0] |= 1u;
    }
    if (r.Cmp(m) >= 0) r.SubInPlace(m);
  }
  r.Trim();
  return r;
}

BigUint BigUint::ModPow(std::uint32_t e, const BigUint& m) const {
  BigUint result;
  result.limbs_.push_back(1);
  BigUint base = Mod(m);
  while (e) {
    if (e & 1u) result = result.Mul(base).Mod(m);
    e >>= 1;
    if (e) base = base.Mul(base).Mod(m);
  }
  return result;
}

}  // namespace mix
}  // namespace ra2
