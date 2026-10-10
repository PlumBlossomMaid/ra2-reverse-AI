#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace ra2::runtime {

// 0x7C91D0: MSVC 1998 _strncpy semantics, including zero padding.
char* strncpy_legacy(char* destination, const char* source, std::size_t count);

// 0x7C9FF0: assemble drive, directory, basename and extension.
std::string assemble_path(std::string_view drive, std::string_view directory,
                          std::string_view basename, std::string_view extension);

// 0x7D16A0: return the previous multibyte character boundary before end.
const unsigned char* backward_multibyte_boundary_legacy(
    const unsigned char* begin, const unsigned char* end, bool multibyte_mode,
    const std::function<bool(unsigned char)>& is_lead_byte);

// 0x7C9CC2 family: split at the first byte present in delimiters.
// The delimiter is replaced by NUL in the mutable input.
char* scan_token(char* input, std::string_view delimiters, char*& cursor);

// 0x7C9B72: skip legacy whitespace and parse a signed decimal prefix.
int parse_decimal_legacy(std::string_view text);

struct NumericFormatForward {
    std::function<void(int, int, int, bool)> forward;
};

// 0x7D468C: enable the unsigned flag for negative base-10 values, then forward.
int forward_numeric_format_legacy(int value, int output, int base,
                                  const NumericFormatForward& formatter);

struct SearchEntry {
    const char* key = nullptr;
    const void* payload = nullptr;
};

// 0x7D6EA7: binary search a key-sorted array and write the matching entry payload.
void binary_search_legacy(const SearchEntry* entries, int last_index,
                          const char* key, const void** output);

struct LocaleInitState {
    std::uint32_t short_name = 0;
    std::uint32_t default_name = 0;
    std::uint32_t code_page = 0;
    std::uint32_t locale_flags = 0;
};

// 0x7D6EFF: initialize locale-name flags and retain callback-discovered metadata.
void initialize_locale_legacy(LocaleInitState& state, std::string_view short_name,
                              std::string_view default_name, std::uint32_t short_code_page,
                              const std::function<std::uint32_t()>& enumerate_locales);

struct HeapSegmentState {
    std::uint32_t flags = 0;
    std::uint32_t free_count = 0;
    std::uint32_t bitmap = 0;
    void* metadata = nullptr;
    void* address = nullptr;
};

struct HeapSegmentAllocator {
    std::function<void*(std::size_t)> allocate_metadata;
    std::function<void*(std::size_t)> reserve_address_space;
    std::function<void(void*)> free_metadata;
    std::function<HeapSegmentState*(HeapSegmentState*, std::size_t)> grow_records;
    std::size_t capacity = 0;
    std::size_t count = 0;
};

// 0x7CFE1C: grow segment records, allocate metadata, and reserve a 1 MiB segment.
HeapSegmentState* create_heap_segment_legacy(HeapSegmentState* segments,
                                             HeapSegmentAllocator& allocator);

// 0x7D7840: unsigned 64-bit remainder, returned as two 32-bit halves.
std::uint64_t unsigned_remainder(std::uint64_t dividend, std::uint64_t divisor);

// 0x7D0A20: overlap-aware byte copy with the original memmove result semantics.
void* memmove_legacy(void* destination, const void* source, std::size_t count);

// 0x7D4BF0: forward copy through the first NUL byte, including that byte.
char* string_copy_legacy(char* destination, const char* source);

// 0x7D15A0: return the byte length before the first NUL character.
std::size_t string_length_legacy(const char* text);

// 0x7D75B0: count the leading ASCII alphabetic characters.
std::size_t leading_alpha_length_legacy(const char* text);

struct RuntimeRecord {
    std::uint32_t value = 0;
    std::uint32_t key = 0;
    std::uint32_t metadata = 0;
};

// 0x7D4B92: return the first fixed-size record with a matching key.
const RuntimeRecord* find_runtime_record_legacy(const RuntimeRecord* records,
                                                std::size_t count, std::uint32_t key);

struct HeapSegmentRange {
    std::uintptr_t base = 0;
};

// 0x7CF7BD: find the segment whose one-megabyte range contains an address.
const HeapSegmentRange* find_heap_segment_legacy(const HeapSegmentRange* segments,
                                                 std::size_t count, std::uintptr_t address);

// 0x7D06E8: invoke the optional allocation retry callback and booleanize its result.
bool invoke_allocation_retry_legacy(const std::function<int(std::size_t)>& retry,
                                    std::size_t requested_size);

struct ConversionGuardState {
    std::uint32_t references = 0;
    bool locking_enabled = false;
};

// 0x7D777D: guard one wide-character conversion using the CRT reference/lock protocol.
int guarded_wide_char_to_byte_legacy(char* destination, wchar_t character, bool multibyte_mode,
                                     ConversionGuardState& state,
                                     const std::function<void()>& lock,
                                     const std::function<void()>& unlock);

// 0x7D77D6: convert one wide character to a narrow byte sequence.
// The return value is the number of output bytes, or -1 on conversion failure.
int wide_char_to_byte(char* destination, wchar_t character, bool multibyte_mode);

// 0x7D9DDC: convert one or more narrow bytes to wide characters.
int byte_to_wide(wchar_t* destination, const char* source, std::size_t capacity,
                 bool multibyte_mode);

// 0x7D75E0: fill a byte range with the low byte of the requested value.
void* memset_legacy(void* destination, int value, std::size_t count);

// 0x7D7577: parse a NUL-terminated hexadecimal string with legacy signed accumulation.
int parse_hex_legacy(const char* text);

// 0x7D8EF5: copy the sign bit from one binary64 value to another.
double copysign_legacy(double value, double sign);

// 0x7D930F: classify an IEEE-754 double using the original bit-level result codes.
int classify_double_legacy(double value);

// 0x7D8DC8: classify IEEE-754 infinity and NaN encodings.
int classify_special_double_legacy(double value);

struct CrtErrorState {
    std::uint32_t dos_error = 0;
    std::uint32_t errno_value = 0;
};

// 0x7D140B/0x7D161B: per-thread CRT error storage and Win32 error mapping.
CrtErrorState& current_error_state();
void map_os_error(std::uint32_t os_error);

// 0x7D168E/0x7D1697: return addresses of the per-thread CRT error fields.
std::uint32_t* errno_slot_legacy();
std::uint32_t* dos_error_slot_legacy();

// 0x7D745B: identify the Win32 NT platform after a successful version query.
bool is_win32_nt_platform_legacy(bool query_succeeded, std::uint32_t platform_id);

}  // namespace ra2::runtime
