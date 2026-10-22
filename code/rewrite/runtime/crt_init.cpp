#include "crt_init.h"

#include <cstring>

namespace ra2::crt {

void initterm_legacy(const InitEntry* begin, const InitEntry* end) {
    // 0x7CBED3: esi = begin; while (esi < end) { if (*esi) (*esi)(); esi += 4; }
    // The end test is unsigned "jump if above or equal", so an inverted range
    // yields zero iterations instead of walking past the table.
    for (const InitEntry* it = begin; it < end; ++it) {
        if (*it != nullptr) (*it)();
    }
}

void cinit_legacy(const CinitTables& tables, const EarlyHook& early_hook) {
    // 0x7CBDAF: test [0x87BEB8]; if non-zero call it, then push the high table
    // pair and run it, then the low table pair. Order matters: high runs first.
    if (early_hook) early_hook();
    initterm_legacy(tables.ctor_high_begin, tables.ctor_high_end);
    initterm_legacy(tables.ctor_low_begin, tables.ctor_low_end);
}

std::size_t scan_env_segments_legacy(const char* environment) {
    // 0x7D5AC0: walk the environment string segment by segment using strlen.
    // A segment whose first character is '=' is skipped; every other segment
    // increments the counter. The loop ends on the first '\0'.
    std::size_t segment_count = 0;
    const char* cursor = environment;
    while (*cursor != '\0') {
        if (*cursor != '=') ++segment_count;
        cursor += std::strlen(cursor) + 1;
    }
    return segment_count;
}

std::size_t env_pointer_array_bytes(std::size_t segment_count) {
    // 0x7D5AF2: lea eax, [edi*4+4] — one extra dword beyond the segment count.
    return segment_count * 4 + 4;
}

}  // namespace ra2::crt