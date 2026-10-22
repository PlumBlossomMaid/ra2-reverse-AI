#include "../crt_error.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// golden 由 Python rd32 打印，是无符号十进制（-998 之类已按 uint32 展开）。
// 必须先按 uint32_t 解析再转 int32_t，否则 libstdc++ 会把超 INT32_MAX 的
// 字符串置成 INT_MAX（batch 936 踩坑）。
std::uint32_t parse_u32(const std::string& tok) {
    return static_cast<std::uint32_t>(std::stoull(tok));
}

// CTOR 输入的 b 参数：emu 里 "null"=0、"fake"=0x4011A0 附近的假对象指针。
std::uint32_t parse_arg(const std::string& tok) {
    if (tok == "null") return 0;
    if (tok == "fake") return 1074896416u;  // golden 写入的四字节值
    return parse_u32(tok);
}

std::vector<std::string> tokens_of(const std::string& line) {
    std::istringstream iss(line);
    std::vector<std::string> toks;
    std::string t;
    while (iss >> t) toks.push_back(t);
    return toks;
}

class RecorderWorld : public ra2::crt::CrtThrowWorld {
 public:
    std::vector<ra2::crt::CrtExceptionObject> thrown;
    std::vector<std::uint32_t> throw_infos;
    std::vector<std::uint32_t> released;

    void CxxThrow(const ra2::crt::CrtExceptionObject& obj,
                  std::uint32_t throw_info) override {
        thrown.push_back(obj);
        throw_infos.push_back(throw_info);
    }

    void ReleaseRef(std::uint32_t b) override { released.push_back(b); }
};

struct Case {
    std::string kind;
    std::uint32_t a = 0, b = 0, c = 0;
    std::uint32_t exp_vtbl = 0, exp_f4 = 0, exp_f8 = 0, exp_fc = 0;
    std::uint32_t exp_throw_info = 0;
    int exp_release = 0;
};

// golden 行样例：
//   WRAP 0 -> 8365208 0 0 0 8450256 0
//   THROWCALL 7 4660 -> 8365208 7 4660 0 8450256 0
//   CTOR 14 fake 1 -> 8365208 14 1074896416 0 1
bool read_golden(const char* path, std::vector<Case>* out) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        auto toks = tokens_of(line);
        if (toks.empty()) continue;
        Case c;
        c.kind = toks[0];
        std::size_t arrow = 0;
        for (std::size_t i = 1; i < toks.size(); ++i) {
            if (toks[i] == "->") {
                arrow = i;
                break;
            }
        }
        if (arrow == 0) return false;
        // 输入参数：WRAP 1 个 / THROWCALL 2 个 / CTOR 3 个
        if (c.kind == "WRAP") {
            c.a = parse_arg(toks[1]);
        } else if (c.kind == "THROWCALL") {
            c.a = parse_u32(toks[1]);
            c.b = parse_u32(toks[2]);
        } else if (c.kind == "CTOR") {
            c.a = parse_u32(toks[1]);
            c.b = parse_arg(toks[2]);
            c.c = parse_u32(toks[3]);
        } else {
            return false;
        }
        // 期望输出：WRAP/THROWCALL 6 列，CTOR 5 列
        const std::vector<std::string> exp(toks.begin() + 1 + arrow, toks.end());
        c.exp_vtbl = parse_u32(exp[0]);
        c.exp_f4 = parse_u32(exp[1]);
        c.exp_f8 = parse_u32(exp[2]);
        c.exp_fc = parse_u32(exp[3]);
        if (c.kind != "CTOR") {
            c.exp_throw_info = parse_u32(exp[4]);
            c.exp_release = static_cast<int>(parse_u32(exp[5]));
        } else {
            c.exp_release = static_cast<int>(parse_u32(exp[4]));
        }
        out->push_back(c);
    }
    return true;
}

}  // namespace

class CrtErrorTest : public ::testing::Test {
 protected:
    std::vector<Case> cases_;

    void SetUp() override {
        const char* path = RA2_GOLDEN_DIR "/deadcode_7dc720_golden.txt";
        ASSERT_TRUE(read_golden(path, &cases_)) << "无法读取 golden: " << path;
        ASSERT_FALSE(cases_.empty());
    }

    // 校验一次抛出：对象四字段 + ThrowInfo + 无引用计数
    void ExpectThrown(const RecorderWorld& w, const Case& c) const {
        ASSERT_EQ(w.thrown.size(), 1u);
        ASSERT_EQ(w.throw_infos.size(), 1u);
        EXPECT_EQ(w.thrown[0].vtable, c.exp_vtbl);
        EXPECT_EQ(w.thrown[0].field4, c.exp_f4);
        EXPECT_EQ(w.thrown[0].field8, c.exp_f8);
        EXPECT_EQ(w.thrown[0].field_c, c.exp_fc);
        EXPECT_EQ(w.throw_infos[0], c.exp_throw_info);
        EXPECT_TRUE(w.released.empty());
    }

    // 校验一次构造：返回对象四字段 + 引用计数次数/对象
    void ExpectCtor(const RecorderWorld& w, const Case& c,
                    const ra2::crt::CrtExceptionObject& obj) const {
        EXPECT_EQ(obj.vtable, c.exp_vtbl);
        EXPECT_EQ(obj.field4, c.exp_f4);
        EXPECT_EQ(obj.field8, c.exp_f8);
        EXPECT_EQ(obj.field_c, c.exp_fc);
        ASSERT_EQ(static_cast<int>(w.released.size()), c.exp_release);
        if (c.exp_release > 0) {
            EXPECT_EQ(w.released[0], c.b);
        }
    }
};

TEST_F(CrtErrorTest, GoldenConstantsMatchHeader) {
    // 审读金标准：期望值必须与头文件常量一致（0x7FA498 / 0x80F0D0）
    EXPECT_EQ(ra2::crt::kCrtExceptionVtable, 0x7FA498u);
    EXPECT_EQ(ra2::crt::kCrtExceptionThrowInfo, 0x80F0D0u);
}

TEST_F(CrtErrorTest, InvalidParameterForwardThrowsWithZeroSecondField) {
    // WRAP arg：f8=0、fC=0、ThrowInfo=0x80F0D0、无引用计数
    for (const Case& c : cases_) {
        if (c.kind != "WRAP") continue;
        RecorderWorld w;
        ra2::crt::invalid_parameter_forward(w, c.a);
        ExpectThrown(w, c);
        // wrapper 路径第二字段写死 0（原版 push 0）
        EXPECT_EQ(w.thrown[0].field8, 0u);
    }
}

TEST_F(CrtErrorTest, ThrowExceptionPropagatesBothFields) {
    // THROWCALL a b：f4=a、f8=b、fC=0，c 恒 0 不触发引用计数
    for (const Case& c : cases_) {
        if (c.kind != "THROWCALL") continue;
        RecorderWorld w;
        ra2::crt::crt_throw_exception(w, c.a, c.b);
        ExpectThrown(w, c);
    }
}

TEST_F(CrtErrorTest, CtorBuildsObjectAndGatesRefcountOnBAndCLowByte) {
    // CTOR：返回对象四字段；release 只在 b!=0 且 c 低字节非 0 时发生
    for (const Case& c : cases_) {
        if (c.kind != "CTOR") continue;
        RecorderWorld w;
        ra2::crt::CrtExceptionObject obj =
            ra2::crt::crt_exception_ctor(w, c.a, c.b, c.c);
        ExpectCtor(w, c, obj);
        EXPECT_TRUE(w.thrown.empty());  // ctor 本身不抛
    }
}

TEST_F(CrtErrorTest, CtorFieldCAwaysZero) {
    // 原版怪癖①：先写 +0xC=0 —— 恒 0
    RecorderWorld w;
    ra2::crt::CrtExceptionObject obj =
        ra2::crt::crt_exception_ctor(w, 1, 2, 0xFF);
    EXPECT_EQ(obj.field_c, 0u);
    EXPECT_EQ(w.released.size(), 1u);  // c 低字节非 0 且 b 非 0
}
