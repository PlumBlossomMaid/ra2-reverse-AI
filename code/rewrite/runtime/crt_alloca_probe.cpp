#include "crt_alloca_probe.h"

namespace ra2::crt {

std::uint32_t alloca_probe_legacy(std::uint32_t caller_esp,
                                  std::uint32_t size,
                                  std::vector<std::uint32_t>* probe_out) {
    // 汇编基点：lea ecx,[esp+8]（push ecx 后）== 调用者 esp
    std::uint32_t base = caller_esp;
    std::uint32_t cur = base;
    std::uint32_t remaining = size;
    if (probe_out) probe_out->clear();
    // 循环（0x7CA65C）：sub ecx,0x1000 / sub eax,0x1000 / test [ecx],eax
    //   / cmp eax,0x1000 / jae——eax 耗尽到 <0x1000 退出
    while (remaining >= 0x1000) {
        cur -= 0x1000;
        remaining -= 0x1000;
        if (probe_out) probe_out->push_back(cur);
    }
    // 收尾（0x7CA670）：sub ecx,eax → 探测 [基点-size]
    cur -= remaining;
    if (probe_out) probe_out->push_back(cur);
    // 返回后 esp = 调用者 esp - size（mov esp,ecx；push 返回地址；ret）
    return base - size;
}

}  // namespace ra2::crt
