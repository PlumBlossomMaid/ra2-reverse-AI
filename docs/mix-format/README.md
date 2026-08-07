# MIX 打包格式区

Westwood 游戏资源打包格式（.mix）——`rulesmd.ini`、音效、动画等资源都
封装在 .mix 里。**已有一个只读的自有解析器**（`code/mix/mix_read.py`），
外加下面这个社区工具作为参照。

## 外部工具：ccmix

- **仓库**：https://github.com/OmniBlade/ccmix
- **许可证**：GPL v2 or later（基于 tsunmix 开发）
- **定位**：Westwood Studios .mix 格式的命令行创建/提取工具

### 支持的游戏变体（--game）

| 变体 | 覆盖 |
|---|---|
| `td` | C&C / Sole Survivor |
| `ra` | Red Alert（支持加密/非加密文件头） |
| `ts` | Tiberian Sun（支持加密/非加密文件头） |
| `ra2` | 红警 2 / 尤里的复仇 |

### 用法

```
ccmix --mode --mix /path/to/file.mix
```

| 模式 | 作用 |
|---|---|
| `--list` | 列出 mix 内容 |
| `--extract` | 提取文件（`--file` 指定单个，默认全部） |
| `--create` | 从目录创建 mix（`--dir` + `--mix`） |
| `--add` / `--delete` | 重建 mix 并增/删指定文件 |

| 选项 | 作用 |
|---|---|
| `--game td\|ra\|ts\|ra2` | 指定游戏变体（默认 td） |
| `--encrypt` | Blowfish 加密文件头（ra 及以上） |
| `--checksum` | 含 SHA1 校验和（ra 及以上） |
| `--lmd` | 生成 XCC 格式本地 mix database.dat（恢复单向哈希文件名） |

### 用途（与本仓库的关系）

- 从 `ra2md.mix` / `ra2.mix` 提取原版 `rulesmd.ini`、`artmd.ini` 等
  规则文件（本仓库 `memory/data/rules/rulesmd.ini` 即来自 mix）
- 分析 mod 的 mix 结构（mod 通常以 mix 形式发布）

## 自有解析器 `code/mix/mix_read.py`

只读，不写、不解包到仓库里。写它的直接原因是本机没有 ccmix，而
[随机地图的 theater 数据](../random-map/theater-data.md)只能从
`ra2md.mix` → `localmd.mix` → `TEMPERATMD.INI` 拿。

新版头（TS 起）的结构：

```
dword flags        低 16 位为 0 才是新版头；0x00010000=带校验和，0x00020000=头加密
[加密时] 80 字节密钥源 -> RSA 解出 56 字节 Blowfish 密钥 -> ECB 解密下面的头
word  count
dword data_size
count × { dword id; dword offset; dword size }    offset 相对数据区起点
```

### 踩到的两个坑（都靠实测定下来）

1. **文件名哈希是标准 CRC-32**（有初值取反、末尾取反），不是"XCC 那种裸表驱动"。
   判据：`langmd.mix` 里 `id=0xBD835079` 的条目内容以 `" FSC"`（CSF 魔数）开头，
   而 `zlib.crc32(pad("RA2MD.CSF"))` 正好是 `0xBD835079`。
   名字先大写、再按 4 字节对齐补齐（补法很别扭，照抄）。
2. **RSA 公钥模数按大端读，密文块按小端读。** 模数小端读出来只有 317 位，
   且首个密文块就 `>= n`，根本解不动；大端读是 319 位，两块都能解，
   明文块长 `(319-1)/8 = 39` 字节，两块共 78 字节取前 56 当 Blowfish 密钥。

### 验证

`ra2md.mix` 解出 25 条内嵌 mix，末条的 `offset + size` 与文件实际大小
只差 29 字节（尾部对齐），`multimd.mix` 差 1 字节——头解对了。

## 未完成

- 写入/打包（本项目用不到）
- 文件名恢复（id 是单向哈希；需要 XCC 的 local mix database.dat）
