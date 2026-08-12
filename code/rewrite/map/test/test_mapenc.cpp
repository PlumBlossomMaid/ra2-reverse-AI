// 地图编码模块黄金对拍：LZO/LCW 压缩解压 vs 原版机器码（Unicorn 取证）。
//
// 基准文件 memory/data/rmg/mapenc_golden.txt 由 code/randmap/dump_mapenc_golden.py
// 用 Unicorn 跑 gamemd.exe 0x55BB90/0x55C0E0/0x551E50/0x551C60 取标准答案。
// 样本数据由本文件按 dump_mapenc_golden.py 的 gen_samples 同款确定性逻辑重建
// （rng.Random(0x5EED) 等价序列 + cells512/terrain64/overlay16k 固定公式）。
#include "lzo.h"
#include "lcw.h"
#include "base64.h"
#include "isomap.h"
#include "encode80.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#ifndef RA2_GOLDEN_DIR
#error "RA2_GOLDEN_DIR must be defined by the build (see map/CMakeLists.txt)"
#endif

namespace mapenc {
namespace {

// —— MT19937，复刻 CPython random.Random（_randommodule.c）——
// 初始化 = init_genrand(19650218U) + init_by_array(key)，不是简单 Knuth 种子。
struct Mt19937 {
  std::vector<uint32_t> mt;
  // CPython init 后 index=N(624)：首次 getrandbits 先 twist 再取数。
  size_t mti = kN;
  static constexpr size_t kN = 624;

  explicit Mt19937(uint32_t seed) : mt(kN) {
    // init_genrand(19650218U)
    mt[0] = 19650218U;
    for (size_t i = 1; i < kN; ++i)
      mt[i] = 1812433253u * (mt[i - 1] ^ (mt[i - 1] >> 30)) + static_cast<uint32_t>(i);
    // init_by_array([seed], 1)
    uint32_t key[1] = {seed};
    size_t i = 1, j = 0;
    size_t k = kN > 1 ? kN : 1;
    for (; k; --k) {
      mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1664525U)) +
              key[j] + static_cast<uint32_t>(j);
      ++i; ++j;
      if (i >= kN) { mt[0] = mt[kN - 1]; i = 1; }
      if (j >= 1) j = 0;
    }
    for (k = kN - 1; k; --k) {
      mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1566083941U)) -
              static_cast<uint32_t>(i);
      ++i;
      if (i >= kN) { mt[0] = mt[kN - 1]; i = 1; }
    }
    mt[0] = 0x80000000U;
  }
  uint32_t Next() {
    if (mti >= kN) {
      for (size_t i = 0; i < kN; ++i) {
        uint32_t y = (mt[i] & 0x80000000u) | (mt[(i + 1) % kN] & 0x7FFFFFFFu);
        mt[i] = mt[(i + 397) % kN] ^ (y >> 1);
        if (y & 1) mt[i] ^= 0x9908B0DFu;
      }
      mti = 0;
    }
    uint32_t y = mt[mti++];
    y ^= y >> 11;
    y ^= (y << 7) & 0x9D2C5680u;
    y ^= (y << 15) & 0xEFC60000u;
    y ^= y >> 18;
    return y;
  }
  // Python random.randrange(n)：_randbelow(n) → getrandbits(n.bit_length())。
  // bit_length() 是"表示 n 所需最少位数"（256→9 位），取 MT 输出**高 k 位**
  // （CPython _randommodule.c: word >> (32-k)），拒绝采样 r >= n。
  int RandBelow(int n) {
    if (n <= 1) return 0;
    int k = 0;
    while ((1u << k) <= static_cast<unsigned>(n)) ++k;
    uint32_t r;
    do {
      r = Next() >> (32 - k);
    } while (r >= static_cast<unsigned>(n));
    return static_cast<int>(r);
  }
};

struct Sample {
  std::string name;
  std::vector<uint8_t> data;
};

std::vector<Sample> GenSamples() {
  Mt19937 rng(0x5EED);
  std::vector<Sample> out;
  auto add = [&](const std::string& name, const std::vector<uint8_t>& d) {
    out.push_back({name, d});
  };
  auto rand_bytes = [&](size_t n) {
    std::vector<uint8_t> b(n);
    for (size_t i = 0; i < n; ++i) b[i] = static_cast<uint8_t>(rng.RandBelow(256));
    return b;
  };
  for (int n : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
                31, 32, 33, 63, 64, 65})
    add("len" + std::to_string(n), rand_bytes(static_cast<size_t>(n)));
  add("zero_100", std::vector<uint8_t>(100, 0));
  add("zero_8192", std::vector<uint8_t>(8192, 0));
  add("one_512", std::vector<uint8_t>(512, 0x41));
  add("ff_8192", std::vector<uint8_t>(8192, 0xFF));
  {
    std::vector<uint8_t> b;
    for (int i = 0; i < 1024; ++i)
      for (char c : {'A', 'B', 'C', 'D'}) b.push_back(static_cast<uint8_t>(c));
    add("pat_abcd", b);
  }
  {
    std::vector<uint8_t> b;
    for (int i = 0; i < 64; ++i)
      for (int v = 0; v < 128; ++v) b.push_back(static_cast<uint8_t>(v));
    add("pat_0127", b);
  }
  {
    std::vector<uint8_t> b;
    static const uint8_t pat[] = {0, 1, 0, 1, 0, 1, 2, 3};
    for (int i = 0; i < 300; ++i)
      for (uint8_t v : pat) b.push_back(v);
    add("pat_weird", b);
  }
  {
    // 类 cell 数据：11 字节/格（与 dump 脚本公式一致）
    std::vector<uint8_t> cell;
    for (int i = 0; i < 0x200; ++i) {
      uint32_t g = (static_cast<uint32_t>(i) * 2654435761u) & 0xFFFF;
      uint16_t vals[3] = {static_cast<uint16_t>(i),
                          static_cast<uint16_t>(i & 0x1FF),
                          static_cast<uint16_t>(g & 0xFFFF)};
      for (auto v : vals) { cell.push_back(v & 0xFF); cell.push_back(v >> 8); }
      cell.push_back(static_cast<uint8_t>(i & 0xFF));
      cell.push_back(static_cast<uint8_t>((i >> 3) & 0xFF));
      cell.push_back(0);
      cell.push_back(static_cast<uint8_t>((i >> 5) & 0x7F));
      cell.push_back(static_cast<uint8_t>(i & 0xFF));
    }
    add("cells512", cell);
  }
  for (int base : {8191, 8192, 8193}) add("rand" + std::to_string(base), rand_bytes(base));
  {
    std::vector<uint8_t> terrain;
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x) {
        uint16_t ground = static_cast<uint16_t>((x * 7 + y * 13) % 0x100);
        uint16_t vals[3] = {static_cast<uint16_t>(x), static_cast<uint16_t>(y), ground};
        for (auto v : vals) { terrain.push_back(v & 0xFF); terrain.push_back(v >> 8); }
        terrain.push_back(static_cast<uint8_t>((x + y) & 0xFF));
        terrain.push_back(0);
        terrain.push_back(0);
        terrain.push_back(0);
        terrain.push_back(0);
      }
    add("terrain64", terrain);
  }
  {
    std::vector<uint8_t> ovl;
    for (int i = 0; i < 16384; ++i) {
      // rng.choice([0,0,0,1,2,0xFF])：randrange(6) 索引
      static const int table[] = {0, 0, 0, 1, 2, 0xFF};
      ovl.push_back(static_cast<uint8_t>(table[rng.RandBelow(6)]));
    }
    add("overlay16k", ovl);
  }
  return out;
}

std::string Hex(const std::vector<uint8_t>& b) {
  static const char* hex = "0123456789abcdef";
  std::string s;
  s.reserve(b.size() * 2);
  for (uint8_t c : b) { s.push_back(hex[c >> 4]); s.push_back(hex[c & 0xF]); }
  return s;
}

std::vector<uint8_t> Unhex(const std::string& h) {
  std::vector<uint8_t> b;
  for (size_t i = 0; i + 1 < h.size(); i += 2) {
    auto nib = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      return c - 'a' + 10;
    };
    b.push_back(static_cast<uint8_t>((nib(h[i]) << 4) | nib(h[i + 1])));
  }
  return b;
}

struct GoldenCase {
  std::string name;
  int len = 0;
  std::vector<uint8_t> lzo_enc;
  int lzo_enc_len = 0;
  int lzo_dec_len = 0;
  std::string lzo_ok;
  std::vector<uint8_t> lcw_enc;
  int lcw_enc_len = 0;
  int lcw_dec_len = 0;
  std::string lcw_ok;
};

std::vector<GoldenCase> LoadGolden() {
  const std::string path = std::string(RA2_GOLDEN_DIR) + "/mapenc_golden.txt";
  std::ifstream fin(path);
  EXPECT_TRUE(fin.good()) << "cannot open " << path;
  std::vector<GoldenCase> cases;
  GoldenCase cur;
  std::string line;
  while (std::getline(fin, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind("## ", 0) == 0) {
      if (!cur.name.empty()) cases.push_back(cur);
      cur = GoldenCase();
      std::istringstream ss(line.substr(3));
      ss >> cur.name;
      std::string tok;
      while (ss >> tok)
        if (tok.rfind("len=", 0) == 0) cur.len = std::atoi(tok.c_str() + 4);
      continue;
    }
    std::istringstream ss(line);
    std::string tok;
    while (ss >> tok) {
      if (tok.rfind("lzo_enc_len=", 0) == 0) { cur.lzo_enc_len = std::atoi(tok.c_str() + 12); cur.lzo_enc.clear(); }
      else if (tok.rfind("lzo_enc_hex=", 0) == 0) { cur.lzo_enc = Unhex(tok.c_str() + 12); }
      else if (tok.rfind("lzo_dec_len=", 0) == 0) { cur.lzo_dec_len = std::atoi(tok.c_str() + 12); }
      else if (tok.rfind("ok=", 0) == 0) {
        if (cur.lcw_enc_len > 0) cur.lcw_ok = tok.c_str() + 3;
        else cur.lzo_ok = tok.c_str() + 3;
      }
      else if (tok.rfind("lcw_enc_len=", 0) == 0) { cur.lcw_enc_len = std::atoi(tok.c_str() + 12); cur.lcw_enc.clear(); }
      else if (tok.rfind("lcw_enc_hex=", 0) == 0) { cur.lcw_enc = Unhex(tok.c_str() + 12); }
      else if (tok.rfind("lcw_dec_len=", 0) == 0) { cur.lcw_dec_len = std::atoi(tok.c_str() + 12); }
    }
  }
  if (!cur.name.empty()) cases.push_back(cur);
  return cases;
}

TEST(MapencLzo, CompressMatchesOriginal) {
  const auto samples = GenSamples();
  const auto golden = LoadGolden();
  ASSERT_EQ(samples.size(), golden.size()) << "golden/sample 数量不一致";
  for (size_t i = 0; i < samples.size(); ++i) {
    SCOPED_TRACE(samples[i].name);
    ASSERT_EQ(samples[i].name, golden[i].name);
    const auto comp = LzoCompress(samples[i].data.data(), samples[i].data.size());
    EXPECT_EQ(Hex(comp), Hex(golden[i].lzo_enc))
        << "LZO 压缩逐字节不一致（样本 " << samples[i].name << "）";
    EXPECT_EQ(static_cast<int>(comp.size()), golden[i].lzo_enc_len);
  }
}

TEST(MapencLzo, DecompressRoundTrip) {
  const auto samples = GenSamples();
  for (const auto& s : samples) {
    SCOPED_TRACE(s.name);
    const auto comp = LzoCompress(s.data.data(), s.data.size());
    std::vector<uint8_t> dec(comp.size() * 2 + s.data.size() + 64);
    const size_t n = LzoDecompress(comp.data(), comp.size(), dec.data(), dec.size());
    dec.resize(n);
    EXPECT_EQ(Hex(dec), Hex(s.data)) << "LZO 解压往返不一致（样本 " << s.name << "）";
  }
}

TEST(MapencLzo, DecompressOriginalStream) {
  // 用原版压缩流直接喂给解压器：验证解压器对 YR 生成的流的兼容性。
  const auto samples = GenSamples();
  const auto golden = LoadGolden();
  for (size_t i = 0; i < samples.size(); ++i) {
    SCOPED_TRACE(samples[i].name);
    if (golden[i].lzo_enc.empty()) continue;
    std::vector<uint8_t> dec(golden[i].lzo_enc.size() * 2 + samples[i].data.size() + 64);
    const size_t n = LzoDecompress(golden[i].lzo_enc.data(), golden[i].lzo_enc.size(),
                                   dec.data(), dec.size());
    EXPECT_EQ(static_cast<int>(n), golden[i].lzo_dec_len);
    dec.resize(n);
    if (golden[i].lzo_ok == "YES")
      EXPECT_EQ(Hex(dec), Hex(samples[i].data));
  }
}

TEST(MapencLcw, CompressMatchesOriginal) {
  // len0 的 golden 是 oracle 内存布局产物（压缩器越界读 0x4000 清零缓冲之外的
  // 残留数据），非 LCW 算法语义——真实 Pipe 不会压缩 0 字节块，跳过该样本。
  const auto samples = GenSamples();
  const auto golden = LoadGolden();
  ASSERT_EQ(samples.size(), golden.size()) << "golden/sample 数量不一致";
  for (size_t i = 0; i < samples.size(); ++i) {
    SCOPED_TRACE(samples[i].name);
    if (samples[i].data.empty()) continue;
    ASSERT_EQ(samples[i].name, golden[i].name);
    const auto comp = LcwCompress(samples[i].data.data(), samples[i].data.size());
    EXPECT_EQ(Hex(comp), Hex(golden[i].lcw_enc))
        << "LCW 压缩逐字节不一致（样本 " << samples[i].name << "）";
    EXPECT_EQ(static_cast<int>(comp.size()), golden[i].lcw_enc_len);
  }
}

TEST(MapencLcw, DecompressRoundTrip) {
  // len0/len1 跳过：len<2 时压缩器首字节越界读产生垃圾流，原版自身往返即失败
  // （golden len0/len1 lcw_dec ok=NO，解压 65537 字节），无往返语义可验证。
  const auto samples = GenSamples();
  for (const auto& s : samples) {
    SCOPED_TRACE(s.name);
    if (s.data.size() <= 1) continue;
    const auto comp = LcwCompress(s.data.data(), s.data.size());
    std::vector<uint8_t> dec(comp.size() * 2 + s.data.size() + 64);
    const size_t n = LcwDecompress(comp.data(), comp.size(), dec.data(), dec.size());
    dec.resize(n);
    EXPECT_EQ(Hex(dec), Hex(s.data)) << "LCW 解压往返不一致（样本 " << s.name << "）";
  }
}

TEST(MapencLcw, DecompressOriginalStream) {
  // 用原版 LCW 流直接喂给解压器（golden 中 ok=YES 的样本）。
  const auto samples = GenSamples();
  const auto golden = LoadGolden();
  for (size_t i = 0; i < samples.size(); ++i) {
    SCOPED_TRACE(samples[i].name);
    if (golden[i].lcw_enc.empty() || golden[i].lcw_ok != "YES") continue;
    std::vector<uint8_t> dec(golden[i].lcw_enc.size() * 2 + samples[i].data.size() + 64);
    const size_t n = LcwDecompress(golden[i].lcw_enc.data(), golden[i].lcw_enc.size(),
                                   dec.data(), dec.size());
    EXPECT_EQ(static_cast<int>(n), golden[i].lcw_dec_len);
    dec.resize(n);
    EXPECT_EQ(Hex(dec), Hex(samples[i].data));
  }
}

// —— IsoMapPack5 / LZO Pipe / base64（dump_isomap_golden.py 取证）——

TEST(MapencBase64, KnownVectors) {
  // 标准 base64 向量（0x42FD30 语义：3→4 映射，'=' 填充）
  EXPECT_EQ(Base64Encode((const uint8_t*)"", 0), "");
  EXPECT_EQ(Base64Encode((const uint8_t*)"f", 1), "Zg==");
  EXPECT_EQ(Base64Encode((const uint8_t*)"fo", 2), "Zm8=");
  EXPECT_EQ(Base64Encode((const uint8_t*)"foo", 3), "Zm9v");
  EXPECT_EQ(Base64Encode((const uint8_t*)"foob", 4), "Zm9vYg==");
  EXPECT_EQ(Base64Encode((const uint8_t*)"fooba", 5), "Zm9vYmE=");
  EXPECT_EQ(Base64Encode((const uint8_t*)"foobar", 6), "Zm9vYmFy");
  // 编码表逐字符核对（0x008189C8 实锤）
  EXPECT_EQ(Base64Encode((const uint8_t*)"\x00\x00\x00", 3), "AAAA");
  EXPECT_EQ(Base64Encode((const uint8_t*)"\xFF\xFF\xFF", 3), "////");
  EXPECT_EQ(Base64Encode((const uint8_t*)"\x00\x10\x83", 3), "ABCD");
}

TEST(MapencIsomap, GoldenMatch) {
  // 与 dump_isomap_golden.py 同场景：W=70 H=70（MapRect 74/82），4 槽 2 有效 2 过滤。
  // golden 块流（oracle 0x56B3F0 实测）：[u16 csize=30][u16 usize=26][lzo 30B]
  const std::string golden_hex =
      "1e001a002b28002800010001001a05773c0032000200000000010000000000110000";
  std::vector<const IsoCell*> slots(0x40000, nullptr);
  IsoCell c1{40, 40, 0x00010001, 0x1A, 0x05, 0x77};
  IsoCell c2{60, 50, 0x00000002, 0x00, 0x01, 0x00};
  IsoCell c3{30, 30, 0x00000003, 0x10, 0x02, 0x11};   // X+Y=60 <= 74 → 过滤
  IsoCell c4{40, 41, 0x0000FFFF, 0x20, 0x00, 0x22};   // iso=0xFFFF 且 level=0 → 过滤
  slots[40 * 512 + 40] = &c1;
  slots[50 * 512 + 60] = &c2;
  slots[30 * 512 + 30] = &c3;
  slots[41 * 512 + 40] = &c4;
  const auto blocks = IsoMapPack5Encode(slots, 0x40000, 74, 82);
  EXPECT_EQ(Hex(blocks), golden_hex);
  // 结构检查：单块 [u16 csize][u16 usize]，usize = 2*11 + 4 = 26
  ASSERT_GE(blocks.size(), 4u);
  EXPECT_EQ(blocks[0], 30);  // csize low
  EXPECT_EQ(blocks[1], 0);
  EXPECT_EQ(blocks[2], 26);  // usize low
  EXPECT_EQ(blocks[3], 0);
  // UUBlock 行：34 字节 base64 = 48 字符 < 70 → 单行
  const auto lines = UUBlockLines(blocks);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0].size(), 48u);
  // 与手算 base64 对照（Python base64.b64encode 同款）
  EXPECT_EQ(lines[0],
            "HgAaACsoACgAAQABABoFdzwAMgACAAAAAAEAAAAAABEAAA==");
}

// —— encode80 / F80（OverlayPack 写链）——

TEST(MapencEncode80, EndMarker) {
  // 全 0xFF 8192 字节段 → FE 00 20 FF 80（fill 0x2000 + 0x80 结束码）。
  // 与 FA2 空地图 测试.mpr 的 OverlayPack 空段逐字节一致：
  //   测试.mpr 空段 = 05 00 00 20 | fe 00 20 ff 80（len=5）
  // 回归：WriteC1(count==0) 不输出任何字节，0x80 必须直写。
  std::vector<uint8_t> ff(8192, 0xFF);
  std::vector<uint8_t> packed(8192);
  const size_t n = Encode80(ff.data(), packed.data(), ff.size());
  ASSERT_EQ(n, 5u);
  const std::vector<uint8_t> want = {0xFE, 0x00, 0x20, 0xFF, 0x80};
  EXPECT_EQ(Hex(std::vector<uint8_t>(packed.begin(), packed.begin() + n)),
            Hex(want));
}

TEST(MapencEncode80, F80EverySectionHasEndMarker) {
  // 0x40000 全 0xFF → 32 段；每段 [3B len][0x20][数据]，数据末字节必须是 0x80。
  std::vector<uint8_t> ff(0x40000, 0xFF);
  const auto f80 = EncodeF80(ff, 32);
  EXPECT_EQ(f80.size(), 32 * 9u);   // 每段 4B 头 + 5B 数据
  for (size_t i = 0; i < 32; ++i) {
    const size_t p = i * 9;
    const size_t len = f80[p] | (f80[p + 1] << 8) | (f80[p + 2] << 16);
    ASSERT_EQ(len, 5u);
    ASSERT_EQ(f80[p + 3], 0x20);
    EXPECT_EQ(f80[p + 4 + len - 1], 0x80) << "段 " << i << " 缺 0x80 结束码";
  }
}

TEST(MapencLzoPipe, MultiBlock) {
  // >0x2000 数据 → 满块 usize=0x2000 + 尾部块；块流可逆（解压拼回原文）
  std::vector<uint8_t> raw;
  Mt19937 rng(0xBEEF);
  for (int i = 0; i < 9000; ++i) raw.push_back(static_cast<uint8_t>(rng.RandBelow(256)));
  const auto blocks = LzoPipeCompress(raw);
  size_t p = 0;
  std::vector<uint8_t> dec;
  int nblocks = 0;
  while (p + 4 <= blocks.size()) {
    const size_t csize = blocks[p] | (blocks[p + 1] << 8);
    const size_t usize = blocks[p + 2] | (blocks[p + 3] << 8);
    ASSERT_LE(p + 4 + csize, blocks.size()) << "块越界";
    std::vector<uint8_t> part(usize + 64);
    const size_t n = LzoDecompress(blocks.data() + p + 4, csize, part.data(), part.size());
    ASSERT_EQ(n, usize);
    dec.insert(dec.end(), part.begin(), part.begin() + n);
    p += 4 + csize;
    ++nblocks;
  }
  ASSERT_EQ(p, blocks.size()) << "块流尾随字节";
  EXPECT_GE(nblocks, 2) << "9000 字节应 ≥2 块（0x2000*2=16384>9000 → 1 满块 + 1 尾部）";
  EXPECT_EQ(nblocks, 2);
  EXPECT_EQ(Hex(dec), Hex(raw));
}

}  // namespace
}  // namespace mapenc
