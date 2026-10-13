#include "map_legal.h"

namespace ra2::runtime {

bool map_coordinates_legal(int map_w, int map_h, std::int16_t x, std::int16_t y) {
    // 0x568316: edi = x + y (movsx 后 lea [y+x])；jle → 非法。
    const std::int32_t sum = static_cast<std::int32_t>(x) + static_cast<std::int32_t>(y);
    if (sum <= map_w) {
        return false;
    }
    // 0x56831F: x - y >= W → 非法。
    if (static_cast<std::int32_t>(x) - static_cast<std::int32_t>(y) >= map_w) {
        return false;
    }
    // 0x568326: y - x >= W → 非法。
    if (static_cast<std::int32_t>(y) - static_cast<std::int32_t>(x) >= map_w) {
        return false;
    }
    // 0x568333: x + y > W + 2H → 非法。
    if (sum > map_w + 2 * map_h) {
        return false;
    }
    return true;
}

}  // namespace ra2::runtime