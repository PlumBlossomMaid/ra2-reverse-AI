#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace ra2::runtime {

struct StreamState {
    std::uint8_t* current = nullptr;
    std::uint8_t* base = nullptr;
    std::uint32_t flags = 0;
    std::uint32_t position = 0;
    std::uint32_t capacity = 0;
    std::uint32_t buffered_count = 0;
    std::uint32_t last_error = 0;
    std::uint8_t inline_storage[2] = {0, 0};
    std::unique_ptr<std::uint8_t[]> heap_storage;

    StreamState() = default;
    StreamState(const StreamState&) = delete;
    StreamState& operator=(const StreamState&) = delete;
    StreamState(StreamState&&) noexcept = default;
    StreamState& operator=(StreamState&&) noexcept = default;
};

// 0x7D7710: allocate the stream's 0x1000-byte buffer, with a two-byte fallback.
bool initialize_stream(StreamState& stream);

// 0x7D9EF8: write one byte into a buffered stream or verify it in flag 0x40 mode.
int buffered_put_byte_legacy(std::uint32_t value, StreamState& stream);

// 0x7D2169: consume one buffered byte, delegating exhausted state to the refill helper.
int buffered_get_byte_legacy(
    std::uint8_t*& current, std::int32_t& remaining,
    const std::function<int(std::uint8_t*&, std::int32_t&)>& refill);

// 0x7D7754: the handle-table flag query used by the stream wrappers.
class HandleTable {
 public:
    struct Slot {
        std::uint8_t flags = 0;
        StreamState* stream = nullptr;
        std::uintptr_t native_handle = invalid_native_handle();
        std::shared_ptr<std::mutex> mutex = std::make_shared<std::mutex>();

        static constexpr std::uintptr_t invalid_native_handle() {
            return static_cast<std::uintptr_t>(-1);
        }
    };

    explicit HandleTable(std::size_t handle_count);
    // 0x7D93A1: lazily allocate the next 32-slot CRT handle group.
    bool ensure_group();
    void set_slot(std::size_t handle, std::uint8_t flags, StreamState* stream);
    std::uint8_t query_flags(std::size_t handle, std::uint8_t mask) const;
    // 0x7D95BF: validate an active slot and return its native handle.
    std::uintptr_t validate_native_handle(std::size_t handle);

    // 0x7D96A8/0x7D9707: initialize, enter, and leave a slot critical section.
    int with_slot_lock(std::size_t handle, const std::function<int()>& operation);

    using Operation = std::function<int(StreamState&, std::uintptr_t, std::uintptr_t)>;
    int dispatch(std::size_t handle, std::uintptr_t arg1, std::uintptr_t arg2,
                 const Operation& operation);

    // 0x7D769D: SetFilePointer-like position update after handle validation.
    using SeekOperation = std::function<std::uint32_t(StreamState&, std::int32_t, std::uint32_t)>;
    std::uint32_t seek(std::size_t handle, std::int32_t offset, std::uint32_t origin,
                       const SeekOperation& operation);

    // 0x7D30D1: validate, lock, and flush an active native handle.
    using FlushOperation = std::function<std::uint32_t(std::uintptr_t)>;
    std::int32_t flush(std::size_t handle, const FlushOperation& operation);

    // 0x7D0DBA: write bytes, with optional text-mode LF expansion.
    using WriteOperation = std::function<std::size_t(StreamState&, const std::uint8_t*, std::size_t)>;
    std::int64_t write(std::size_t handle, const std::uint8_t* data, std::size_t size,
                       const WriteOperation& operation);

    // 0x7D0844: read bytes, with optional CRLF normalization and Ctrl-Z EOF handling.
    using ReadOperation = std::function<std::size_t(StreamState&, std::uint8_t*, std::size_t)>;
    std::int64_t read(std::size_t handle, std::uint8_t* data, std::size_t size,
                      const ReadOperation& operation);

    // 0x7D2AD1: close the validated handle and invalidate its table slot.
    using CloseOperation = std::function<std::uint32_t(StreamState&)>;
    std::int32_t close(std::size_t handle, const CloseOperation& operation);

    // 0x7D94C4: bind an OS-handle-like value to an unused slot.
    std::int32_t bind(std::size_t handle, std::uintptr_t native_handle);

    // 0x7D9540: clear the native handle from an active slot.
    std::int32_t release_native_handle(std::size_t handle);

    enum class NativeFileType : std::uint32_t {
        unknown = 0,
        disk = 1,
        character = 2,
        pipe = 3,
    };

    // 0x7D9601: allocate a slot and derive its flags from open mode and file type.
    std::size_t create(std::uintptr_t native_handle, std::uint32_t open_mode,
                       NativeFileType file_type);

    std::uint32_t invalid_handle_errors() const { return invalid_handle_errors_; }

 private:
    std::vector<Slot> slots_;
    std::uint32_t invalid_handle_errors_ = 0;
};

}  // namespace ra2::runtime
