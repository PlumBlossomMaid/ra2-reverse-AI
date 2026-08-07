// Westwood .mix 归档的**只读**解析器（TS / RA2 / YR 变体）。
//
// 这是 code/mix/mix_read.py 的 C++ 对应物——命令行程序要能自己从
// ra2.mix / ra2md.mix 里取 theater ini 与 .tmp 砖文件，不能依赖 Python。
// 只读：不写、不重打包、不把游戏数据落进仓库。
//
// 新版头（TS 起）：
//   dword flags       低 16 位为 0 才是新版头；0x00010000=带校验和，0x00020000=头加密
//   [加密时] 80 字节密钥源 -> RSA 还原 56 字节 Blowfish 密钥 -> ECB 解密下面的头
//   word  count
//   dword data_size
//   count x { dword id; dword offset; dword size }    offset 相对数据区起点
//
// 两个坑（都在 docs/mix-format/README.md 里记着）：
//   1. 名字哈希是**标准 CRC-32**（有初值取反、末尾取反），作用在
//      「大写 + 4 字节对齐补齐」之后的名字上；补齐方式很别扭，照抄。
//   2. RSA 公钥模数按**大端**读，密文块按**小端**读。
#ifndef RA2_MIX_MIX_ARCHIVE_H_
#define RA2_MIX_MIX_ARCHIVE_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ra2 {
namespace mix {

// 名字 -> 32 位 id。名字大小写不敏感。
std::uint32_t MixId(const std::string& name);

class MixArchive {
 public:
  MixArchive() {}

  // 接管一段字节。失败返回 false（头解不开 / 目录越界）。
  bool Open(std::vector<std::uint8_t> data);
  // 从内层 mix 的一段视图打开（会拷贝一份，内层 mix 通常不大……
  // isotemp.mix 有 29 MB，所以这里用 move 版本，见 OpenNested）。
  bool OpenNested(const MixArchive& parent, const std::string& name);

  bool Has(const std::string& name) const {
    return entries_.find(MixId(name)) != entries_.end();
  }
  // 找不到返回 nullptr；size 写出条目字节数。
  const std::uint8_t* Find(const std::string& name, std::size_t* size) const;
  // 便利版：拷一份出来。
  bool Read(const std::string& name, std::vector<std::uint8_t>* out) const;

  std::size_t Count() const { return entries_.size(); }
  bool encrypted() const { return encrypted_; }
  bool checksum() const { return checksum_; }

 private:
  struct Entry {
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
  };

  bool ParseHeader();

  std::vector<std::uint8_t> data_;
  std::map<std::uint32_t, Entry> entries_;
  std::size_t base_ = 0;        // 数据区起点（相对 data_ 开头）
  bool encrypted_ = false;
  bool checksum_ = false;
};

// 从 80 字节密钥源还原 56 字节 Blowfish 密钥（导出仅为便于单测）。
std::vector<std::uint8_t> BlowfishKeyFromSource(const std::uint8_t* src80);

}  // namespace mix
}  // namespace ra2

#endif  // RA2_MIX_MIX_ARCHIVE_H_
