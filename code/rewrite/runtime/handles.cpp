#include "handles.h"
#include "lowlevel.h"

#include <limits>
#include <new>

namespace ra2::runtime {

bool initialize_stream(StreamState& stream) {
    stream.heap_storage.reset(new (std::nothrow) std::uint8_t[0x1000]);
    stream.position = 0;
    stream.buffered_count = 0;
    if (stream.heap_storage) {
        stream.base = stream.heap_storage.get();
        stream.current = stream.base;
        stream.capacity = 0x1000;
        stream.flags |= 0x8;
        return true;
    }
    stream.base = stream.inline_storage;
    stream.current = stream.base;
    stream.capacity = 2;
    stream.flags |= 0x4;
    return false;
}

int buffered_put_byte_legacy(std::uint32_t value, StreamState& stream) {
    const std::uint8_t byte = static_cast<std::uint8_t>(value);
    const bool writable = (stream.flags & 0x1u) != 0;
    const bool text_mode = (stream.flags & 0x80u) != 0;
    const bool not_eof = (stream.flags & 0x2u) == 0;
    if (!(value != std::numeric_limits<std::uint32_t>::max() &&
          (writable || (text_mode && not_eof)))) {
        return -1;
    }
    if (stream.base == nullptr || stream.current == nullptr) initialize_stream(stream);
    if (stream.current == stream.base) {
        if (stream.buffered_count != 0) return -1;
        ++stream.current;
    }
    if (stream.current == stream.base + stream.capacity) {
        if (stream.buffered_count != 0) return -1;
        ++stream.current;
    }
    if ((stream.flags & 0x40u) != 0) {
        --stream.current;
        if (*stream.current != byte) {
            ++stream.current;
            return -1;
        }
    } else {
        --stream.current;
        *stream.current = byte;
    }
    ++stream.buffered_count;
    stream.flags = (stream.flags & ~0x10u) | 0x1u;
    return byte;
}

int buffered_get_byte_legacy(
    std::uint8_t*& current, std::int32_t& remaining,
    const std::function<int(std::uint8_t*&, std::int32_t&)>& refill) {
    --remaining;
    if (remaining >= 0) {
        const int value = *current;
        ++current;
        return value;
    }
    return refill(current, remaining);
}

HandleTable::HandleTable(std::size_t handle_count) : slots_(handle_count) {}

bool HandleTable::ensure_group() {
    const std::size_t remainder = slots_.size() % 32;
    const std::size_t group_size = 32;
    const std::size_t growth = remainder == 0 ? group_size : group_size - remainder;
    try {
        slots_.resize(slots_.size() + growth);
    } catch (const std::bad_alloc&) {
        return false;
    }
    return true;
}

void HandleTable::set_slot(std::size_t handle, std::uint8_t flags, StreamState* stream) {
    if (handle >= slots_.size()) return;
    slots_[handle].flags = flags;
    slots_[handle].stream = stream;
    slots_[handle].native_handle = Slot::invalid_native_handle();
}

std::uintptr_t HandleTable::validate_native_handle(std::size_t handle) {
    if (handle >= slots_.size() || (slots_[handle].flags & 0x1) == 0) {
        current_error_state().errno_value = 9;
        current_error_state().dos_error = 0;
        ++invalid_handle_errors_;
        return Slot::invalid_native_handle();
    }
    return slots_[handle].native_handle;
}

int HandleTable::with_slot_lock(std::size_t handle, const std::function<int()>& operation) {
    if (handle >= slots_.size()) {
        ++invalid_handle_errors_;
        return -1;
    }
    std::lock_guard<std::mutex> guard(*slots_[handle].mutex);
    return operation();
}

std::int32_t HandleTable::bind(std::size_t handle, std::uintptr_t native_handle) {
    if (handle >= slots_.size() || slots_[handle].native_handle != Slot::invalid_native_handle() ||
        native_handle == Slot::invalid_native_handle()) {
        current_error_state().errno_value = 9;
        current_error_state().dos_error = 0;
        ++invalid_handle_errors_;
        return -1;
    }
    std::lock_guard<std::mutex> guard(*slots_[handle].mutex);
    if (slots_[handle].native_handle != Slot::invalid_native_handle()) {
        current_error_state().errno_value = 9;
        current_error_state().dos_error = 0;
        ++invalid_handle_errors_;
        return -1;
    }
    slots_[handle].native_handle = native_handle;
    slots_[handle].flags |= 0x1;
    return 0;
}

std::int32_t HandleTable::release_native_handle(std::size_t handle) {
    if (handle >= slots_.size() || (slots_[handle].flags & 0x1) == 0 ||
        slots_[handle].native_handle == Slot::invalid_native_handle()) {
        current_error_state().errno_value = 9;
        current_error_state().dos_error = 0;
        ++invalid_handle_errors_;
        return -1;
    }
    std::lock_guard<std::mutex> guard(*slots_[handle].mutex);
    slots_[handle].native_handle = Slot::invalid_native_handle();
    slots_[handle].flags &= static_cast<std::uint8_t>(~0x1u);
    return 0;
}

std::size_t HandleTable::create(std::uintptr_t native_handle, std::uint32_t open_mode,
                                NativeFileType file_type) {
    if (native_handle == Slot::invalid_native_handle() || file_type == NativeFileType::unknown) {
        return std::numeric_limits<std::size_t>::max();
    }

    for (std::size_t handle = 0; handle < slots_.size(); ++handle) {
        if (slots_[handle].native_handle != Slot::invalid_native_handle() ||
            (slots_[handle].flags & 0x1) != 0) {
            continue;
        }
        std::lock_guard<std::mutex> guard(*slots_[handle].mutex);
        if (slots_[handle].native_handle != Slot::invalid_native_handle() ||
            (slots_[handle].flags & 0x1) != 0) {
            continue;
        }
        std::uint8_t flags = 0x1;
        if ((open_mode & 0x8) != 0) flags |= 0x20;
        if ((open_mode & 0x4000) != 0) flags |= 0x80;
        if ((open_mode & 0x80) != 0) flags |= 0x10;
        if (file_type == NativeFileType::character) flags |= 0x40;
        if (file_type == NativeFileType::pipe) flags |= 0x8;
        slots_[handle].native_handle = native_handle;
        slots_[handle].flags = flags;
        return handle;
    }
    const std::size_t previous_size = slots_.size();
    if (!ensure_group()) return std::numeric_limits<std::size_t>::max();
    const std::size_t handle = previous_size;
    std::lock_guard<std::mutex> guard(*slots_[handle].mutex);
    std::uint8_t flags = 0x1;
    if ((open_mode & 0x8) != 0) flags |= 0x20;
    if ((open_mode & 0x4000) != 0) flags |= 0x80;
    if ((open_mode & 0x80) != 0) flags |= 0x10;
    if (file_type == NativeFileType::character) flags |= 0x40;
    if (file_type == NativeFileType::pipe) flags |= 0x8;
    slots_[handle].native_handle = native_handle;
    slots_[handle].flags = flags;
    return handle;
}

std::uint8_t HandleTable::query_flags(std::size_t handle, std::uint8_t mask) const {
    if (handle >= slots_.size()) return 0;
    return static_cast<std::uint8_t>(slots_[handle].flags & mask);
}

int HandleTable::dispatch(std::size_t handle, std::uintptr_t arg1, std::uintptr_t arg2,
                          const Operation& operation) {
    if (handle >= slots_.size() || (slots_[handle].flags & 0x1) == 0 ||
        slots_[handle].stream == nullptr) {
        ++invalid_handle_errors_;
        return -1;
    }
    std::lock_guard<std::mutex> guard(*slots_[handle].mutex);
    return operation(*slots_[handle].stream, arg1, arg2);
}

std::uint32_t HandleTable::seek(std::size_t handle, std::int32_t offset, std::uint32_t origin,
                                const SeekOperation& operation) {
    if (handle >= slots_.size() || (slots_[handle].flags & 0x1) == 0 ||
        slots_[handle].stream == nullptr) {
        ++invalid_handle_errors_;
        return std::numeric_limits<std::uint32_t>::max();
    }
    std::lock_guard<std::mutex> guard(*slots_[handle].mutex);
    StreamState& stream = *slots_[handle].stream;
    const std::uint32_t result = operation(stream, offset, origin);
    if (result == std::numeric_limits<std::uint32_t>::max()) {
        stream.last_error = 1;
        return result;
    }
    stream.position = result;
    stream.last_error = 0;
    return result;
}

std::int32_t HandleTable::flush(std::size_t handle, const FlushOperation& operation) {
    if (handle >= slots_.size() || (slots_[handle].flags & 0x1) == 0) {
        current_error_state().errno_value = 9;
        return -1;
    }
    std::lock_guard<std::mutex> guard(*slots_[handle].mutex);
    if ((slots_[handle].flags & 0x1) == 0 ||
        slots_[handle].native_handle == Slot::invalid_native_handle()) {
        current_error_state().errno_value = 9;
        return -1;
    }
    const std::uint32_t error = operation(slots_[handle].native_handle);
    if (error != 0) {
        current_error_state().dos_error = error;
        current_error_state().errno_value = 9;
        return -1;
    }
    return 0;
}

std::int64_t HandleTable::write(std::size_t handle, const std::uint8_t* data, std::size_t size,
                                const WriteOperation& operation) {
    if (size == 0) return 0;
    if (handle >= slots_.size() || (slots_[handle].flags & 0x1) == 0 ||
        slots_[handle].stream == nullptr) {
        ++invalid_handle_errors_;
        return -1;
    }
    if (data == nullptr) {
        slots_[handle].stream->last_error = 22;
        return -1;
    }

    std::lock_guard<std::mutex> guard(*slots_[handle].mutex);
    StreamState& stream = *slots_[handle].stream;
    const bool text_mode = (slots_[handle].flags & 0x80) != 0;
    std::size_t consumed = 0;
    std::size_t written = 0;
    std::size_t inserted = 0;
    stream.last_error = 0;

    while (consumed < size) {
        if (!text_mode) {
            const std::size_t count = operation(stream, data + consumed, size - consumed);
            written += count;
            consumed = size;
            if (count != size) {
                stream.last_error = 1;
                break;
            }
            continue;
        }

        std::uint8_t buffer[0x404];
        std::size_t output_size = 0;
        while (consumed < size && output_size < 0x400) {
            const std::uint8_t value = data[consumed++];
            if (value == '\n') {
                buffer[output_size++] = '\r';
                ++inserted;
            }
            buffer[output_size++] = value;
        }
        const std::size_t count = operation(stream, buffer, output_size);
        written += count;
        if (count != output_size) {
            stream.last_error = 1;
            break;
        }
    }
    return static_cast<std::int64_t>(written - inserted);
}

std::int64_t HandleTable::read(std::size_t handle, std::uint8_t* data, std::size_t size,
                               const ReadOperation& operation) {
    if (size == 0) return 0;
    if (handle >= slots_.size() || (slots_[handle].flags & 0x1) == 0 ||
        slots_[handle].stream == nullptr || data == nullptr) {
        ++invalid_handle_errors_;
        return -1;
    }

    std::lock_guard<std::mutex> guard(*slots_[handle].mutex);
    StreamState& stream = *slots_[handle].stream;
    const std::size_t count = operation(stream, data, size);
    if (count > size) {
        stream.last_error = 1;
        return -1;
    }
    if ((slots_[handle].flags & 0x80) == 0) {
        stream.last_error = 0;
        return static_cast<std::int64_t>(count);
    }

    const bool character_device = (slots_[handle].flags & 0x40) != 0;
    std::size_t output = 0;
    for (std::size_t input = 0; input < count; ++input) {
        const std::uint8_t value = data[input];
        if (value == 0x1a && !character_device) {
            slots_[handle].flags |= 0x2;
            break;
        }
        if (value == '\r' && input + 1 < count && data[input + 1] == '\n') {
            data[output++] = '\n';
            ++input;
        } else {
            data[output++] = value;
        }
    }
    stream.last_error = 0;
    return static_cast<std::int64_t>(output);
}

std::int32_t HandleTable::close(std::size_t handle, const CloseOperation& operation) {
    if (handle >= slots_.size() || (slots_[handle].flags & 0x1) == 0 ||
        slots_[handle].stream == nullptr) {
        ++invalid_handle_errors_;
        return -1;
    }
    std::lock_guard<std::mutex> guard(*slots_[handle].mutex);
    StreamState& stream = *slots_[handle].stream;
    const std::uint32_t error = operation(stream);
    slots_[handle].flags = 0;
    slots_[handle].native_handle = Slot::invalid_native_handle();
    if (error != 0) {
        map_os_error(error);
        stream.last_error = error;
        return -1;
    }
    stream.last_error = 0;
    return 0;
}

}  // namespace ra2::runtime
