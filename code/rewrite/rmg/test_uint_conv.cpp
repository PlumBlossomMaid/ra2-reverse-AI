#include <cstdio>
#include <cstdint>
#include <cstring>

int main() {
    uint32_t test_values[6] = {0, 1, 0x7FFFFFFF, 0x80000000, 0xD3770013, 0xFFFFFFFF};
    for (int i = 0; i < 6; ++i) {
        uint32_t v = test_values[i];
        int64_t signed_val = static_cast<int64_t>(v);
        double cli_val = static_cast<double>(signed_val);
        int64_t fild_val = static_cast<int64_t>(static_cast<uint64_t>(v));
        double orig_val = static_cast<double>(fild_val);
        printf("0x%08X: CLI=%.1f orig=%.1f %s\n",
               v, cli_val, orig_val,
               cli_val == orig_val ? "SAME" : "DIFF!");
    }
    return 0;
}
