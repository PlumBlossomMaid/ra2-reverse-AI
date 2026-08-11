// base64 编码实现 —— 见 base64.h 头部注释。
#include "base64.h"

namespace mapenc {

namespace {
constexpr char kTable[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string Base64Encode(const uint8_t* src, size_t src_len) {
  std::string out;
  out.reserve((src_len + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 3 <= src_len; i += 3) {
    const uint32_t v = (src[i] << 16) | (src[i + 1] << 8) | src[i + 2];
    out += kTable[v >> 18];
    out += kTable[(v >> 12) & 0x3F];
    out += kTable[(v >> 6) & 0x3F];
    out += kTable[v & 0x3F];
  }
  const size_t rest = src_len - i;
  if (rest == 1) {
    const uint32_t v = src[i] << 16;
    out += kTable[v >> 18];
    out += kTable[(v >> 12) & 0x3F];
    out += '=';
    out += '=';
  } else if (rest == 2) {
    const uint32_t v = (src[i] << 16) | (src[i + 1] << 8);
    out += kTable[v >> 18];
    out += kTable[(v >> 12) & 0x3F];
    out += kTable[(v >> 6) & 0x3F];
    out += '=';
  }
  return out;
}

std::string Base64Encode(const std::vector<uint8_t>& src) {
  return Base64Encode(src.data(), src.size());
}

}  // namespace mapenc
