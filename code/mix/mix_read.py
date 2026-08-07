# -*- coding: utf-8 -*-
"""Westwood .mix 归档的只读解析器（TS/RA2/YR 变体）。

为什么自己写：docs/mix-format 原先的策略是"用 ccmix 提取，不重复造轮子"，
但本机没有 ccmix，而 theater 的 TileSet 起始砖号只能从 mix 里的
TEMPERATMD.INI / SNOWMD.INI 拿。这个模块只读不写，也不把游戏数据带进仓库。

格式（新版头，TS 起）：
  dword flags        低 16 位为 0 才是新版头；0x00010000=带校验和，0x00020000=头加密
  [加密时] 80 字节密钥源 -> 56 字节 Blowfish 密钥 -> ECB 解密下面的头
  word  count        条目数
  dword data_size
  count × { dword id; dword offset; dword size }   offset 相对数据区起点

文件名不入档，只存 32 位 id，所以只能"猜名字再算 id"去查。
"""
import struct
import sys
import zlib

def _pad(name):
    """TS/RA2 的名字预处理：大写，再按 4 字节对齐补齐（补法很别扭，照抄）。"""
    s = bytearray(name.upper().encode("ascii"))
    n = len(s)
    a = n >> 2
    if n & 3:
        s.append(n - (a << 2))
        for _ in range(3 - (n & 3)):
            s.append(s[a << 2])
    return bytes(s)


def mix_id(name):
    """标准 CRC-32（有初值/末尾取反）作用在补齐后的大写名字上。

    实测依据：langmd.mix 里 id=0xBD835079 的条目内容以 " FSC" 开头（CSF 魔数），
    而 zlib.crc32(_pad("ra2md.csf")) 正好是 0xBD835079。"""
    return zlib.crc32(_pad(name)) & 0xFFFFFFFF


class MixFile(object):
    def __init__(self, data):
        self.data = data
        flags = struct.unpack_from("<I", data, 0)[0]
        if flags & 0xFFFF:
            # 老版头：直接是 count/data_size
            off, self.encrypted, self.checksum = 0, False, False
        else:
            off = 4
            self.encrypted = bool(flags & 0x00020000)
            self.checksum = bool(flags & 0x00010000)
        if self.encrypted:
            hdr, body_off = self._decrypt_header(data, off)
        else:
            hdr, body_off = data[off:], off
        count, _size = struct.unpack_from("<HI", hdr, 0)
        self.entries = {}
        for i in range(count):
            eid, eoff, esz = struct.unpack_from("<IiI", hdr, 6 + i * 12)
            self.entries[eid & 0xFFFFFFFF] = (eoff, esz)
        if self.encrypted:
            self.base = body_off + 80 + ((6 + count * 12 + 7) // 8) * 8
        else:
            self.base = body_off + 6 + count * 12

    def _decrypt_header(self, data, off):
        from Cryptodome.Cipher import Blowfish
        key = blowfish_key(data[off:off + 80])
        enc = data[off + 80:]
        n = (len(enc) // 8) * 8
        return Blowfish.new(key, Blowfish.MODE_ECB).decrypt(enc[:n]), off

    def has(self, name):
        return mix_id(name) in self.entries

    def get(self, name):
        e = self.entries.get(mix_id(name))
        if e is None:
            return None
        return self.data[self.base + e[0]:self.base + e[0] + e[1]]


# Westwood 的公钥（社区通行的那一串），用来把 80 字节密钥源还原成 Blowfish 密钥
_PUBKEY_B64 = "AihRvNoIbTn85FZRYNZRcT+i6KpU+maCsEqr3Q5q+LDB5tH7Tz2qQ38V"


def _pubkey():
    import base64
    blob = base64.b64decode(_PUBKEY_B64 + "==")
    assert blob[0] == 0x02, "unexpected key blob"
    ln = blob[1]
    # 模数按**大端**读（小端读出来只有 317 位，且首块密文就 >= n，解不动）
    return int.from_bytes(blob[2:2 + ln], "big"), 65537


def blowfish_key(src80):
    # 明文块长 = (模数位数 - 1) / 8 = (317 - 1) / 8 = 39 字节；
    # 两个 40 字节密文块解出 78 字节，取前 56 字节当 Blowfish 密钥。
    n, e = _pubkey()
    blk = (n.bit_length() - 1) // 8   # 319 位 -> 39 字节明文块
    out = b""
    for i in (0, 40):
        m = int.from_bytes(src80[i:i + 40], "little")   # 密文块小端
        out += pow(m, e, n).to_bytes(blk, "little")
    return out[:56]


def open_mix(path):
    with open(path, "rb") as f:
        return MixFile(f.read())


if __name__ == "__main__":
    m = open_mix(sys.argv[1])
    print("entries=%d encrypted=%s checksum=%s base=0x%X"
          % (len(m.entries), m.encrypted, m.checksum, m.base))
    for name in sys.argv[2:]:
        e = m.entries.get(mix_id(name))
        print("%-24s id=0x%08X %s" % (name, mix_id(name),
                                      ("off=0x%X size=%d" % e) if e else "MISSING"))
