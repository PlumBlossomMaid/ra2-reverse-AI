// LCW 压缩/解压实现 —— 复刻 0x551E50 / 0x551C60。
// 语义来源：rmg_lcw_core.txt 反编译 + rmg_lcw_dec_asm.txt 汇编逐指令核对。
// 寄存器映射（压缩器）：esi=p 扫描位置 / edi=base 滚动搜索起点 /
// [ebp-0x20]=prev_match / [ebp-0x8]=字面量模式标志 / [ebp-0xc]=匹配长度。
#include "lcw.h"

namespace mapenc {

std::vector<uint8_t> LcwCompress(const uint8_t* src, size_t src_len) {
  const uint8_t* const end = src + src_len;
  std::vector<uint8_t> out(src_len * 2 + 64);
  uint8_t* op = out.data();

  const uint8_t* p = src + 1;        // esi：扫描位置（入口 lodsb 后）
  const uint8_t* base = src;         // edi：匹配搜索起点（主循环重置为 src）
  const uint8_t* prev_match = src;   // [ebp-0x20]：前次匹配源位置
  bool lit_mode = true;              // [ebp-0x8]：活动字面量命令存在
  uint32_t ml = 1;                   // [ebp-0xc]：匹配长度
  uint8_t* cmd_pos = out.data();     // [ebp-0x14]：字面量命令字节位置

  op[0] = 0x81;                      // 入口：dst[0]=0x81, dst[1]=src[0]
  op[1] = src[0];
  op += 2;

  for (;;) {
    base = src;                      // 0x551ec9：主循环重置搜索起点
    ml = 1;                          // 0x551ecc
  LAB_ed3:
    // —— run 检测（0x551ed3）：*p == p[0x40] 才统计 ——
    for (;;) {
      // 越界读按 YR 缓冲语义补 0（0x4000 清零缓冲）：p 可能 ≥ end（len≤1），
      // run 输出后 p 可能推进到接近 end 而 ml 未重置。
      const uint8_t c = (p < end) ? *p : 0;
      const uint8_t p40 = (p + 0x40 < end) ? p[0x40] : 0;
      if (c != p40) break;
      // repe scasb（ecx = end-p）：统计从 p 起连续 ==c 的字节数
      const uint8_t* q = p;
      while (q < end && *q == c) ++q;
      --q;                           // dec edi：最后匹配位置
      const size_t run_len = q - p;  // 连续数-1（匹配数=run_len+1）
      if (run_len < 0x41) break;     // jb 0x551f0f：不够长不编码
      lit_mode = false;              // 0x551ef1
      p = q;                         // 0x551ef8：esi = run 末尾
      op[0] = 0xFE;
      op[1] = static_cast<uint8_t>(run_len);
      op[2] = static_cast<uint8_t>(run_len >> 8);
      op[3] = c;
      op += 4;
      // 0x551f08：输出位置已存；0x551f0b：edi=base（本实现 base 不变）→ 重检
    }
    // —— 匹配查找（0x551f11）：repne scasb 从 base 找 *p ——
    for (;;) {
      if (p == base) break;          // ecx=p-base==0 → je 0x551f4d
      const uint8_t* found = nullptr;
      for (const uint8_t* q = base; q < p; ++q) {
        if (*q == *p) { found = q; break; }   // repne scasb
      }
      if (!found) break;             // jne 0x551f4d
      base = found + 1;              // repne 后 edi=候选+1
      // 0x551f1e/0x551f22：p[ml-1] == 候选[ml-1]（edi+ml-2 = 候选+ml-1）。
      // ml 可能来自上次 match_extend（run 输出推进 p 后未重置），越界读按 0 处理。
      // 注意 ml-1/ml-2 必须用有符号 ptrdiff_t：原版是 32 位指针减法（ml=1 时
      // 读候选[-1] 是合法负偏移），uint32 下溢会变成巨大地址。
      const ptrdiff_t off1 = static_cast<ptrdiff_t>(ml) - 1;
      const ptrdiff_t off2 = static_cast<ptrdiff_t>(ml) - 2;
      const uint8_t v1 = (p + off1 < end) ? p[off1] : 0;
      const uint8_t v2 = (base + off2 < end) ? base[off2] : 0;
      if (v1 == v2) {
        // —— match_extend（0x551f28）：repe cmpsb p[i] vs 候选[i] ——
        const uint8_t* cand = base - 1;
        size_t m = 0;
        while (p + m < end && p[m] == cand[m]) ++m;
        if (m >= ml) {               // 0x551f3f：新长度 >= ml 才更新
          ml = static_cast<uint32_t>(m);
          prev_match = cand;         // 0x551f47/48：local_24 = 候选
        }
        base = cand + 1;             // 0x551f3b：edi = 候选+1
        goto LAB_ed3;                // jmp 0x551ed3
      }
      // jne 0x551f11：继续扫描
    }
    // —— 发射（0x551f4d）——
    if (ml <= 2) {
      // 字面量（0x551f89）
      if (!lit_mode) {               // cmp [ebp-8],0; jne 0x551f95
        cmd_pos = op;                // 0x551f8f
        op[0] = 0x80;
        ++op;
      }
      while (*cmd_pos == 0xBF) {     // 0x551f98：命令满 0xBF 开新命令
        cmd_pos = op;
        op[0] = 0x80;
        ++op;
      }
      ++*cmd_pos;                    // 0x551f9d：命令计数+1
      op[0] = *p;                    // lodsb/stosb
      ++op;
      ++p;
      lit_mode = true;               // 0x551fa1
    } else {
      if (ml <= 10 && (p - prev_match) <= 0xFFF) {
        // 短复制（0x551f5d-0x551f77）：off 为相对距离，2 字节
        const ptrdiff_t off = p - prev_match;
        op[0] = static_cast<uint8_t>(((ml - 3) << 4) + ((off >> 8) & 0xFF));
        op[1] = static_cast<uint8_t>(off);
        op += 2;
      } else if (ml <= 0x40) {
        // 中复制（0x551f79）：off 为绝对偏移，命令 + LE16
        op[0] = static_cast<uint8_t>((ml - 3) | 0xC0);
        ++op;
        const ptrdiff_t off = prev_match - src;
        op[0] = static_cast<uint8_t>(off);
        op[1] = static_cast<uint8_t>(off >> 8);
        op += 2;
      } else {
        // 长复制（0x551faa）：0xFF + LE16 len + LE16 off
        op[0] = 0xFF;
        op[1] = static_cast<uint8_t>(ml);
        op[2] = static_cast<uint8_t>(ml >> 8);
        op += 3;
        const ptrdiff_t off = prev_match - src;
        op[0] = static_cast<uint8_t>(off);
        op[1] = static_cast<uint8_t>(off >> 8);
        op += 2;
      }
      p += ml;                       // 0x551fba
      lit_mode = false;              // 0x551fbd
    }
    if (p >= end) {                  // 0x551fc4：收尾写 0x80
      op[0] = 0x80;
      ++op;
      break;
    }
    // jmp 0x551ec6
  }
  out.resize(static_cast<size_t>(op - out.data()));
  return out;
}

size_t LcwDecompress(const uint8_t* src, size_t src_len, uint8_t* dst,
                     size_t /*dst_cap*/) {
  const uint8_t* ip = src;
  uint8_t* op = dst;
  for (;;) {
    const uint8_t b = *ip++;
    if ((b & 0x80) == 0) {
      // 短复制（0x551c82）：len=(b>>4)+3，m=op-((b&0xF)<<8)-off
      uint8_t* m = op - ((b & 0xF) << 8) - *ip++;
      size_t n = (b >> 4) + 3;
      while (n--) *op++ = *m++;
    } else if (b & 0x40) {
      if (b == 0xFE) {
        // 填充（0x551cd9）：count=LE16，fill=第 3 参数字节
        const uint32_t count = ip[0] | (uint32_t(ip[1]) << 8);
        const uint8_t fill = ip[2];
        ip += 3;
        for (uint32_t i = 0; i < count; ++i) *op++ = fill;
      } else if (b == 0xFF) {
        // 长复制（0x551dc1）：len/off 均 LE16
        const uint32_t n = ip[0] | (uint32_t(ip[1]) << 8);
        const uint32_t off = ip[2] | (uint32_t(ip[3]) << 8);
        ip += 4;
        uint8_t* m = dst + off;
        for (uint32_t i = 0; i < n; ++i) *op++ = *m++;
      } else {
        // 中复制（0x551dfd）：len=(b&0x3F)+3，off=LE16
        const uint32_t n = (b & 0x3F) + 3;
        const uint32_t off = ip[0] | (uint32_t(ip[1]) << 8);
        ip += 2;
        uint8_t* m = dst + off;
        for (uint32_t i = 0; i < n; ++i) *op++ = *m++;
      }
    } else if (b == 0x80) {
      break;                         // 0x551e36：流结束
    } else {
      // 字面量（0x551cc3）：len=b&0x3F
      uint32_t n = b & 0x3F;
      while (n--) *op++ = *ip++;
    }
  }
  return static_cast<size_t>(op - dst);
}

}  // namespace mapenc
