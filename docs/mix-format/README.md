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

## 自有解析器：Python 一套 + C++ 一套

两套**独立实现**，互为对拍基准：

| 实现 | 位置 | 用途 |
|---|---|---|
| Python | `code/mix/mix_read.py` | 逆向期取数据、生成基准档 |
| C++ | `code/rewrite/mix/` | 命令行程序运行期自己读 mix，不依赖 Python |

C++ 那套由四个文件组成，全部自写、不引第三方库：

| 文件 | 内容 |
|---|---|
| `blowfish.{h,cpp}` | Blowfish ECB。初始 P/S 表**按定义就是 pi 的十六进制小数位**，由 `code/mix/gen_blowfish_tables.py` 用 Machin 公式现算成 `blowfish_tables.h`，没有从任何第三方实现里搬常量；自检 `P[0] == 0x243F6A88` |
| `bignum.{h,cpp}` | 够用就好的无符号大整数（32 位肢、移位-相减长除法）。只为那两次 RSA 公钥运算服务，一张 mix 算两块，性能无所谓 |
| `mix_archive.{h,cpp}` | CRC-32 名字哈希 + 目录解析 + 嵌套 mix |
| `tmp_file.{h,cpp}` | `.tmp` 的几何/属性字段（不碰像素、不碰调色板） |

回归 `code/rewrite/mix/test/test_mix.cpp` 分两层：

1. **纯算法层**（任何机器都跑）：五组公开的 Blowfish ECB 测试向量 + 56 字节
   长密钥一组 + 名字哈希的已知值 `MixId("ra2md.csf") == 0xBD835079`。
   能对上这些向量，说明 pi 算对了、轮函数也对。
2. **真档层**（需要环境变量 `RA2_GAME_DIR` 指向原版目录，否则 `SKIP`）：
   打开 `ra2.mix`（头加密 + 带校验和）→ 嵌套 `isotemp.mix`（不加密，1035 条）→
   解 `water09.tem` / `shore04.tem` / `cliff01.tem` 三块砖的尺寸与空洞数。
   仓库里不放游戏数据，所以 CI 上这一层是跳过的。

另有一层装载器 `code/rewrite/assets/theater_assets.{h,cpp}`，把
「mix → theater ini → 前缀和 → 逐砖 `.tmp`」整条链串起来，回归
`test_theater_assets.cpp` 拿 Python 侧生成的 `memory/data/rmg/tile_geometry.txt`
逐字段对拍——**两套独立实现互相印证**，mix 头解密 / 名字哈希 / `.tmp` 头 /
前缀和四处任一出错都会被抓住。

### Python 那套的实现说明

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

## 顺带：`.tmp` 砖块文件头

随机地图的贴砖器要读砖的**占地格数**，那不在 ini 里，在 `.tmp` 文件头。
砖块在 `ra2.mix` → `isotemp.mix`（温和）/ `isosnow.mix`（雪地）里，
名字就是 ini 里 `FileName` 加两位序号加 theater 后缀，例如 `shore04.tem`。

| 偏移 | 含义 |
|---|---|
| `+0x00` | 占地宽（格）。原版 `0x546B46` 用 `mov al` **按字节**读 |
| `+0x04` | 占地高（格），同上 |
| `+0x08` / `+0x0C` | 单块像素宽 / 高（实测恒为 60 / 30） |
| `+0x10` 起 | `宽 × 高` 个 dword 偏移；**为 0 表示该子格是空洞** |

子块头里贴砖器只用 `+0x28` 一个字节（该子格的高度增量）。
装载时 `0x546B66`-`0x546B80` 会把偏移表里的相对偏移就地改成绝对指针。

实测：`water09.tem` 是 `1×1`、`shore04.tem` 是 `1×2`、`shore42.tem` 是 `9×5`；
`cliff01.tem` 的 `2×3` 里有两个空洞。用法见
[../random-map/stage-seeding-water.md](../random-map/stage-seeding-water.md) §七。

## 未完成

- 写入/打包（本项目用不到）
- 文件名恢复（id 是单向哈希；需要 XCC 的 local mix database.dat）
