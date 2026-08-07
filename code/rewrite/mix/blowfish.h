// Blowfish（ECB 解密方向够用即可）——只为解开 .mix 的加密文件头。
//
// 加密的 mix 头里存的是 80 字节「密钥源」：RSA 公钥运算还原出 56 字节
// Blowfish 密钥，再拿它 ECB 解密后面的目录。见 docs/mix-format/README.md。
//
// 初始 P/S 表是 pi 的十六进制小数位，由 code/mix/gen_blowfish_tables.py 现算，
// 见 blowfish_tables.h。
#ifndef RA2_MIX_BLOWFISH_H_
#define RA2_MIX_BLOWFISH_H_

#include <cstddef>
#include <cstdint>

namespace ra2 {
namespace mix {

class Blowfish {
 public:
  // key_len 取 1..56 字节
  Blowfish(const std::uint8_t* key, std::size_t key_len);

  void DecryptBlock(std::uint32_t* xl, std::uint32_t* xr) const;
  void EncryptBlock(std::uint32_t* xl, std::uint32_t* xr) const;

  // ECB，len 必须是 8 的倍数；src/dst 可以相同。字节序按**大端**取块
  //（Westwood 用的是标准 Blowfish 字节序）。
  void DecryptEcb(const std::uint8_t* src, std::uint8_t* dst,
                  std::size_t len) const;

 private:
  std::uint32_t F(std::uint32_t x) const;

  std::uint32_t p_[18];
  std::uint32_t s_[4][256];
};

}  // namespace mix
}  // namespace ra2

#endif  // RA2_MIX_BLOWFISH_H_
