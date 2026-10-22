#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace ra2::crt {

// 0x7CBED3: walk a [begin, end) table of function pointers, calling every
// non-NULL entry. Mirrors _initterm. The original compares the cursor against
// the table end with an unsigned "jump if above or equal", so an inverted range
// performs zero iterations rather than running off the end.
using InitEntry = void (*)();
void initterm_legacy(const InitEntry* begin, const InitEntry* end);

// 0x7CBDAF: the two-round static-constructor dispatcher. Calls the early-CRT
// hook when the global is non-NULL, then runs the high table and the low table
// in that order.
using EarlyHook = std::function<void()>;
struct CinitTables {
    const InitEntry* ctor_low_begin = nullptr;
    const InitEntry* ctor_low_end = nullptr;
    const InitEntry* ctor_high_begin = nullptr;
    const InitEntry* ctor_high_end = nullptr;
};
void cinit_legacy(const CinitTables& tables, const EarlyHook& early_hook);

// 0x7D5AC0: the _setenvp segment counter. Walks the '='-separated environment
// string segment by segment with strlen, counting only the segments that do not
// start with '=', then reports the pointer-array size the original derives from
// that count.
std::size_t scan_env_segments_legacy(const char* environment);

// 0x7D5AF2: lea eax, [edi*4+4] — the pointer array always carries one extra dword.
std::size_t env_pointer_array_bytes(std::size_t segment_count);

}  // namespace ra2::crt