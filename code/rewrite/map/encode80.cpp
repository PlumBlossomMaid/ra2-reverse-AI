// encode80 / F80 实现 —— 见 encode80.h 头部注释。
#include "encode80.h"

#include <algorithm>
#include <cstring>

namespace mapenc {

namespace {

inline void WriteU16(std::uint8_t*& w, int v) {
  w[0] = static_cast<std::uint8_t>(v & 0xFF);
  w[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
  w += 2;
}

// 0cccpppp p：copy，count = (code>>4)+3 ∈ [3,10]，偏移 = ((code&0xF)<<8)|p，
// 从「当前位置 - 偏移」拷贝（向后引用，距离 ∈ [1, 0xFFF]）。
inline void WriteC0(std::uint8_t*& w, int count, int dist) {
  *w++ = static_cast<std::uint8_t>(((count - 3) << 4) | (dist >> 8));
  *w++ = static_cast<std::uint8_t>(dist & 0xFF);
}

// 10cccccc：copy，count = code & 0x3F ∈ [0, 63]，直接复制 count 字节。
inline void WriteC1(std::uint8_t*& w, int count, const std::uint8_t* r) {
  while (count > 0) {
    const int n = count < 0x40 ? count : 0x3F;
    *w++ = static_cast<std::uint8_t>(0x80 | n);
    std::memcpy(w, r, static_cast<std::size_t>(n));
    r += n;
    w += n;
    count -= n;
  }
}

// 11cccccc pp：copy，count = (code & 0x3F) + 3 ∈ [3, 66]，偏移 = 16 位**绝对**
// 位置（从输出开头数），即 p - s。
inline void WriteC2(std::uint8_t*& w, int count, int abs_off) {
  *w++ = static_cast<std::uint8_t>(0xC0 | (count - 3));
  WriteU16(w, abs_off);
}

// 11111110 cc vv：fill，count = 16 位，填同一个值 v。
inline void WriteC3(std::uint8_t*& w, int count, int v) {
  *w++ = 0xFE;
  WriteU16(w, count);
  *w++ = static_cast<std::uint8_t>(v);
}

// 11111111 cc vv pp pp：copy，count = 16 位，偏移 = 16 位**绝对**位置。
inline void WriteC4(std::uint8_t*& w, int count, int abs_off) {
  *w++ = 0xFF;
  WriteU16(w, count);
  WriteU16(w, abs_off);
}

// 把攒着的 raw 段 flush 成一条或多条 10cccccc。
inline void FlushC1(std::uint8_t*& w, const std::uint8_t* r,
                    const std::uint8_t*& copy_from) {
  if (copy_from != nullptr) {
    WriteC1(w, static_cast<int>(r - copy_from), copy_from);
    copy_from = nullptr;
  }
}

// 从 r 起的连续相同字节长度（run length）。
int RunLength(const std::uint8_t* r, const std::uint8_t* s_end) {
  const std::uint8_t v = *r;
  const std::uint8_t* p = r + 1;
  while (p < s_end && *p == v) ++p;
  return static_cast<int>(p - r);
}

// get_same：在已编码前缀 [s, r) 里找与 r 开头**最长公共前缀**的起点。
// 返回 p = 起点、cb_p = 匹配长度。XCC 原版是 O(n²) 的全量搜索，照抄。
void GetSame(const std::uint8_t* s, const std::uint8_t* r,
             const std::uint8_t* s_end, const std::uint8_t*& p, int& cb_p) {
  cb_p = 0;
  p = nullptr;
  for (const std::uint8_t* q = s; q < r; ++q) {
    int len = 0;
    const std::uint8_t* a = q;
    const std::uint8_t* b = r;
    while (b < s_end && *a == *b) {
      ++a;
      ++b;
      ++len;
    }
    if (len > cb_p) {
      cb_p = len;
      p = q;
    }
  }
}

}  // namespace

std::size_t Encode80(const std::uint8_t* s, std::uint8_t* d, std::size_t cb_s) {
  const std::uint8_t* const s_end = s + cb_s;
  const std::uint8_t* r = s;
  std::uint8_t* w = d;
  const std::uint8_t* copy_from = nullptr;

  while (r < s_end) {
    const int t = RunLength(r, s_end);
    const std::uint8_t* p = nullptr;
    int cb_p = 0;
    GetSame(s, r, s_end, p, cb_p);
    if (t < cb_p && cb_p > 2) {
      FlushC1(w, r, copy_from);
      const int dist = static_cast<int>(r - p);
      if (cb_p - 3 < 8 && dist < 0x1000) {
        WriteC0(w, cb_p, dist);
      } else if (cb_p - 3 < 0x3E) {
        WriteC2(w, cb_p, static_cast<int>(p - s));
      } else {
        WriteC4(w, cb_p, static_cast<int>(p - s));
      }
      r += cb_p;
    } else {
      if (t < 3) {
        if (copy_from == nullptr) copy_from = r;
      } else {
        FlushC1(w, r, copy_from);
        WriteC3(w, t, *r);
      }
      r += t;
    }
  }
  FlushC1(w, r, copy_from);
  WriteC1(w, 0, nullptr);   // 0x80 结束码
  return static_cast<std::size_t>(w - d);
}

std::vector<std::uint8_t> EncodeF80(const std::vector<std::uint8_t>& data,
                                    std::size_t n_sections) {
  std::vector<std::uint8_t> out;
  out.reserve(data.size());
  const std::size_t len = data.size() / n_sections;
  std::vector<std::uint8_t> packed(data.size());   // 每段最坏 = 段长
  for (std::size_t i = 0; i < n_sections; ++i) {
    const std::uint8_t* sec = data.data() + i * len;
    const std::size_t pack_len =
        Encode80(sec, packed.data(), len);
    // [3B 压缩长 LE][1B 0x20][数据]
    out.push_back(static_cast<std::uint8_t>(pack_len & 0xFF));
    out.push_back(static_cast<std::uint8_t>((pack_len >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((pack_len >> 16) & 0xFF));
    out.push_back(0x20);
    out.insert(out.end(), packed.begin(),
               packed.begin() + static_cast<std::ptrdiff_t>(pack_len));
  }
  return out;
}

}  // namespace mapenc
