#include <cstdio>
#include <cstdint>

extern "C" double x87_mul_add(double a, double b, double c);

int main() {
    int diffs = 0;
    int total = 0;
    uint32_t seed = 12345;
    for (int i = 0; i < 1000000; ++i) {
        seed = seed * 1664525u + 1013904223u;
        double a = (double)seed / 4294967295.0;
        seed = seed * 1664525u + 1013904223u;
        double b = (double)seed / 4294967295.0;
        seed = seed * 1664525u + 1013904223u;
        double c = (double)seed / 4294967295.0 * 0.1;
        double sse2 = a * b + c;
        double x87 = x87_mul_add(a, b, c);
        total++;
        if (sse2 != x87) {
            diffs++;
            if (diffs <= 5) {
                printf("DIFF i=%d a=%.17g b=%.17g c=%.17g SSE2=%.17g x87=%.17g\n",
                       i, a, b, c, sse2, x87);
            }
        }
    }
    printf("Total=%d Diffs=%d (%.4f%%)\n", total, diffs, 100.0 * diffs / total);
    return 0;
}
