#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>
#include "lzo.h"
int main(int argc, char** argv) {
  std::ifstream f(argv[1], std::ios::binary);
  std::vector<std::uint8_t> b((std::istreambuf_iterator<char>(f)), {});
  std::size_t p = 0, blocks = 0, total = 0;
  while (p + 4 <= b.size()) {
    const std::size_t clen = b[p] | (b[p + 1] << 8);
    const std::size_t olen = b[p + 2] | (b[p + 3] << 8);
    if (p + 4 + clen > b.size()) return 2;
    std::vector<std::uint8_t> out(olen + 64);
    const auto got = mapenc::LzoDecompress(b.data() + p + 4, clen, out.data(), out.size());
    if (got != olen) return 3;
    ++blocks; total += got; p += 4 + clen;
  }
  std::cout << "blocks=" << blocks << " bytes=" << total << " trailing=" << (b.size() - p) << "\n";
  return p == b.size() ? 0 : 4;
}
