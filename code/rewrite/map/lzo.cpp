// LZO1X 压缩/解压 —— 逐位复刻 gamemd.exe 0x55BB90/0x55BC00/0x55C0E0。
//
// 语义来源：Ghidra 反编译（memory/data/decomp/rmg_lzo_core.txt、
// rmg_lzo_core2.txt、rmg_lzo_dec_b64core.txt）+ Unicorn 指令级跟踪。
// 结构与 FUN_0055bc00 / FUN_0055c0e0 伪代码逐条对应。
//
// 要点：
//  - 压缩入口 len==0 输出 11 00 00；len<0xE 直存（0x11+len+数据）
//  - 哈希：((src[2]<<5^src[1])<<5^src[0])，索引 (dv*0x9F5F>>5)&0x3FFF
//  - 滚动：src[i+3]<<10 ^ (src[i]>>5) ^ (dv>>5)，距离上限 0xBFFF
//  - 匹配扩展失败(3字节前缀)：M1(off<=0x800) 或 M2/M4(远距)，
//    长度域 = (char)(ip-lit_start) 的 8 位截断
//  - 长字面量/长匹配：0x00 命令 + 计数，n=(count-1)/0xFF 个 0x00 + 尾字节
//  - 解压：M3 分支 m_pos==op（流尾 11 00 00）即结束
#include "lzo.h"

#include <cstring>

namespace mapenc {
namespace {

constexpr uint32_t kHashMul = 0x9F5F;
constexpr size_t kWorkSlots = 1 << 14;  // 0x3FFF 掩码 → 16384 槽

inline uint32_t Hash(uint32_t dv) { return (dv * kHashMul >> 5) & 0x3FFF; }
inline uint32_t Roll(uint32_t dv, uint8_t hi, uint8_t lo) {
  return (uint32_t(hi) << 10) ^ (lo >> 5) ^ (dv >> 5);
}

// 长计数（编码端）：写 n=(count-1)/0xFF 个 0x00 + 尾字节(count-0xFF*n)。
inline void WriteCountBytes(uint8_t*& op, uint32_t count) {
  if (count <= 0xFF) {
    *op++ = static_cast<uint8_t>(count);
    return;
  }
  uint32_t n = (count - 1) / 0xFF;
  for (uint32_t i = 0; i < n; ++i) *op++ = 0;
  *op++ = static_cast<uint8_t>(count - 0xFF * n);
}

// 字面量输出（lit 字节），编码命令并直写数据。返回新 op。
// 注意：lit<=3 合并进 d[-2] 低 2 位（调用方保证 d[-2] 是已写命令字节）。
inline uint8_t* EmitLiteral(uint8_t* d, const uint8_t*& lit, size_t l) {
  if (l < 4) {
    d[-2] |= static_cast<uint8_t>(l);
  } else if (l < 0x13) {
    *d++ = static_cast<uint8_t>(l - 3);
  } else {
    *d++ = 0;
    WriteCountBytes(d, static_cast<uint32_t>(l - 0x12));
  }
  std::memcpy(d, lit, l);
  lit += l;
  return d + l;
}

}  // namespace

std::vector<uint8_t> LzoCompress(const uint8_t* src, size_t src_len) {
  std::vector<uint8_t> out;
  if (src_len == 0) {
    out.insert(out.end(), {0x11, 0x00, 0x00});
    return out;
  }
  if (src_len < 0xE) {
    out.push_back(static_cast<uint8_t>(src_len + 0x11));
    out.insert(out.end(), src, src + src_len);
    out.insert(out.end(), {0x11, 0x00, 0x00});
    return out;
  }

  // —— 0x55BC00 压缩核心 ——
  std::vector<uint8_t> buf(src_len * 2 + 64);
  uint8_t* const dst0 = buf.data();
  uint8_t* d = dst0;

  thread_local std::vector<const uint8_t*> work(kWorkSlots, nullptr);
  std::fill(work.begin(), work.end(), nullptr);

  const uint8_t* end = src + src_len;
  uint32_t dv = ((uint32_t(src[2]) << 5 ^ src[1]) << 5) ^ src[0];
  work[Hash(dv)] = src;
  dv = Roll(dv, src[3], src[0]);
  work[Hash(dv)] = src + 1;
  dv = Roll(dv, src[4], src[1]);
  work[Hash(dv)] = src + 2;
  dv = Roll(dv, src[5], src[2]);
  work[Hash(dv)] = src + 3;

  const uint8_t* ip = src + 4;    // 当前匹配位置（pbVar11）
  const uint8_t* lit = src;       // 字面量起点（pbVar13）
  const uint8_t* lit_save = src;  // 匹配长度基准（local_18）

loop_start:  // LAB_0055bceb
  {
    uint32_t idx = Hash(dv);
    const uint8_t* m = work[idx];
    work[idx] = ip;
    int32_t off = static_cast<int32_t>(ip - m);
    if (m < src || off < 1 || off > 0xBFFF || m[0] != ip[0] || m[1] != ip[1] ||
        m[2] != ip[2]) {
      goto no_match;
    }
    size_t l = static_cast<size_t>(ip - lit);
    if (off < 0x801) {
      // 短距离：字面量直接输出
      if (l != 0) d = EmitLiteral(d, lit, l);
    } else {
      if (l != 3) {
        if (m[3] != ip[3]) goto no_match;  // 匹配失败 → 滚动
        if (l != 0) d = EmitLiteral(d, lit, l);
      } else {
        // 3 字面量并入 d[-2] 低 2 位 + 直写 3 字节
        d[-2] |= 3;
        std::memcpy(d, lit, 3);
        lit += 3;
        d += 3;
      }
    }
    // 匹配长度基准 = 匹配起点（原版 local_18：字面量拷贝循环内持续更新）
    lit_save = lit;
    // —— 匹配扩展（0x55be3d 起）：6 个检查 m[3..8] == ip[3..8] ——
    // 失败位置决定短匹配长度域（原版 cVar4 = 3 + 通过的检查数 = 实际匹配字节数）
    {
      size_t ml = 3;
      const uint8_t* pp = ip + 3;  // 匹配终点（失败位置）
      if (m[3] == ip[3]) {
        ++ml;
        pp = ip + 4;
        for (int i = 0; i < 5; ++i) {  // m[4..8]
          if (m[4 + i] != ip[4 + i]) break;
          ++ml;
          pp = ip + 5 + i;
        }
        if (ml == 9) {
          // 6 检查全过 → 长扩展（0x55be95）：m[9..] vs ip[9..]
          const uint8_t* mp = m + 9;
          pp = ip + 9;
          while (pp < end && *mp == *pp) {
            ++mp;
            ++pp;
          }
          ml = static_cast<size_t>(pp - lit_save);
          int32_t off2 = off;
          if (off2 < 0x4001) {
            off2 -= 1;
            if (ml < 0x22) {
              *d++ = static_cast<uint8_t>(ml - 2) | 0x20;
            } else {
              *d++ = 0x20;
              WriteCountBytes(d, static_cast<uint32_t>(ml - 0x21));
            }
          } else {
            off2 -= 0x4000;
            uint8_t hi = static_cast<uint8_t>(off2 >> 0xb) & 8;
            if (ml > 9) {
              *d++ = hi | 0x10;
              WriteCountBytes(d, static_cast<uint32_t>(ml - 9));
            } else {
              *d++ = hi | static_cast<uint8_t>(ml - 2) | 0x10;
            }
          }
          // 2 字节偏移（0x55bfb5）
          *d++ = static_cast<uint8_t>(off2) << 2;
          *d++ = static_cast<uint8_t>(off2 >> 6);
          lit = pp;
          lit_save = pp;
          ip = pp;
          if (end - 0xD <= ip) goto finish;
          dv = ((uint32_t(ip[2]) << 5 ^ ip[1]) << 5) ^ ip[0];
          goto loop_start;
        }
      }
      // —— 0x55bf64：匹配扩展失败（ml = 3..8），ip 停在失败位置 ——
      {
        if (off > 0x800) {
          int32_t off2 = off;
          if (off2 < 0x4001) {
            off2 -= 1;
            *d++ = static_cast<uint8_t>(ml - 2) | 0x20;
          } else {
            off2 -= 0x4000;
            *d++ = (static_cast<uint8_t>(off2 >> 0xb) & 8) |
                   static_cast<uint8_t>(ml - 2) | 0x10;
          }
          *d++ = static_cast<uint8_t>(off2) << 2;
          *d++ = static_cast<uint8_t>(off2 >> 6);
        } else {
          // M1：2 字节命令（低 2 位留给后续 ≤3 字节字面量合并）
          *d++ = static_cast<uint8_t>(((off - 1) & 7) << 2) |
                 static_cast<uint8_t>((ml + 7) * 0x20);
          *d++ = static_cast<uint8_t>((off - 1) >> 3);
        }
        lit = pp;
        lit_save = pp;
        ip = pp;
        if (end - 0xD <= ip) goto finish;
        dv = ((uint32_t(ip[2]) << 5 ^ ip[1]) << 5) ^ ip[0];
        goto loop_start;
      }
    }
  }
no_match:  // LAB_0055bffb：未命中，滚动哈希推进 1
  if (ip + 1 < end - 0xD) {
    dv = Roll(dv, ip[3], ip[0]);
    ++ip;
    goto loop_start;
  }
finish:
  // —— 0x55c027 收尾：剩余字面量 ——
  {
    size_t rem = static_cast<size_t>(end - lit);
    if (rem >= 1) {
      if (d == dst0 && rem < 0xEF) {
        *d++ = static_cast<uint8_t>(rem + 0x11);  // 直存
      } else {
        if (rem > 3) {
          if (rem < 0x13) {
            *d++ = static_cast<uint8_t>(rem - 3);
          } else {
            *d++ = 0;
            WriteCountBytes(d, static_cast<uint32_t>(rem - 0x12));
          }
        } else {
          d[-2] |= static_cast<uint8_t>(rem);
        }
      }
      std::memcpy(d, lit, rem);
      d += rem;
    }
  }
  out.assign(dst0, d);
  out.insert(out.end(), {0x11, 0x00, 0x00});
  return out;
}

size_t LzoDecompress(const uint8_t* src, size_t src_len, uint8_t* dst,
                     size_t /*dst_cap*/) {
  const uint8_t* ip = src;
  uint8_t* op = dst;
  uint32_t t;
  if (*ip <= 0x11) goto L109;
  t = *ip - 0x11;
  ++ip;
  goto L14e;
L109:
  t = *ip++;
  if (t >= 0x10) goto L1c1;
  // 字面量（t < 0x10）
  if (t == 0) {
    // 长字面量：t = 0xf + n*0xff + tail（汇编 0x55c126：inc 前读 [eax+1]）
    t = 0xF;
    if (*ip != 0) {
      t += *ip;
      ++ip;
    } else {
      do {
        ++ip;
        t += 0xFF;
      } while (*ip == 0);
      t += *ip;
      ++ip;
    }
  }
  // 复制 3 + t 字节（0x55c13b + 0x55c14e）
  op[0] = ip[0];
  op[1] = ip[1];
  op[2] = ip[2];
  op += 3;
  ip += 3;
L14e:
  while (t--) { *op++ = *ip++; }
  // 读下一命令
  t = *ip++;
  if (t >= 0x10) goto L1c1;
  // M1 主路径（0x55c162）：m_pos = op - (t>>2) - (ip[0]<<2) - 0x801，拷 3 字节
  {
    uint8_t* m = op - (t >> 2) - (static_cast<int32_t>(ip[0]) << 2) - 0x801;
    ++ip;
    op[0] = m[0];
    op[1] = m[1];
    op[2] = m[2];
    op += 3;
  }
TAIL:  // 0x55c18e：匹配尾部 ≤3 字节流内字面量（[ip-2]&3 = 命令/第一偏移字节低 2 位）
  t = ip[-2] & 3;
  if (t == 0) goto L109;
  while (t--) { *op++ = *ip++; }
  t = *ip++;
  if (t >= 0x10) goto L1c1;
  // 链式 M1（0x55c1ad）：m_pos = op - (t>>2) - (ip[0]<<2) - 1，拷 2 字节
  {
    uint8_t* m = op - (t >> 2) - (static_cast<int32_t>(ip[0]) << 2) - 1;
    ++ip;
    op[0] = m[0];
    op[1] = m[1];
    op += 2;
  }
  goto TAIL;
L1c1:
  if (t < 0x40) {
    if (t < 0x20) {
      // M3 短距（0x55c22a）：m3hi 先取；长计数先于偏移读取；m_pos==op 即 EOF
      uint8_t* m = op - ((t & 8) << 0xB);
      t &= 7;
      if (t == 0) {
        t = 7;
        if (*ip != 0) {
          t += *ip;
          ++ip;
        } else {
          do {
            ++ip;
            t += 0xFF;
          } while (*ip == 0);
          t += *ip;
          ++ip;
        }
      }
      m -= (static_cast<int32_t>(ip[0]) >> 2) +
           (static_cast<int32_t>(ip[1]) << 6);
      ip += 2;
      if (m == op) return static_cast<size_t>(op - dst);  // EOF 11 00 00
      m -= 0x4000;
      op[0] = m[0];
      op[1] = m[1];
      op += 2;
      m += 2;
      while (t--) { *op++ = *m++; }
    } else {
      // M4 长距：t&=0x1f，m_pos = op - 1 - (ip[0]>>2) - (ip[1]<<6)
      t &= 0x1F;
      if (t == 0) {
        t = 0x1F;
        if (*ip != 0) {
          t += *ip;
          ++ip;
        } else {
          do {
            ++ip;
            t += 0xFF;
          } while (*ip == 0);
          t += *ip;
          ++ip;
        }
      }
      uint8_t* m = op - 1 - (static_cast<int32_t>(ip[0]) >> 2) -
                   (static_cast<int32_t>(ip[1]) << 6);
      ip += 2;
      op[0] = m[0];
      op[1] = m[1];
      op += 2;
      m += 2;
      while (t--) { *op++ = *m++; }
    }
  } else {
    // M2 短距：m_pos = op - 1 - ((t>>2)&7) - (ip[0]<<3)，t = (t>>5)-1
    uint8_t* m = op - 1 - ((t >> 2) & 7) - (static_cast<int32_t>(ip[0]) << 3);
    ++ip;
    t = (t >> 5) - 1;
    op[0] = m[0];
    op[1] = m[1];
    op += 2;
    m += 2;
    while (t--) { *op++ = *m++; }
  }
  goto TAIL;
}

}  // namespace mapenc
