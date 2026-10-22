#include "../crt_init.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

std::vector<int> g_calls;

void record_one() { g_calls.push_back(1); }
void record_two() { g_calls.push_back(2); }
void record_three() { g_calls.push_back(3); }

class CrtInitTest : public ::testing::Test {
 protected:
    void SetUp() override { g_calls.clear(); }
};

}  // namespace

TEST_F(CrtInitTest, InittermCallsEveryNonNullEntry) {
    const ra2::crt::InitEntry table[] = {record_one, nullptr, record_two};
    ra2::crt::initterm_legacy(table, table + 3);
    EXPECT_EQ(g_calls, (std::vector<int>{1, 2}));
}

TEST_F(CrtInitTest, InittermPreservesEntryOrder) {
    const ra2::crt::InitEntry table[] = {record_three, record_one, record_two};
    ra2::crt::initterm_legacy(table, table + 3);
    EXPECT_EQ(g_calls, (std::vector<int>{3, 1, 2}));
}

TEST_F(CrtInitTest, InittermOnEmptyRangeCallsNothing) {
    const ra2::crt::InitEntry table[] = {record_one};
    ra2::crt::initterm_legacy(table, table);
    EXPECT_TRUE(g_calls.empty());
}

TEST_F(CrtInitTest, InittermOnNullRangeCallsNothing) {
    ra2::crt::initterm_legacy(nullptr, nullptr);
    EXPECT_TRUE(g_calls.empty());
}

// 0x7CBED3 tests the cursor against the table end with an unsigned "above or
// equal" jump, so an inverted range must stop immediately rather than run past
// the begin pointer.
TEST_F(CrtInitTest, InittermOnInvertedRangeStopsWithoutCalling) {
    const ra2::crt::InitEntry table[] = {record_one, record_two};
    ra2::crt::initterm_legacy(table + 2, table);
    EXPECT_TRUE(g_calls.empty());
}

TEST_F(CrtInitTest, InittermAllNullTableCallsNothing) {
    const ra2::crt::InitEntry table[] = {nullptr, nullptr, nullptr};
    ra2::crt::initterm_legacy(table, table + 3);
    EXPECT_TRUE(g_calls.empty());
}

TEST_F(CrtInitTest, CinitRunsEarlyHookThenHighTableThenLowTable) {
    std::vector<int> order;
    const ra2::crt::InitEntry low[] = {record_one};
    const ra2::crt::InitEntry high[] = {record_two, record_three};
    ra2::crt::CinitTables tables;
    tables.ctor_low_begin = low;
    tables.ctor_low_end = low + 1;
    tables.ctor_high_begin = high;
    tables.ctor_high_end = high + 2;

    ra2::crt::cinit_legacy(tables, [&order] { order.push_back(0); });

    // g_calls holds the entry calls; the hook fired before any of them.
    EXPECT_EQ(g_calls, (std::vector<int>{2, 3, 1}));
    EXPECT_EQ(order, (std::vector<int>{0}));
}

TEST_F(CrtInitTest, CinitSkipsHookWhenGlobalIsNull) {
    const ra2::crt::InitEntry low[] = {record_one};
    ra2::crt::CinitTables tables;
    tables.ctor_low_begin = low;
    tables.ctor_low_end = low + 1;
    ra2::crt::cinit_legacy(tables, nullptr);
    EXPECT_EQ(g_calls, (std::vector<int>{1}));
}

TEST_F(CrtInitTest, EnvScanCountsPlainSegments) {
    EXPECT_EQ(ra2::crt::scan_env_segments_legacy("A=1\0B=2\0C=3\0"), 3u);
}

TEST_F(CrtInitTest, EnvScanSkipsSegmentsStartingWithEquals) {
    // The original only inspects each segment's first character, so "=1" is
    // dropped while "A==1" is still counted once.
    EXPECT_EQ(ra2::crt::scan_env_segments_legacy("=1\0A=2\0"), 1u);
    EXPECT_EQ(ra2::crt::scan_env_segments_legacy("A==1\0"), 1u);
}

TEST_F(CrtInitTest, EnvScanOnEmptyStringCountsNothing) {
    EXPECT_EQ(ra2::crt::scan_env_segments_legacy(""), 0u);
}

TEST_F(CrtInitTest, EnvScanOnAllEqualsStringCountsNothing) {
    EXPECT_EQ(ra2::crt::scan_env_segments_legacy("=1\0=2\0"), 0u);
}

TEST(CrtInitTestEnvBytes, PointerArrayCarriesOneExtraDword) {
    EXPECT_EQ(ra2::crt::env_pointer_array_bytes(0), 4u);
    EXPECT_EQ(ra2::crt::env_pointer_array_bytes(1), 8u);
    EXPECT_EQ(ra2::crt::env_pointer_array_bytes(7), 32u);
}