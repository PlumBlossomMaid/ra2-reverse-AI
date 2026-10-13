#include "facing.h"

namespace ra2::runtime {

namespace {

// 汇编里 |diff| 用 cdq/xor/sub 三连算（对 INT16_MIN 取绝对值得到 0x8000 而非回绕），
// 这里用无分支写法保持一致。
std::uint32_t abs_word(std::int32_t v) {
    const std::int32_t sign = v >> 31;
    return static_cast<std::uint32_t>((v ^ sign) - sign);
}

}  // namespace

std::uint32_t timer_get_time_left(std::uint32_t start_time, std::uint32_t time_left,
                                  std::uint32_t current_frame) {
    // 0x426630: StartTime == -1 走原样返回；否则 elapsed = frame - StartTime，
    // 无符号 jge 判定 elapsed >= TimeLeft 时返回 0。
    if (start_time == 0xFFFFFFFFu) {
        return time_left;
    }
    const std::uint32_t elapsed = current_frame - start_time;
    if (elapsed >= time_left) {
        return 0;
    }
    return time_left - elapsed;
}

std::uint32_t facing_get_facing(const FacingStruct& f, std::uint32_t current_frame) {
    const std::int16_t rot = static_cast<std::int16_t>(f.ROT);

    // 0x4C93DB: 无转速 → 直接回读 Value（dword，低 16 位即朝向）。
    if (rot <= 0) {
        return static_cast<std::uint32_t>(f.Value);
    }

    std::uint32_t remaining = f.TimeLeft;
    if (f.StartTime != 0xFFFFFFFFu) {
        // 0x4C93F2: elapsed >= TimeLeft 时原样返回 Value（旋转已结束）。
        const std::uint32_t elapsed = current_frame - f.StartTime;
        if (elapsed >= f.TimeLeft) {
            return static_cast<std::uint32_t>(f.Value);
        }
        remaining -= elapsed;
    }
    // 0x4C93FA: remaining == 0 → 原样返回 Value。
    if (remaining == 0) {
        return static_cast<std::uint32_t>(f.Value);
    }

    const std::int32_t diff =
        static_cast<std::int16_t>(static_cast<std::uint16_t>(f.Value - f.Initial));

    // 0x4C9411: steps = |diff| / ROT，为 0 时不插值。
    const std::uint32_t steps = abs_word(diff) / static_cast<std::uint32_t>(rot);
    if (steps == 0) {
        return static_cast<std::uint32_t>(f.Value);
    }

    // ★ 机器码顺序：先除后乘。0x4C943B idiv 得 diff/steps，0x4C943D imul
    // 乘 remaining。YRpp 头文件写的是 steps_left*diff/num_steps（先乘后除），
    // 整数截断结果不同——以机器码为准。
    const std::int32_t per_step = diff / static_cast<std::int32_t>(steps);
    const std::int32_t drop =
        static_cast<std::int16_t>(per_step * static_cast<std::int32_t>(remaining));
    return static_cast<std::uint32_t>(
        static_cast<std::uint16_t>(f.Value - drop));
}

bool facing_is_rotating(const FacingStruct& f, std::uint32_t current_frame) {
    // 0x4C9485: ROT <= 0 → false。
    if (static_cast<std::int16_t>(f.ROT) <= 0) {
        return false;
    }
    // 0x4C949A: elapsed >= TimeLeft → false；否则剩余时间非 0 才算在转。
    if (f.StartTime == 0xFFFFFFFFu) {
        return f.TimeLeft != 0;
    }
    const std::uint32_t elapsed = current_frame - f.StartTime;
    if (elapsed >= f.TimeLeft) {
        return false;
    }
    return (f.TimeLeft - elapsed) != 0;
}

bool facing_is_rotating_cw(const FacingStruct& f, std::uint32_t current_frame) {
    if (!facing_is_rotating(f, current_frame)) {
        return false;
    }
    // 0x4C94DF: setg —— (int16)(Value - Initial) > 0 即顺时针。
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(f.Value - f.Initial)) > 0;
}

bool facing_is_rotating_ccw(const FacingStruct& f, std::uint32_t current_frame) {
    if (!facing_is_rotating(f, current_frame)) {
        return false;
    }
    // 0x4C9522: sets —— (int16)(Value - Initial) < 0 即逆时针。
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(f.Value - f.Initial)) < 0;
}

bool facing_set_desired(FacingStruct& f, std::uint16_t target, std::uint32_t current_frame) {
    // 0x4C922D: Value == target → 直接 false，字段不动。
    if (f.Value == target) {
        return false;
    }

    // 0x4C9220 尾：Initial 记的是「转向此刻的实际朝向」而非旧 Value。
    const std::uint32_t here = facing_get_facing(f, current_frame);
    f.Initial = static_cast<std::uint16_t>(here);
    f.Value = target;

    // 0x4C92BF: 无转速 → 立即到位，不起步定时器。
    if (static_cast<std::int16_t>(f.ROT) <= 0) {
        return true;
    }

    // 0x4C92DA: TimeLeft = |Value - Initial| / ROT。
    const std::int32_t diff =
        static_cast<std::int16_t>(static_cast<std::uint16_t>(f.Value - f.Initial));
    f.TimeLeft = abs_word(diff) / static_cast<std::uint32_t>(static_cast<std::int16_t>(f.ROT));
    f.StartTime = current_frame;
    // 0x4C92E5 写的 +0x0C 取自未初始化栈槽，本家族从不读，这里保持 0。
    f.unused_C = 0;
    return true;
}

std::int16_t facing_difference(const FacingStruct& f, std::uint32_t current_frame) {
    // 0x4C95B5: Value - current()，只留低 16 位。
    const std::uint16_t now = static_cast<std::uint16_t>(facing_get_facing(f, current_frame));
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(f.Value - now));
}

std::int16_t facing_difference_from(const FacingStruct& f, std::uint16_t facing,
                                    std::uint32_t current_frame) {
    // 0x4C965F: 给定朝向 - current()，只留低 16 位。
    const std::uint16_t now = static_cast<std::uint16_t>(facing_get_facing(f, current_frame));
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(facing - now));
}

void facing_construct(FacingStruct* out, std::uint32_t rot, std::uint32_t current_frame) {
    // 0x4C91C0/0x4C91E0: 只写 Value/Initial/TimeLeft/StartTime/ROT，
    // +0x02/+0x06/+0x0C 三处不写（模型保持 0）。
    out->Value = 0;
    out->unused_2 = 0;
    out->Initial = 0;
    out->unused_6 = 0;
    out->StartTime = current_frame;
    out->unused_C = 0;
    out->TimeLeft = 0;
    // 0x4C91FF: clamp 到 127，再左移 8 位（INI 值 × 256）。
    out->ROT = static_cast<std::uint16_t>((rot > 0x7Fu ? 0x7Fu : rot) << 8);
    out->unused_16 = 0;
}

}  // namespace ra2::runtime