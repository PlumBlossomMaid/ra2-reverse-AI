#include "../handles.h"
#include "../lowlevel.h"

#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <vector>

using ra2::runtime::HandleTable;
using ra2::runtime::StreamState;
using ra2::runtime::current_error_state;

TEST(Handles, InitializesHeapStream) {
    StreamState stream;
    EXPECT_TRUE(ra2::runtime::initialize_stream(stream));
    EXPECT_EQ(stream.capacity, 0x1000u);
    EXPECT_NE(stream.current, nullptr);
    EXPECT_EQ(stream.position, 0u);
    EXPECT_NE(stream.flags & 0x8u, 0u);
}

TEST(Handles, BufferedPutByteWritesAndTracksState) {
    StreamState stream;
    stream.flags = 0x1;
    ASSERT_TRUE(ra2::runtime::initialize_stream(stream));
    EXPECT_EQ(ra2::runtime::buffered_put_byte_legacy('A', stream), 'A');
    EXPECT_EQ(stream.buffered_count, 1u);
    EXPECT_EQ(stream.flags & 0x11u, 0x1u);
    EXPECT_EQ(stream.base[0], static_cast<std::uint8_t>('A'));
}

TEST(Handles, BufferedPutByteVerifiesWideModeAndRejectsMismatch) {
    StreamState stream;
    stream.flags = 0x41;
    ASSERT_TRUE(ra2::runtime::initialize_stream(stream));
    stream.base[0] = static_cast<std::uint8_t>('A');
    stream.current = stream.base + 1;
    EXPECT_EQ(ra2::runtime::buffered_put_byte_legacy('A', stream), 'A');
    EXPECT_EQ(stream.buffered_count, 1u);
    EXPECT_EQ(ra2::runtime::buffered_put_byte_legacy('B', stream), -1);
    EXPECT_EQ(stream.buffered_count, 1u);
}

TEST(Handles, BufferedPutByteRejectsEofAndInvalidValue) {
    StreamState stream;
    stream.flags = 0x80;
    ASSERT_TRUE(ra2::runtime::initialize_stream(stream));
    stream.flags |= 0x2;
    EXPECT_EQ(ra2::runtime::buffered_put_byte_legacy('A', stream), -1);
    stream.flags &= static_cast<std::uint8_t>(~0x2u);
    EXPECT_EQ(ra2::runtime::buffered_put_byte_legacy(0xffffffffu, stream), -1);
}

TEST(Handles, BufferedPutByteRejectsFullBufferedStream) {
    StreamState stream;
    stream.flags = 0x1;
    ASSERT_TRUE(ra2::runtime::initialize_stream(stream));
    stream.current = stream.base + stream.capacity;
    stream.buffered_count = 1;
    EXPECT_EQ(ra2::runtime::buffered_put_byte_legacy('A', stream), -1);
    EXPECT_EQ(stream.current, stream.base + stream.capacity);
    EXPECT_EQ(stream.buffered_count, 1u);
}

TEST(Handles, BufferedGetByteConsumesAvailableByte) {
    std::uint8_t bytes[] = {'A', 'B'};
    std::uint8_t* current = bytes;
    std::int32_t remaining = 1;
    bool refilled = false;
    const auto refill = [&](std::uint8_t*&, std::int32_t&) {
        refilled = true;
        return -1;
    };
    EXPECT_EQ(ra2::runtime::buffered_get_byte_legacy(current, remaining, refill), 'A');
    EXPECT_EQ(current, bytes + 1);
    EXPECT_EQ(remaining, 0);
    EXPECT_FALSE(refilled);
}

TEST(Handles, BufferedGetByteDelegatesAfterExhaustion) {
    std::uint8_t* current = nullptr;
    std::int32_t remaining = 0;
    bool called = false;
    const auto refill = [&](std::uint8_t*& refill_current, std::int32_t& refill_remaining) {
        called = true;
        refill_current = reinterpret_cast<std::uint8_t*>(0x1234);
        refill_remaining = 7;
        return 0x5a;
    };
    EXPECT_EQ(ra2::runtime::buffered_get_byte_legacy(current, remaining, refill), 0x5a);
    EXPECT_TRUE(called);
    EXPECT_EQ(current, reinterpret_cast<std::uint8_t*>(0x1234));
    EXPECT_EQ(remaining, 7);
}

TEST(Handles, DispatchesValidSlot) {
    StreamState stream;
    HandleTable table(2);
    table.set_slot(1, 0x41, &stream);
    const int result = table.dispatch(1, 3, 4, [](StreamState& state, std::uintptr_t a,
                                                   std::uintptr_t b) {
        state.position = static_cast<std::uint32_t>(a + b);
        return 7;
    });
    EXPECT_EQ(result, 7);
    EXPECT_EQ(stream.position, 7u);
    EXPECT_EQ(table.query_flags(1, 0x40), 0x40);
}

TEST(Handles, RejectsInvalidSlot) {
    HandleTable table(1);
    EXPECT_EQ(table.dispatch(2, 0, 0, [](StreamState&, std::uintptr_t, std::uintptr_t) {
        return 1;
    }), -1);
    EXPECT_EQ(table.invalid_handle_errors(), 1u);
}

TEST(Handles, SeeksValidSlotAndTracksErrors) {
    StreamState stream;
    HandleTable table(1);
    table.set_slot(0, 0x1, &stream);

    EXPECT_EQ(table.seek(0, -4, 2, [](StreamState& state, std::int32_t offset,
                                      std::uint32_t origin) {
        EXPECT_EQ(offset, -4);
        EXPECT_EQ(origin, 2u);
        return static_cast<std::uint32_t>(96);
    }), 96u);
    EXPECT_EQ(stream.position, 96u);
    EXPECT_EQ(stream.last_error, 0u);

    EXPECT_EQ(table.seek(0, 0, 0, [](StreamState&, std::int32_t, std::uint32_t) {
        return 0xffffffffu;
    }), 0xffffffffu);
    EXPECT_EQ(stream.last_error, 1u);
}

TEST(Handles, RejectsInvalidSeek) {
    HandleTable table(1);
    EXPECT_EQ(table.seek(1, 0, 0, [](StreamState&, std::int32_t, std::uint32_t) {
        return 0u;
    }), 0xffffffffu);
    EXPECT_EQ(table.invalid_handle_errors(), 1u);
}

TEST(Handles, WritesBinaryDataWithoutConversion) {
    StreamState stream;
    HandleTable table(1);
    table.set_slot(0, 0x1, &stream);
    std::vector<std::uint8_t> output;

    const std::uint8_t input[] = {'a', '\n', 'b'};
    EXPECT_EQ(table.write(0, input, sizeof(input), [&](StreamState&, const std::uint8_t* data,
                                                       std::size_t size) {
        output.insert(output.end(), data, data + size);
        return size;
    }), 3);
    EXPECT_EQ(output, (std::vector<std::uint8_t>{'a', '\n', 'b'}));
    EXPECT_EQ(stream.last_error, 0u);
}

TEST(Handles, WritesTextDataWithLfExpansion) {
    StreamState stream;
    HandleTable table(1);
    table.set_slot(0, 0x81, &stream);
    std::vector<std::uint8_t> output;
    std::size_t calls = 0;

    const std::uint8_t input[] = {'a', '\n', 'b', '\n'};
    EXPECT_EQ(table.write(0, input, sizeof(input), [&](StreamState&, const std::uint8_t* data,
                                                       std::size_t size) {
        ++calls;
        output.insert(output.end(), data, data + size);
        return size;
    }), 4);
    EXPECT_EQ(output, (std::vector<std::uint8_t>{'a', '\r', '\n', 'b', '\r', '\n'}));
    EXPECT_EQ(calls, 1u);
    EXPECT_EQ(stream.last_error, 0u);
}

TEST(Handles, WriteFailureTracksError) {
    StreamState stream;
    HandleTable table(1);
    table.set_slot(0, 0x1, &stream);
    const std::uint8_t input[] = {'x', 'y'};

    EXPECT_EQ(table.write(0, input, sizeof(input), [](StreamState&, const std::uint8_t*, std::size_t) {
        return std::size_t{0};
    }), 0);
    EXPECT_EQ(stream.last_error, 1u);
}

TEST(Handles, ReadsBinaryDataWithoutTranslation) {
    StreamState stream;
    HandleTable table(1);
    table.set_slot(0, 0x1, &stream);
    std::uint8_t output[4] = {};
    EXPECT_EQ(table.read(0, output, sizeof output, [](StreamState&, std::uint8_t* data,
                                                       std::size_t size) {
        EXPECT_EQ(size, 4u);
        data[0] = 'a';
        data[1] = '\r';
        data[2] = '\n';
        data[3] = 0x1a;
        return std::size_t{4};
    }), 4);
    EXPECT_EQ(output[1], '\r');
    EXPECT_EQ(output[2], '\n');
    EXPECT_EQ(output[3], 0x1au);
}

TEST(Handles, ReadsTextDataWithCrLfAndCtrlZHandling) {
    StreamState stream;
    HandleTable table(1);
    table.set_slot(0, 0x81, &stream);
    std::uint8_t output[6] = {};
    EXPECT_EQ(table.read(0, output, sizeof output, [](StreamState&, std::uint8_t* data,
                                                       std::size_t) {
        const std::uint8_t source[] = {'a', '\r', '\n', 'b', 0x1a, 'c'};
        std::memcpy(data, source, sizeof source);
        return sizeof source;
    }), 3);
    EXPECT_EQ(output[0], 'a');
    EXPECT_EQ(output[1], '\n');
    EXPECT_EQ(output[2], 'b');
    EXPECT_EQ(table.query_flags(0, 0x2), 0x2);
}

TEST(Handles, CharacterTextReadDoesNotTreatCtrlZAsEof) {
    StreamState stream;
    HandleTable table(1);
    table.set_slot(0, 0xc1, &stream);
    std::uint8_t output[2] = {};
    EXPECT_EQ(table.read(0, output, sizeof output, [](StreamState&, std::uint8_t* data,
                                                       std::size_t) {
        data[0] = 0x1a;
        data[1] = 'x';
        return std::size_t{2};
    }), 2);
    EXPECT_EQ(output[0], 0x1au);
    EXPECT_EQ(output[1], 'x');
    EXPECT_EQ(table.query_flags(0, 0x2), 0u);
}

TEST(Handles, ClosesValidSlotAndInvalidatesIt) {
    StreamState stream;
    HandleTable table(1);
    table.set_slot(0, 0x1, &stream);
    bool called = false;

    EXPECT_EQ(table.close(0, [&](StreamState& state) {
        called = true;
        state.position = 12;
        return 0u;
    }), 0);
    EXPECT_TRUE(called);
    EXPECT_EQ(stream.position, 12u);
    EXPECT_EQ(table.query_flags(0, 0xff), 0u);
    EXPECT_EQ(table.close(0, [](StreamState&) { return 0u; }), -1);
    EXPECT_EQ(table.invalid_handle_errors(), 1u);
}

TEST(Handles, CloseFailureInvalidatesSlotAndMapsError) {
    StreamState stream;
    HandleTable table(1);
    table.set_slot(0, 0x1, &stream);

    EXPECT_EQ(table.close(0, [](StreamState&) { return 5u; }), -1);
    EXPECT_EQ(stream.last_error, 5u);
    EXPECT_EQ(current_error_state().dos_error, 5u);
    EXPECT_EQ(table.query_flags(0, 0xff), 0u);
}

TEST(Handles, ReusesSlotAfterClose) {
    StreamState stream;
    HandleTable table(1);
    table.set_slot(0, 0, &stream);
    ASSERT_EQ(table.bind(0, 123), 0);
    ASSERT_EQ(table.close(0, [](StreamState&) { return 0u; }), 0);

    EXPECT_EQ(table.create(456, 0, HandleTable::NativeFileType::disk), 0u);
    EXPECT_EQ(table.validate_native_handle(0), 456u);
}

TEST(Handles, BindsAndReleasesNativeHandle) {
    StreamState stream;
    HandleTable table(1);
    table.set_slot(0, 0, &stream);

    EXPECT_EQ(table.bind(0, 123), 0);
    EXPECT_EQ(table.query_flags(0, 0x1), 0x1);
    EXPECT_EQ(table.bind(0, 456), -1);
    EXPECT_EQ(table.release_native_handle(0), 0);
    EXPECT_EQ(table.query_flags(0, 0x1), 0u);
    EXPECT_EQ(table.release_native_handle(0), -1);
    EXPECT_EQ(current_error_state().errno_value, 9u);
    EXPECT_EQ(current_error_state().dos_error, 0u);
    EXPECT_EQ(table.invalid_handle_errors(), 2u);
}

TEST(Handles, RejectsInvalidNativeHandleBinding) {
    HandleTable table(1);
    EXPECT_EQ(table.bind(0, HandleTable::Slot::invalid_native_handle()), -1);
    EXPECT_EQ(current_error_state().errno_value, 9u);
    EXPECT_EQ(current_error_state().dos_error, 0u);
    EXPECT_EQ(table.invalid_handle_errors(), 1u);
}

TEST(Handles, RejectsOverwritingBoundNativeHandle) {
    HandleTable table(1);
    ASSERT_EQ(table.bind(0, 123), 0);
    EXPECT_EQ(table.bind(0, 456), -1);
    EXPECT_EQ(table.validate_native_handle(0), 123u);
    EXPECT_EQ(current_error_state().errno_value, 9u);
    EXPECT_EQ(current_error_state().dos_error, 0u);
}

TEST(Handles, FlushesValidNativeHandleAndMapsFailure) {
    StreamState stream;
    HandleTable table(1);
    table.set_slot(0, 0, &stream);
    ASSERT_EQ(table.bind(0, 123), 0);
    std::uintptr_t observed = 0;
    EXPECT_EQ(table.flush(0, [&](std::uintptr_t native_handle) {
        observed = native_handle;
        return 0u;
    }), 0);
    EXPECT_EQ(observed, 123u);
    EXPECT_EQ(table.flush(0, [](std::uintptr_t) { return 5u; }), -1);
    EXPECT_EQ(current_error_state().dos_error, 5u);
    EXPECT_EQ(current_error_state().errno_value, 9u);
}

TEST(Handles, RejectsFlushForInvalidSlot) {
    HandleTable table(1);
    EXPECT_EQ(table.flush(0, [](std::uintptr_t) { return 0u; }), -1);
    EXPECT_EQ(current_error_state().errno_value, 9u);
}

TEST(Handles, ValidatesNativeHandleAndTracksInvalidState) {
    StreamState stream;
    HandleTable table(1);
    table.set_slot(0, 0x1, &stream);
    EXPECT_EQ(table.bind(0, 123), 0);
    EXPECT_EQ(table.validate_native_handle(0), 123u);
    EXPECT_EQ(table.validate_native_handle(1), HandleTable::Slot::invalid_native_handle());
    EXPECT_EQ(current_error_state().errno_value, 9u);
    EXPECT_EQ(current_error_state().dos_error, 0u);
}

TEST(Handles, InvalidatesNativeHandleAfterRelease) {
    StreamState stream;
    HandleTable table(1);
    table.set_slot(0, 0, &stream);
    ASSERT_EQ(table.bind(0, 123), 0);
    ASSERT_EQ(table.release_native_handle(0), 0);

    EXPECT_EQ(table.validate_native_handle(0), HandleTable::Slot::invalid_native_handle());
    EXPECT_EQ(current_error_state().errno_value, 9u);
    EXPECT_EQ(current_error_state().dos_error, 0u);
}

TEST(Handles, EntersAndLeavesSlotLock) {
    HandleTable table(1);
    bool called = false;
    EXPECT_EQ(table.with_slot_lock(0, [&] {
        called = true;
        return 17;
    }), 17);
    EXPECT_TRUE(called);
}

TEST(Handles, GrowsHandleTableByGroupsOfThirtyTwo) {
    HandleTable table(1);
    EXPECT_EQ(table.query_flags(31, 0xff), 0u);
    EXPECT_TRUE(table.ensure_group());
    EXPECT_EQ(table.bind(31, 123), 0);
    EXPECT_EQ(table.validate_native_handle(31), 123u);
    EXPECT_EQ(table.query_flags(32, 0xff), 0u);
}

TEST(Handles, GrowsNonAlignedTableToNextGroupBoundary) {
    HandleTable table(33);
    ASSERT_TRUE(table.ensure_group());
    EXPECT_EQ(table.bind(63, 123), 0);
    EXPECT_EQ(table.validate_native_handle(63), 123u);
    EXPECT_EQ(table.query_flags(64, 0xff), 0u);
}

TEST(Handles, CreatesEntryAfterFillingOneCompleteGroup) {
    HandleTable table(32);
    for (std::size_t i = 0; i < 32; ++i) {
        ASSERT_EQ(table.create(100 + i, 0, HandleTable::NativeFileType::disk), i);
    }
    EXPECT_EQ(table.create(999, 0, HandleTable::NativeFileType::disk), 32u);
    EXPECT_EQ(table.validate_native_handle(32), 999u);
}

TEST(Handles, CreatesEntryWithModeAndFileTypeFlags) {
    HandleTable table(2);
    const auto handle = table.create(123, 0x4088, HandleTable::NativeFileType::character);
    EXPECT_EQ(handle, 0u);
    EXPECT_EQ(table.query_flags(handle, 0x1), 0x1);
    EXPECT_EQ(table.query_flags(handle, 0x20), 0x20);
    EXPECT_EQ(table.query_flags(handle, 0x80), 0x80);
    EXPECT_EQ(table.query_flags(handle, 0x40), 0x40);
}

TEST(Handles, CreateGrowsTableWhenAllSlotsAreOccupied) {
    HandleTable table(1);
    for (std::size_t i = 0; i < 1; ++i) {
        ASSERT_EQ(table.create(123 + i, 0, HandleTable::NativeFileType::disk), i);
    }
    EXPECT_EQ(table.create(456, 0, HandleTable::NativeFileType::disk), 1u);
    EXPECT_EQ(table.validate_native_handle(1), 456u);
}
