#include "../lowlevel.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>

using namespace ra2::runtime;

TEST(Runtime, StrncpyPadsAndTerminates) {
    char output[8];
    EXPECT_EQ(strncpy_legacy(output, "ab", sizeof output), output);
    EXPECT_EQ(std::memcmp(output, "ab\0\0\0\0\0\0", sizeof output), 0);
}

TEST(Runtime, StrncpyDoesNotWriteWhenCountIsZero) {
    char output[4] = {'x', 'x', 'x', 'x'};
    strncpy_legacy(output, "abc", 0);
    EXPECT_EQ(output[0], 'x');
}

TEST(Runtime, AssemblePath) {
    EXPECT_EQ(assemble_path("C", "games", "map", "bin"), "C:games\\map.bin");
    EXPECT_EQ(assemble_path("", "games\\", "map", ".bin"), "games\\map.bin");
    EXPECT_EQ(assemble_path("", "", "map", ""), "map");
}

TEST(Runtime, FindsBackwardSingleByteBoundary) {
    const unsigned char path[] = {'a', '/', 'b', '\0'};
    EXPECT_EQ(backward_multibyte_boundary_legacy(path, path + 3, false, {}), path + 2);
    EXPECT_EQ(backward_multibyte_boundary_legacy(path, path, false, {}), nullptr);
}

TEST(Runtime, FindsBackwardMultibyteBoundaryWithLeadByteTable) {
    const unsigned char path[] = {'a', 0x81, 0x40, '/', 'b', '\0'};
    const auto is_lead = [](unsigned char c) { return c == 0x81; };
    EXPECT_EQ(backward_multibyte_boundary_legacy(path, path + 5, true, is_lead), path + 4);
    EXPECT_EQ(backward_multibyte_boundary_legacy(path, path + 2, true, is_lead), path);
    EXPECT_EQ(backward_multibyte_boundary_legacy(path, path + 1, true, is_lead), path);
}

TEST(Runtime, ScanToken) {
    char text[] = "a,b,,c";
    char* cursor = nullptr;
    EXPECT_STREQ(scan_token(text, ",", cursor), "a");
    EXPECT_STREQ(scan_token(nullptr, ",", cursor), "b");
    EXPECT_STREQ(scan_token(nullptr, ",", cursor), "c");
    EXPECT_EQ(scan_token(nullptr, ",", cursor), nullptr);
}

TEST(Runtime, ParsesSignedDecimalPrefix) {
    EXPECT_EQ(parse_decimal_legacy(" \t-123tail"), -123);
    EXPECT_EQ(parse_decimal_legacy("+42"), 42);
    EXPECT_EQ(parse_decimal_legacy("tail"), 0);
}

TEST(Runtime, ForwardsNumericFormatWithLegacyUnsignedFlag) {
    int seen_value = 0;
    int seen_output = 0;
    int seen_base = 0;
    bool seen_unsigned = false;
    NumericFormatForward formatter{[&](int value, int output, int base, bool unsigned_value) {
        seen_value = value;
        seen_output = output;
        seen_base = base;
        seen_unsigned = unsigned_value;
    }};
    EXPECT_EQ(forward_numeric_format_legacy(-7, 11, 10, formatter), 11);
    EXPECT_EQ(seen_value, -7);
    EXPECT_EQ(seen_output, 11);
    EXPECT_EQ(seen_base, 10);
    EXPECT_TRUE(seen_unsigned);
    EXPECT_EQ(forward_numeric_format_legacy(-7, 11, 16, formatter), 11);
    EXPECT_FALSE(seen_unsigned);
}

TEST(Runtime, BinarySearchReturnsPayloadForLastIndexRange) {
    const int first = 1;
    const int second = 2;
    const int third = 3;
    const SearchEntry entries[] = {{"alpha", &first}, {"bravo", &second}, {"charlie", &third}};
    const void* output = nullptr;
    binary_search_legacy(entries, 2, "BRAVO", &output);
    EXPECT_EQ(output, &second);
    binary_search_legacy(entries, 2, "charlie", &output);
    EXPECT_EQ(output, &third);
    binary_search_legacy(entries, 2, "delta", &output);
    EXPECT_EQ(output, &third);
    binary_search_legacy(entries, -1, "alpha", &output);
}

TEST(Runtime, InitializesLocaleFlagsAndCodePage) {
    LocaleInitState state;
    initialize_locale_legacy(state, "eng", "english", 1252, [] { return 0x307u; });
    EXPECT_EQ(state.short_name, 1u);
    EXPECT_EQ(state.default_name, 0u);
    EXPECT_EQ(state.code_page, 2u);
    EXPECT_EQ(state.locale_flags, 0x307u);

    initialize_locale_legacy(state, "english", "eng", 936, [] { return 0x100u; });
    EXPECT_EQ(state.short_name, 0u);
    EXPECT_EQ(state.code_page, 936u);
    EXPECT_EQ(state.locale_flags, 0u);
}

TEST(Runtime, CreatesHeapSegmentWithInjectedVirtualMemory) {
    HeapSegmentState segments[2];
    int metadata = 0;
    int address = 0;
    bool grew = false;
    HeapSegmentAllocator allocator;
    allocator.capacity = 1;
    allocator.count = 1;
    allocator.allocate_metadata = [&](std::size_t size) {
        EXPECT_EQ(size, 0x41c4u);
        return static_cast<void*>(&metadata);
    };
    allocator.reserve_address_space = [&](std::size_t size) {
        EXPECT_EQ(size, 0x100000u);
        return static_cast<void*>(&address);
    };
    allocator.grow_records = [&](HeapSegmentState* current, std::size_t size) {
        EXPECT_EQ(current, segments);
        EXPECT_EQ(size, 17u);
        grew = true;
        return current;
    };
    EXPECT_EQ(create_heap_segment_legacy(segments, allocator), segments + 1);
    EXPECT_TRUE(grew);
    EXPECT_EQ(allocator.count, 2u);
    EXPECT_EQ(segments[1].metadata, &metadata);
    EXPECT_EQ(segments[1].address, &address);
    EXPECT_EQ(segments[1].bitmap, 0xffffffffu);
}

TEST(Runtime, ReleasesMetadataWhenHeapAddressReservationFails) {
    HeapSegmentState segments[1];
    int metadata = 0;
    bool freed = false;
    HeapSegmentAllocator allocator;
    allocator.capacity = 1;
    allocator.allocate_metadata = [&](std::size_t) { return static_cast<void*>(&metadata); };
    allocator.reserve_address_space = [&](std::size_t) { return static_cast<void*>(nullptr); };
    allocator.free_metadata = [&](void* value) {
        EXPECT_EQ(value, &metadata);
        freed = true;
    };
    EXPECT_EQ(create_heap_segment_legacy(segments, allocator), nullptr);
    EXPECT_TRUE(freed);
    EXPECT_EQ(allocator.count, 0u);
}

TEST(Runtime, UnsignedRemainder) {
    EXPECT_EQ(unsigned_remainder(0x100000001ull, 7), 5ull);
    EXPECT_EQ(unsigned_remainder(0xffffffffffffffffull, 0x100000001ull), 0ull);
}

TEST(Runtime, MemmoveHandlesOverlap) {
    char text[] = "abcdef";
    EXPECT_EQ(memmove_legacy(text + 2, text, 4), text + 2);
    EXPECT_STREQ(text, "ababcd");

    char reverse[] = "abcdef";
    EXPECT_EQ(memmove_legacy(reverse, reverse + 2, 4), reverse);
    EXPECT_STREQ(reverse, "cdefef");
}

TEST(Runtime, StringCopyCopiesTerminator) {
    char output[8] = {};
    EXPECT_EQ(string_copy_legacy(output, "abc"), output);
    EXPECT_STREQ(output, "abc");
    EXPECT_EQ(output[3], '\0');
}

TEST(Runtime, StringLengthAndLeadingAlphaMatchLegacyScans) {
    const char terminated[] = {'a', 'b', 'c', '\0', 'i', 'g', 'n', 'o', 'r', 'e', 'd', '\0'};
    EXPECT_EQ(string_length_legacy(terminated), 3u);
    EXPECT_EQ(leading_alpha_length_legacy("Abc123"), 3u);
    EXPECT_EQ(leading_alpha_length_legacy("123"), 0u);
    EXPECT_EQ(leading_alpha_length_legacy("abc"), 3u);
}

TEST(Runtime, FindsFixedSizeRuntimeRecord) {
    const RuntimeRecord records[] = {{1, 10, 100}, {2, 20, 200}, {3, 30, 300}};
    const RuntimeRecord* found = find_runtime_record_legacy(records, 3, 20);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found, records + 1);
    EXPECT_EQ(found->value, 2u);
    EXPECT_EQ(find_runtime_record_legacy(records, 3, 99), nullptr);
    EXPECT_EQ(find_runtime_record_legacy(nullptr, 3, 20), nullptr);
}

TEST(Runtime, FindsContainingHeapSegmentRange) {
    const HeapSegmentRange segments[] = {{0x10000000u}, {0x20000000u}};
    EXPECT_EQ(find_heap_segment_legacy(segments, 2, 0x10000000u), segments);
    EXPECT_EQ(find_heap_segment_legacy(segments, 2, 0x100fffffu), segments);
    EXPECT_EQ(find_heap_segment_legacy(segments, 2, 0x10100000u), nullptr);
    EXPECT_EQ(find_heap_segment_legacy(segments, 2, 0x20000100u), segments + 1);
    EXPECT_EQ(find_heap_segment_legacy(nullptr, 2, 0x10000000u), nullptr);
}

TEST(Runtime, InvokesAllocationRetryCallback) {
    std::size_t observed_size = 0;
    EXPECT_TRUE(invoke_allocation_retry_legacy([&](std::size_t size) {
        observed_size = size;
        return 7;
    }, 4096));
    EXPECT_EQ(observed_size, 4096u);
    EXPECT_FALSE(invoke_allocation_retry_legacy([](std::size_t) { return 0; }, 1));
    EXPECT_FALSE(invoke_allocation_retry_legacy({}, 1));
}

TEST(Runtime, GuardsWideCharacterConversionWithReferenceOrLock) {
    char output[2] = {};
    ConversionGuardState state;
    int lock_count = 0;
    int unlock_count = 0;
    EXPECT_EQ(guarded_wide_char_to_byte_legacy(output, L'A', false, state,
                                                [&] { ++lock_count; }, [&] { ++unlock_count; }), 1);
    EXPECT_EQ(state.references, 0u);
    EXPECT_EQ(lock_count, 0);
    EXPECT_EQ(unlock_count, 0);

    state.locking_enabled = true;
    EXPECT_EQ(guarded_wide_char_to_byte_legacy(output, L'B', false, state,
                                                [&] { ++lock_count; }, [&] { ++unlock_count; }), 1);
    EXPECT_EQ(state.references, 0u);
    EXPECT_EQ(lock_count, 1);
    EXPECT_EQ(unlock_count, 1);
}

TEST(Runtime, MemsetFillsLowByteAndReturnsDestination) {
    unsigned char output[6] = {};
    EXPECT_EQ(memset_legacy(output, 0x1234, sizeof output), output);
    for (const unsigned char value : output) EXPECT_EQ(value, 0x34u);
    EXPECT_EQ(memset_legacy(output, 0, 0), output);
}

TEST(Runtime, ParsesLegacyHexAccumulation) {
    EXPECT_EQ(parse_hex_legacy("0"), 0);
    EXPECT_EQ(parse_hex_legacy("A"), 10);
    EXPECT_EQ(parse_hex_legacy("1A"), 26);
    EXPECT_EQ(parse_hex_legacy(""), 0);
}

TEST(Runtime, MapsOsErrorsToCrtState) {
    map_os_error(5);
    EXPECT_EQ(current_error_state().dos_error, 5u);
    EXPECT_EQ(current_error_state().errno_value, 22u);

    map_os_error(32);
    EXPECT_EQ(current_error_state().errno_value, 13u);

    map_os_error(200);
    EXPECT_EQ(current_error_state().errno_value, 8u);
}

TEST(Runtime, MapsOsErrorRangesWithExclusiveAssemblyBoundaries) {
    map_os_error(19);
    EXPECT_EQ(current_error_state().errno_value, 22u);
    map_os_error(20);
    EXPECT_EQ(current_error_state().errno_value, 13u);
    map_os_error(36);
    EXPECT_EQ(current_error_state().errno_value, 13u);
    map_os_error(37);
    EXPECT_EQ(current_error_state().errno_value, 22u);

    map_os_error(187);
    EXPECT_EQ(current_error_state().errno_value, 22u);
    map_os_error(188);
    EXPECT_EQ(current_error_state().errno_value, 8u);
    map_os_error(202);
    EXPECT_EQ(current_error_state().errno_value, 8u);
    map_os_error(203);
    EXPECT_EQ(current_error_state().errno_value, 22u);
}

TEST(Runtime, ExposesThreadLocalErrorSlots) {
    current_error_state().errno_value = 11;
    current_error_state().dos_error = 22;
    EXPECT_EQ(errno_slot_legacy(), &current_error_state().errno_value);
    EXPECT_EQ(dos_error_slot_legacy(), &current_error_state().dos_error);
    *errno_slot_legacy() = 33;
    EXPECT_EQ(current_error_state().errno_value, 33u);
}

TEST(Runtime, RecognizesNtPlatformOnlyAfterSuccessfulQuery) {
    EXPECT_TRUE(is_win32_nt_platform_legacy(true, 2));
    EXPECT_FALSE(is_win32_nt_platform_legacy(false, 2));
    EXPECT_FALSE(is_win32_nt_platform_legacy(true, 1));
}

TEST(Runtime, ConvertsSingleByteWideCharacter) {
    char output[8] = {};
    EXPECT_EQ(wide_char_to_byte(output, L'A', false), 1);
    EXPECT_EQ(output[0], 'A');
    EXPECT_EQ(wide_char_to_byte(output, L'\u03A9', false), -1);
    EXPECT_EQ(current_error_state().errno_value, 42u);
}

TEST(Runtime, ConvertsByteToWideCharacter) {
    wchar_t output[2] = {};
    EXPECT_EQ(byte_to_wide(output, "A", 1, false), 1);
    EXPECT_EQ(output[0], L'A');
    EXPECT_EQ(byte_to_wide(output, "", 1, false), 0);
    EXPECT_EQ(output[0], L'\0');
}

TEST(Runtime, ClassifiesDoubleLikeOriginal) {
    EXPECT_EQ(classify_double_legacy(0.0), 0x40);
    EXPECT_EQ(classify_double_legacy(-0.0), 0x20);
    EXPECT_EQ(classify_double_legacy(1.0), 0x100);
    EXPECT_EQ(classify_double_legacy(-1.0), 0x108);
    EXPECT_EQ(classify_double_legacy(std::numeric_limits<double>::infinity()), 1);
    EXPECT_EQ(classify_double_legacy(std::numeric_limits<double>::quiet_NaN()), 2);
}

TEST(Runtime, CopysignPreservesMagnitudeAndCopiesSignBit) {
    EXPECT_EQ(copysign_legacy(3.5, 1.0), 3.5);
    EXPECT_EQ(copysign_legacy(3.5, -1.0), -3.5);
    EXPECT_EQ(copysign_legacy(-3.5, 1.0), 3.5);
    EXPECT_EQ(copysign_legacy(-3.5, -1.0), -3.5);
    EXPECT_EQ(copysign_legacy(0.0, -1.0), -0.0);
    EXPECT_EQ(copysign_legacy(-0.0, 1.0), 0.0);
    EXPECT_TRUE(std::isinf(copysign_legacy(std::numeric_limits<double>::infinity(), -1.0)));
    EXPECT_TRUE(std::signbit(copysign_legacy(std::numeric_limits<double>::quiet_NaN(), -1.0)));
}

TEST(Runtime, ClassifiesSpecialDoubleEncodings) {
    EXPECT_EQ(classify_special_double_legacy(1.0), 0);
    EXPECT_EQ(classify_special_double_legacy(std::numeric_limits<double>::infinity()), 1);
    EXPECT_EQ(classify_special_double_legacy(-std::numeric_limits<double>::infinity()), 2);
    EXPECT_EQ(classify_special_double_legacy(std::numeric_limits<double>::quiet_NaN()), 3);
}
