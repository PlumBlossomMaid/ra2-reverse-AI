// base64 编码 —— 逐位复刻 gamemd.exe 0x42FD30。
//
// 地址依据（Ghidra 反编译 memory/data/decomp/rmg_lzo_dec_b64core.txt + oracle 实测）：
//   0x42FD30 编码（3→4 映射；余 1 字节 → 2 字符 + 2 '='；余 2 字节 → 3 字符 + 1 '='）
//   编码表 @ 0x008189C8 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
//   填充字符 @ 0x00818A0C = '='
//   解码表 @ 0x7E3914：0xFE 无效 / 0xFF 结束（'='）
//
// 与标准 base64 完全一致（RFC 4648 alphabet，'=' 填充）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mapenc {

// base64 编码（标准表 + '=' 填充）。与 0x42FD30 输出一致（除尾部 '\0' 终止符）。
std::string Base64Encode(const uint8_t* src, size_t src_len);

// 便捷重载。
std::string Base64Encode(const std::vector<uint8_t>& src);

}  // namespace mapenc
