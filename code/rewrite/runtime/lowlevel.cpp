#include "lowlevel.h"

#include <algorithm>
#include <cstring>
#include <climits>
#include <cwchar>
#include <string>

namespace ra2::runtime {

namespace {
thread_local CrtErrorState error_state;

int compare_ascii_folded(std::string_view left, const char* right) {
    std::size_t index = 0;
    while (index < left.size() && right[index] != '\0') {
        unsigned char left_char = static_cast<unsigned char>(left[index]);
        unsigned char right_char = static_cast<unsigned char>(right[index]);
        if (left_char >= 'A' && left_char <= 'Z') left_char += 'a' - 'A';
        if (right_char >= 'A' && right_char <= 'Z') right_char += 'a' - 'A';
        if (left_char != right_char) return left_char < right_char ? -1 : 1;
        ++index;
    }
    if (index < left.size()) return 1;
    return right[index] == '\0' ? 0 : -1;
}
}

char* strncpy_legacy(char* destination, const char* source, std::size_t count) {
    if (count == 0) return destination;
    std::size_t i = 0;
    for (; i < count && source[i] != '\0'; ++i) destination[i] = source[i];
    for (; i < count; ++i) destination[i] = '\0';
    return destination;
}

std::string assemble_path(std::string_view drive, std::string_view directory,
                          std::string_view basename, std::string_view extension) {
    std::string result;
    result.reserve(drive.size() + directory.size() + basename.size() + extension.size() + 2);
    result.append(drive);
    if (!drive.empty() && !directory.empty()) result.push_back(':');
    result.append(directory);
    if (!directory.empty() && !basename.empty() && directory.back() != '/' && directory.back() != '\\') {
        result.push_back('\\');
    }
    result.append(basename);
    if (!extension.empty() && extension.front() != '.') result.push_back('.');
    result.append(extension);
    return result;
}

const unsigned char* backward_multibyte_boundary_legacy(
    const unsigned char* begin, const unsigned char* end, bool multibyte_mode,
    const std::function<bool(unsigned char)>& is_lead_byte) {
    if (begin == nullptr || end == nullptr || begin >= end) return nullptr;
    if (!multibyte_mode || !is_lead_byte) return end - 1;

    const unsigned char* cursor = end - 1;
    if (is_lead_byte(*cursor)) return end - begin >= 2 ? end - 2 : begin;
    while (cursor > begin) {
        --cursor;
        if (!is_lead_byte(*cursor)) break;
    }
    const std::ptrdiff_t distance = end - cursor;
    const unsigned char* result = end - (distance & 1) - 1;
    return result < begin ? begin : result;
}

char* scan_token(char* input, std::string_view delimiters, char*& cursor) {
    char* begin = input != nullptr ? input : cursor;
    if (begin == nullptr) return nullptr;
    auto is_delimiter = [delimiters](unsigned char value) {
        return delimiters.find(static_cast<char>(value)) != std::string_view::npos;
    };
    while (*begin != '\0' && is_delimiter(static_cast<unsigned char>(*begin))) ++begin;
    if (*begin == '\0') {
        cursor = begin;
        return nullptr;
    }
    char* token = begin;
    while (*begin != '\0' && !is_delimiter(static_cast<unsigned char>(*begin))) ++begin;
    if (*begin != '\0') {
        *begin = '\0';
        cursor = begin + 1;
    } else {
        cursor = begin;
    }
    return token;
}

int parse_decimal_legacy(std::string_view text) {
    std::size_t index = 0;
    while (index < text.size() && (text[index] == ' ' || text[index] == '\t' ||
                                   text[index] == '\n' || text[index] == '\r' ||
                                   text[index] == '\f' || text[index] == '\v')) {
        ++index;
    }
    bool negative = false;
    if (index < text.size() && (text[index] == '-' || text[index] == '+')) {
        negative = text[index] == '-';
        ++index;
    }
    int result = 0;
    while (index < text.size() && text[index] >= '0' && text[index] <= '9') {
        result = result * 10 + (text[index] - '0');
        ++index;
    }
    return negative ? -result : result;
}

int forward_numeric_format_legacy(int value, int output, int base,
                                  const NumericFormatForward& formatter) {
    const bool unsigned_value = base == 10 && value < 0;
    if (formatter.forward) formatter.forward(value, output, base, unsigned_value);
    return output;
}

void binary_search_legacy(const SearchEntry* entries, int last_index,
                          const char* key, const void** output) {
    if (entries == nullptr || key == nullptr || output == nullptr || last_index < 0) return;
    int low = 0;
    int high = last_index;
    while (low <= high) {
        const int middle = low + (high - low) / 2;
        const int comparison = compare_ascii_folded(key, entries[middle].key);
        if (comparison == 0) {
            *output = entries[middle].payload;
            return;
        }
        if (comparison < 0) high = middle - 1;
        else low = middle + 1;
    }
}

void initialize_locale_legacy(LocaleInitState& state, std::string_view short_name,
                              std::string_view default_name, std::uint32_t short_code_page,
                              const std::function<std::uint32_t()>& enumerate_locales) {
    state.short_name = string_length_legacy(std::string(short_name).c_str()) == 3 ? 1u : 0u;
    state.default_name = string_length_legacy(std::string(default_name).c_str()) == 3 ? 1u : 0u;
    state.locale_flags = 0;
    state.code_page = state.short_name != 0 ? 2u : short_code_page;
    const std::uint32_t discovered = enumerate_locales ? enumerate_locales() : 0u;
    if ((discovered & 0x100u) != 0 && (discovered & 0x200u) != 0 &&
        (discovered & 0x7u) != 0) {
        state.locale_flags = discovered;
    }
}

HeapSegmentState* create_heap_segment_legacy(HeapSegmentState* segments,
                                             HeapSegmentAllocator& allocator) {
    if (segments == nullptr || !allocator.allocate_metadata ||
        !allocator.reserve_address_space) return nullptr;
    if (allocator.count == allocator.capacity) {
        if (!allocator.grow_records) return nullptr;
        segments = allocator.grow_records(segments, allocator.capacity + 16);
        if (segments == nullptr) return nullptr;
        allocator.capacity += 16;
    }
    HeapSegmentState& segment = segments[allocator.count];
    segment.metadata = allocator.allocate_metadata(0x41c4);
    if (segment.metadata == nullptr) return nullptr;
    segment.address = allocator.reserve_address_space(0x100000);
    if (segment.address == nullptr) {
        if (allocator.free_metadata) allocator.free_metadata(segment.metadata);
        segment.metadata = nullptr;
        return nullptr;
    }
    segment.flags = 0;
    segment.free_count = 0;
    segment.bitmap = 0xffffffffu;
    ++allocator.count;
    return &segment;
}

std::uint64_t unsigned_remainder(std::uint64_t dividend, std::uint64_t divisor) {
    return dividend % divisor;
}

void* memmove_legacy(void* destination, const void* source, std::size_t count) {
    return std::memmove(destination, source, count);
}

char* string_copy_legacy(char* destination, const char* source) {
    std::size_t i = 0;
    do {
        destination[i] = source[i];
    } while (source[i++] != '\0');
    return destination;
}

std::size_t string_length_legacy(const char* text) {
    return std::strlen(text);
}

std::size_t leading_alpha_length_legacy(const char* text) {
    std::size_t length = 0;
    while ((text[length] >= 'A' && text[length] <= 'Z') ||
           (text[length] >= 'a' && text[length] <= 'z')) {
        ++length;
    }
    return length;
}

const RuntimeRecord* find_runtime_record_legacy(const RuntimeRecord* records,
                                                std::size_t count, std::uint32_t key) {
    if (records == nullptr) return nullptr;
    for (std::size_t index = 0; index < count; ++index) {
        if (records[index].key == key) return records + index;
    }
    return nullptr;
}

const HeapSegmentRange* find_heap_segment_legacy(const HeapSegmentRange* segments,
                                                 std::size_t count, std::uintptr_t address) {
    if (segments == nullptr) return nullptr;
    for (std::size_t index = 0; index < count; ++index) {
        const std::uintptr_t base = segments[index].base;
        if (address >= base && address - base < 0x100000) return segments + index;
    }
    return nullptr;
}

bool invoke_allocation_retry_legacy(const std::function<int(std::size_t)>& retry,
                                    std::size_t requested_size) {
    return retry && retry(requested_size) != 0;
}

int guarded_wide_char_to_byte_legacy(char* destination, wchar_t character, bool multibyte_mode,
                                     ConversionGuardState& state,
                                     const std::function<void()>& lock,
                                     const std::function<void()>& unlock) {
    ++state.references;
    const bool use_lock = state.locking_enabled;
    if (use_lock) {
        --state.references;
        lock();
    }
    const int result = wide_char_to_byte(destination, character, multibyte_mode);
    if (use_lock) {
        unlock();
    } else {
        --state.references;
    }
    return result;
}

int wide_char_to_byte(char* destination, wchar_t character, bool multibyte_mode) {
    if (destination == nullptr) return 0;
    if (!multibyte_mode) {
        if (static_cast<unsigned long>(character) < 0x100) {
            destination[0] = static_cast<char>(character);
            return 1;
        }
        current_error_state().errno_value = 42;
        return -1;
    }

    std::mbstate_t state{};
    const std::size_t result = std::wcrtomb(destination, character, &state);
    if (result == static_cast<std::size_t>(-1)) {
        current_error_state().errno_value = 42;
        return -1;
    }
    return static_cast<int>(result);
}

int byte_to_wide(wchar_t* destination, const char* source, std::size_t capacity,
                 bool multibyte_mode) {
    if (source == nullptr || capacity == 0) return 0;
    const unsigned char first = static_cast<unsigned char>(source[0]);
    if (first == 0) {
        if (destination != nullptr) destination[0] = L'\0';
        return 0;
    }
    if (!multibyte_mode) {
        if (destination != nullptr) destination[0] = static_cast<wchar_t>(first);
        return 1;
    }
    if (destination == nullptr) return -1;
    std::mbstate_t state{};
    const std::size_t result = std::mbrtowc(destination, source, capacity, &state);
    if (result == static_cast<std::size_t>(-1) || result == static_cast<std::size_t>(-2)) {
        current_error_state().errno_value = 42;
        return -1;
    }
    return result == 0 ? 0 : 1;
}

void* memset_legacy(void* destination, int value, std::size_t count) {
    return std::memset(destination, value, count);
}

int parse_hex_legacy(const char* text) {
    int result = 0;
    while (*text != '\0') {
        unsigned char digit = static_cast<unsigned char>(*text++);
        if (digit >= 'a' && digit <= 'f') {
            digit = static_cast<unsigned char>(digit - 0x27);
        } else if (digit >= 'A' && digit <= 'F') {
            digit = static_cast<unsigned char>(digit - 7);
        }
        result = (result - 3) * 16 + static_cast<int>(static_cast<signed char>(digit));
    }
    return result;
}

double copysign_legacy(double value, double sign) {
    std::uint64_t value_bits = 0;
    std::uint64_t sign_bits = 0;
    std::memcpy(&value_bits, &value, sizeof value_bits);
    std::memcpy(&sign_bits, &sign, sizeof sign_bits);
    value_bits = (value_bits & 0x7fffffffffffffffull) | (sign_bits & 0x8000000000000000ull);
    std::memcpy(&value, &value_bits, sizeof value);
    return value;
}

int classify_double_legacy(double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof bits == sizeof value, "double must be IEEE-754 binary64");
    std::memcpy(&bits, &value, sizeof bits);
    const bool negative = (bits >> 63) != 0;
    const std::uint64_t exponent = (bits >> 52) & 0x7ff;
    const std::uint64_t fraction = bits & 0x000fffffffffffffull;
    if (exponent == 0x7ff) {
        return fraction == 0 ? 1 : 2;
    }
    if (exponent == 0 && fraction != 0) {
        return negative ? 0x10 : 0x80;
    }
    if (value == 0.0) return negative ? 0x20 : 0x40;
    return negative ? 0x108 : 0x100;
}

int classify_special_double_legacy(double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof bits == sizeof value, "double must be IEEE-754 binary64");
    std::memcpy(&bits, &value, sizeof bits);
    const std::uint64_t exponent = (bits >> 52) & 0x7ff;
    const std::uint64_t fraction = bits & 0x000fffffffffffffull;
    if (exponent != 0x7ff) return 0;
    if (fraction == 0) return (bits >> 63) != 0 ? 2 : 1;
    return (fraction & 0x0008000000000000ull) != 0 ? 3 : 4;
}

CrtErrorState& current_error_state() {
    return error_state;
}

void map_os_error(std::uint32_t os_error) {
    CrtErrorState& state = current_error_state();
    state.dos_error = os_error;
    if (os_error >= 20 && os_error <= 36) {
        state.errno_value = 13;
    } else if (os_error >= 188 && os_error <= 202) {
        state.errno_value = 8;
    } else {
        state.errno_value = 22;
    }
}

std::uint32_t* errno_slot_legacy() {
    return &current_error_state().errno_value;
}

std::uint32_t* dos_error_slot_legacy() {
    return &current_error_state().dos_error;
}

bool is_win32_nt_platform_legacy(bool query_succeeded, std::uint32_t platform_id) {
    return query_succeeded && platform_id == 2;
}

}  // namespace ra2::runtime
