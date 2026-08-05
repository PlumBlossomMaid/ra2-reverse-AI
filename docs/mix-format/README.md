# MIX 打包格式区

Westwood 游戏资源打包格式（.mix）——`rulesmd.ini`、音效、动画等资源都
封装在 .mix 里。**本区暂未做自有解析，指向社区成熟工具。**

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

## 未完成

- 自有 .mix 格式解析（目录表/Blowfish/哈希命名）——当前策略：用 ccmix
  提取，不重复造轮子；如后续需要深入可参考 ccmix 源码（GPL 可借鉴思路）
