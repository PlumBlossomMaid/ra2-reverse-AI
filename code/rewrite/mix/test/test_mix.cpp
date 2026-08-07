// .mix / .tmp 只读解析器的回归
//
// 分两层：
//   1) 不碰游戏数据的纯算法层——Blowfish 标准测试向量、名字哈希的已知值。
//      这一层在任何机器上都跑。
//   2) 真档层——需要环境变量 RA2_GAME_DIR 指向原版目录，否则整段 SKIP。
//      仓库里不放任何游戏数据，所以 CI 上这一层是跳过的。
#include "mix_archive.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "blowfish.h"
#include "gtest/gtest.h"
#include "tmp_file.h"

namespace ra2 {
namespace mix {
namespace {

std::vector<std::uint8_t> Hex(const char* s) {
  std::vector<std::uint8_t> out;
  for (const char* p = s; p[0] && p[1]; p += 2) {
    auto v = [](char c) -> unsigned {
      return (c >= '0' && c <= '9') ? unsigned(c - '0')
                                    : unsigned((c | 32) - 'a' + 10);
    };
    out.push_back(static_cast<std::uint8_t>((v(p[0]) << 4) | v(p[1])));
  }
  return out;
}

std::string HexStr(const std::uint8_t* p, std::size_t n) {
  std::string s;
  char buf[3];
  for (std::size_t i = 0; i < n; ++i) {
    std::snprintf(buf, sizeof(buf), "%02X", p[i]);
    s += buf;
  }
  return s;
}

// Eric Young 那套公开的 Blowfish ECB 测试向量。表里的常量是 pi 算出来的
// （见 code/mix/gen_blowfish_tables.py），能对上这些向量说明 pi 与轮函数都对。
TEST(Blowfish, StandardVectors) {
  struct V { const char* key; const char* clear; const char* cipher; };
  const V vs[] = {
      {"0000000000000000", "0000000000000000", "4EF997456198DD78"},
      {"FFFFFFFFFFFFFFFF", "FFFFFFFFFFFFFFFF", "51866FD5B85ECB8A"},
      {"3000000000000000", "1000000000000001", "7D856F9A613063F2"},
      {"0123456789ABCDEF", "1111111111111111", "61F9C3802281B096"},
      {"FEDCBA9876543210", "0123456789ABCDEF", "0ACEAB0FC6A0A28D"},
  };
  for (const V& v : vs) {
    const std::vector<std::uint8_t> key = Hex(v.key);
    const std::vector<std::uint8_t> clear = Hex(v.clear);
    const std::vector<std::uint8_t> cipher = Hex(v.cipher);
    const Blowfish bf(&key[0], key.size());
    std::uint32_t l = 0, r = 0;
    for (int i = 0; i < 4; ++i) l = (l << 8) | clear[i];
    for (int i = 4; i < 8; ++i) r = (r << 8) | clear[i];
    bf.EncryptBlock(&l, &r);
    std::uint8_t got[8];
    for (int i = 0; i < 4; ++i) got[3 - i] = static_cast<std::uint8_t>(l >> (8 * i));
    for (int i = 0; i < 4; ++i) got[7 - i] = static_cast<std::uint8_t>(r >> (8 * i));
    EXPECT_EQ(std::string(v.cipher), HexStr(got, 8)) << "key=" << v.key;
    // 再解回去
    std::uint8_t back[8];
    bf.DecryptEcb(&cipher[0], back, 8);
    EXPECT_EQ(std::string(v.clear), HexStr(back, 8)) << "key=" << v.key;
  }
}

// 56 字节密钥（mix 用的就是这个长度）
TEST(Blowfish, LongKey) {
  std::vector<std::uint8_t> key(56);
  for (int i = 0; i < 56; ++i) key[i] = static_cast<std::uint8_t>(i + 1);
  const Blowfish bf(&key[0], key.size());
  const std::vector<std::uint8_t> clear = Hex("FEDCBA9876543210");
  std::uint32_t l = 0, r = 0;
  for (int i = 0; i < 4; ++i) l = (l << 8) | clear[i];
  for (int i = 4; i < 8; ++i) r = (r << 8) | clear[i];
  bf.EncryptBlock(&l, &r);
  std::uint8_t got[8];
  for (int i = 0; i < 4; ++i) got[3 - i] = static_cast<std::uint8_t>(l >> (8 * i));
  for (int i = 0; i < 4; ++i) got[7 - i] = static_cast<std::uint8_t>(r >> (8 * i));
  EXPECT_EQ(std::string("E64E22EA335ACD61"), HexStr(got, 8));
}

// 名字哈希：判据见 docs/mix-format/README.md ——langmd.mix 里 id=0xBD835079
// 的条目内容以 " FSC"（CSF 魔数）开头，而 "RA2MD.CSF" 的哈希正是这个值。
TEST(MixArchive, NameHash) {
  EXPECT_EQ(0xBD835079u, MixId("ra2md.csf"));
  EXPECT_EQ(MixId("RA2MD.CSF"), MixId("ra2md.csf"));   // 大小写不敏感
  // 长度正好是 4 的倍数时不补齐
  EXPECT_EQ(MixId("abcd"), MixId("ABCD"));
}

// —— 下面是真档层 ——
const char* GameDir() { return std::getenv("RA2_GAME_DIR"); }

bool ReadFile(const std::string& path, std::vector<std::uint8_t>* out) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return false;
  std::fseek(f, 0, SEEK_END);
  const long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  out->assign(static_cast<std::size_t>(n), 0);
  const std::size_t got = n > 0 ? std::fread(&(*out)[0], 1, static_cast<std::size_t>(n), f) : 0;
  std::fclose(f);
  return got == out->size();
}

TEST(MixArchive, RealArchive) {
  const char* dir = GameDir();
  if (dir == nullptr) GTEST_SKIP() << "未设置 RA2_GAME_DIR，跳过真档层";
  std::vector<std::uint8_t> raw;
  ASSERT_TRUE(ReadFile(std::string(dir) + "/ra2.mix", &raw)) << "读不到 ra2.mix";

  MixArchive ra2;
  ASSERT_TRUE(ra2.Open(std::move(raw)));
  EXPECT_TRUE(ra2.encrypted());     // ra2.mix 的头是加密的
  EXPECT_TRUE(ra2.checksum());
  EXPECT_TRUE(ra2.Has("isotemp.mix"));
  EXPECT_TRUE(ra2.Has("isosnow.mix"));

  MixArchive iso;
  ASSERT_TRUE(iso.OpenNested(ra2, "isotemp.mix"));
  EXPECT_FALSE(iso.encrypted());    // 内层砖库不加密
  EXPECT_GT(iso.Count(), 1000u);

  // 三块砖的尺寸，见 docs/mix-format/README.md「.tmp 砖块文件头」
  struct T { const char* name; int w; int h; int holes; };
  const T ts[] = {{"water09.tem", 1, 1, 0},
                  {"shore04.tem", 1, 2, 0},
                  {"cliff01.tem", 2, 3, 2}};
  for (const T& t : ts) {
    std::size_t sz = 0;
    const std::uint8_t* p = iso.Find(t.name, &sz);
    ASSERT_NE(nullptr, p) << t.name;
    TmpTile tile;
    ASSERT_TRUE(ParseTmp(p, sz, &tile)) << t.name;
    EXPECT_EQ(t.w, tile.blocks_x) << t.name;
    EXPECT_EQ(t.h, tile.blocks_y) << t.name;
    EXPECT_EQ(60, tile.pixel_w) << t.name;
    EXPECT_EQ(30, tile.pixel_h) << t.name;
    int holes = 0;
    for (std::size_t i = 0; i < tile.sub.size(); ++i) {
      if (!tile.sub[i].present) ++holes;
    }
    EXPECT_EQ(t.holes, holes) << t.name;
  }
}

// ra2md.mix 的头同样加密，且内嵌 localmd.mix（theater ini 在里面）
TEST(MixArchive, RealArchiveMd) {
  const char* dir = GameDir();
  if (dir == nullptr) GTEST_SKIP() << "未设置 RA2_GAME_DIR，跳过真档层";
  std::vector<std::uint8_t> raw;
  ASSERT_TRUE(ReadFile(std::string(dir) + "/ra2md.mix", &raw));
  MixArchive md;
  ASSERT_TRUE(md.Open(std::move(raw)));
  EXPECT_TRUE(md.encrypted());
  ASSERT_TRUE(md.Has("localmd.mix"));
  MixArchive local;
  ASSERT_TRUE(local.OpenNested(md, "localmd.mix"));
  std::size_t sz = 0;
  EXPECT_NE(nullptr, local.Find("temperatmd.ini", &sz));
  EXPECT_GT(sz, 1000u);
  EXPECT_NE(nullptr, local.Find("snowmd.ini", &sz));
}

}  // namespace
}  // namespace mix
}  // namespace ra2
