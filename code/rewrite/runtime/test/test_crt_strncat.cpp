#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "crt_strncat.h"
#include "gtest/gtest.h"

namespace {

struct GoldenCase {
    std::string dst_before;
    std::string src;
    std::size_t count;
    std::string dst_after;  // 0x50 字节全量（hex 解码后）
};

std::vector<GoldenCase> LoadGolden() {
    std::vector<GoldenCase> out;
    std::string path = std::string(RA2_GOLDEN_DIR);
    path += "/deadcode_7cb550_golden.txt";
    std::ifstream in(path);
    EXPECT_TRUE(in.good()) << "cannot open golden: " << path;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();  // Windows text 模式 golden 行尾 CR
        }
        if (line.rfind("CASE dst=", 0) != 0) {
            continue;
        }
        // CASE dst=<hex 空格分隔> src=<hex 空格分隔> count=<dec> -> off=<dec> DST=<hex 连续>
        const std::size_t src_pos = line.find(" src=");
        const std::size_t cnt_pos = line.find(" count=");
        const std::size_t arr_pos = line.find(" -> off=");
        const std::size_t dst_pos = line.find(" DST=");
        if (src_pos == std::string::npos || cnt_pos == std::string::npos
            || arr_pos == std::string::npos
            || dst_pos == std::string::npos) {
            continue;
        }
        auto parse_hex_bytes = [](const std::string& hex,
                                   const char sep) -> std::string {
            std::string out_bytes;
            if (sep == ' ') {
                // 空格分隔逐字节
                std::size_t i = 0;
                while (i < hex.size()) {
                    if (hex[i] == ' ') {
                        ++i;
                        continue;
                    }
                    if (i + 1 < hex.size()) {
                        out_bytes.push_back(
                            static_cast<char>(std::stoi(hex.substr(i, 2),
                                                         nullptr, 16)));
                        i += 2;
                    } else {
                        break;
                    }
                }
            } else {
                for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
                    out_bytes.push_back(
                        static_cast<char>(std::stoi(hex.substr(i, 2),
                                                     nullptr, 16)));
                }
            }
            return out_bytes;
        };
        GoldenCase c;
        // "CASE dst=" 前缀 9 字符（CASE 空格 dst =），内容从 9 起
        c.dst_before = parse_hex_bytes(
            line.substr(9, src_pos - 9), ' ');
        c.src = parse_hex_bytes(
            line.substr(src_pos + 5, cnt_pos - (src_pos + 5)), ' ');
        const std::string cnt_s = line.substr(cnt_pos + 7,
                                               arr_pos - (cnt_pos + 7));
        c.count = static_cast<std::size_t>(std::stoull(cnt_s));
        c.dst_after = parse_hex_bytes(
            line.substr(dst_pos + 5), 0);
        out.push_back(c);
    }
    return out;
}

}  // namespace

// 与 Unicorn 真身逐位对拍（golden 基准）——注意 dst 之后 0x50 字节
// 全量比对（含 NUL 填充区与写越界检查区，任何越界写都会被抓到）
TEST(CrtStrncat, GoldenMatches) {
    const std::vector<GoldenCase> cases = LoadGolden();
    ASSERT_FALSE(cases.empty());
    for (const GoldenCase& c : cases) {
        // ★与 golden 采集环境一致：dst 区前 0x50 字节零填充（采集时
        // dst_bytes + bytes(0x60)），0x50-0x60 填 0xCC 作越界警戒区
        char buf[0x60];
        std::memset(buf, 0, 0x50);
        std::memset(buf + 0x50, 0xCC, 0x10);
        std::memcpy(buf, c.dst_before.data(), c.dst_before.size());
        // src 缓冲同样零填充（采集时 src_bytes + bytes(0x60)）
        char src_buf[0x60];
        std::memset(src_buf, 0, sizeof(src_buf));
        std::memcpy(src_buf, c.src.data(), c.src.size());

        char* ret = ra2::crt::strncat_legacy(buf, src_buf, c.count);
        EXPECT_EQ(ret, buf) << "返回值必须为 dst 指针";
        // 0x50 字节全量比对（golden 采集时读 0x50）
        EXPECT_EQ(std::memcmp(buf, c.dst_after.data(), 0x50), 0)
            << "dst 区 0x50 字节全量不一致 count=" << c.count;
        // 越界写检查：0x50 后 0x10 字节必须保持 0xCC 未动
        for (std::size_t i = 0x50; i < 0x60; ++i) {
            EXPECT_EQ(static_cast<unsigned char>(buf[i]), 0xCCu)
                << "越界写 detected at " << i << " (count=" << c.count
                << ")";
        }
    }
}

// 边界用例（golden 之外的补充——标准语义与行为约束）
TEST(CrtStrncat, BoundaryCases) {
    using ra2::crt::strncat_legacy;
    char buf[0x40];

    // count==0：dst 完全不动（原版早退，与 wcsncat 不同）
    std::memset(buf, 0xCC, sizeof(buf));
    std::strcpy(buf, "abc");
    char* r = strncat_legacy(buf, "XYZ", 0);
    EXPECT_EQ(r, buf);
    EXPECT_EQ(std::strcmp(buf, "abc"), 0);
    EXPECT_EQ(static_cast<unsigned char>(buf[3]), 0u);  // NUL 保持原位
    EXPECT_EQ(static_cast<unsigned char>(buf[4]), 0xCCu);  // 之后未动

    // 标准追加 + 恒补 NUL
    std::strcpy(buf, "abc");
    strncat_legacy(buf, "def", 3);
    EXPECT_STREQ(buf, "abcdef");

    // src NUL 提前终止（count 充裕）
    std::strcpy(buf, "abc");
    strncat_legacy(buf, "de", 10);
    EXPECT_STREQ(buf, "abcde");

    // dst 空串（dst[0] 即 NUL）
    buf[0] = 0;
    strncat_legacy(buf, "XY", 2);
    EXPECT_STREQ(buf, "XY");

    // src 空串 → 直接补 NUL（dst 已含 NUL，结果不变）
    std::strcpy(buf, "abc");
    strncat_legacy(buf, "", 5);
    EXPECT_STREQ(buf, "abc");

    // 追加后 count 恰好等于 src 长度（无 NUL 补充前的原始 NUL 保留在 src 内）
    std::strcpy(buf, "abc");
    strncat_legacy(buf, "de", 2);
    EXPECT_STREQ(buf, "abcde");
}

// 与标准库 strncat 交叉抽查（行为语义等价，除 count==0 的边界差
// 异——标准库 count==0 也是 no-op，一致）
TEST(CrtStrncat, ReferenceCrossCheck) {
    using ra2::crt::strncat_legacy;
    struct Case {
        const char* dst;
        const char* src;
        std::size_t count;
    };
    const Case cases[] = {
        {"abc", "XYZ", 3},
        {"abc", "XYZ", 2},
        {"abc", "XYZ", 100},
        {"", "XYZ", 3},
        {"", "XYZ", 0},
        {"abcdef", "GH", 1},
        {"abcdef", "GH", 2},
        {"abcdef", "GH", 100},
        {"The quick brown fox", " jumps", 6},
        {"The quick brown fox", " jumps", 3},
        {"The quick brown fox", " jumps", 100},
        {"A", "", 5},
        {"AAAAAAAAAAAAAAAA", "BBBBBBBBBBBBBBBB", 16},
        {"AAAAAAAAAAAAAAAA", "BBBBBBBBBBBBBBBB", 17},
        {"AAAAAAAAAAAAAAAA", "BBBBBBBBBBBBBBBB", 1},
    };
    for (const Case& c : cases) {
        char a[0x40];
        char b[0x40];
        std::memset(a, 0xCC, sizeof(a));
        std::memset(b, 0xCC, sizeof(b));
        std::strcpy(a, c.dst);
        std::strcpy(b, c.dst);
        ra2::crt::strncat_legacy(a, c.src, c.count);
        std::strncat(b, c.src, c.count);
        EXPECT_EQ(std::strcmp(a, b), 0)
            << "dst=" << c.dst << " src=" << c.src << " count=" << c.count
            << " got=" << a << " expect=" << b;
    }
}