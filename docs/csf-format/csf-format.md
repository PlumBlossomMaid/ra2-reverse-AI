# RA2/YR 字符串资源（.csf）格式逆向

对应 `ra2md.csf` / `ra2.csf`——红警 2 的文本字符串资源文件
（界面文本、单位名称、提示语等）。本仓库收录了**完整可运行的解析/回写算法**
（`code/csf_reader.py`），算法本身即文档。

> 来源：完整 Python 实现（含解析、保存、批量替换三个方向），见
> `code/csf_reader.py`（本仓库资产）。与存档（.sav）逆向同属资源/数据文件格式线。

## 一句话结论

`.csf` = **FSC 魔数头 + 若干 LBL Label，每个 Label 含若干字符串值**。
字符串值以 **UTF-16LE 编码后逐字节取反**（`~x & 0xff`）存储——纯文本
编辑器打开是乱码，必须按算法还原。

## 文件结构

```
.csf
├── Header      " FSC" (0x20 0x46 0x53 0x43)
│   ├── DWORD Version
│   ├── DWORD NumLabels
│   ├── DWORD NumStrings
│   ├── DWORD (unused)
│   └── DWORD Language
├── Label × NumLabels
│   ├── Header  " LBL"
│   │   ├── DWORD Number_of_string_pairs
│   │   ├── DWORD LabelNameLength
│   │   └── char[LabelNameLength] LabelName
│   └── Value × Number_of_string_pairs
│       ├── " RTS" 或 "WRTS" (4 字节标识)
│       ├── DWORD ValueLength
│       ├── byte[ValueLength*2] Value   ← UTF-16LE 逐字节取反
│       └── (仅 WRTS) DWORD ExtraValueLength + char[ExtraValueLength] ExtraValue
```

## 字段详解

| 字段 | 类型 | 说明 |
|---|---|---|
| 魔数 | `" FSC"` | 注意**前导空格**（0x20 0x46 0x53 0x43） |
| Version | DWORD | CSF 版本号 |
| NumLabels | DWORD | Label 总数 |
| NumStrings | DWORD | 字符串总数 |
| Language | DWORD | 语言枚举（见下表） |
| Label 魔数 | `" LBL"` | 同样有前导空格 |
| LabelName | 原始字节 | 如 `NAME:0001`，ASCII |
| Value 魔数 | `" RTS"` / `"WRTS"` | **RTS 有前导空格、WRTS 没有**——用于区分是否含 ExtraValue |
| ValueLength | DWORD | **字符数**（Value 字节数 = ValueLength × 2） |
| Value | byte[×2] | UTF-16LE 编码后**逐字节取反**（混淆存储） |
| ExtraValue | 字节 | 仅 WRTS 变体，附加数据（如翻译备注） |

### Language 枚举（langMap）

| 值 | 语言 |
|---|---|
| 0 | US |
| 1 | UK |
| 2 | German |
| 3 | French |
| 4 | Spanish |
| 5 | Italian |
| 6 | Japanese |
| 7 | Jabberwockie |
| 8 | Korean |
| 9 | Chinese |
| 10 | Unknown |

## 编码细节（易错点）

1. **逐字节取反**：`stored[i] = ~raw[i] & 0xff`。读取时同样 `~x & 0xff` 还原，
   再 `decode('utf16')`。
2. **UTF-16 编码必须显式指定 LE**：`text.encode('utf-16-le')`——只写
   `'utf16'` 会多出 2 字节 BOM（大小端指示），破坏文件格式。
3. **ValueLength 是字符数不是字节数**：写入时 `len(text)`，读取时
   `f.read(ValueLength * 2)`。
4. **魔数区分**：`" RTS"`（前导空格）vs `"WRTS"`（无空格）——4 字节
   精确比对，不可用字符串 trim 后判断。

## 算法能力（code/csf_reader.py）

| 函数 | 能力 |
|---|---|
| `parse_csf(filename)` | 完整解析 → (header, labels) 对象树 |
| `header.save(f)` / `label.save(f)` / `value.save(f)` | 对象树 → 字节流（**无损回写**） |
| `dump_texts()` | 导出 `[LabelName]Value` 纯文本清单 |
| `read_translate()` | 读取翻译表（`[LabelName]新文本` 格式） |
| `replace_translation()` | 按翻译表批量替换（用于繁体→简体工作流） |

完整繁体→简体流水线（main 演示）：
`parse_csf → dump_texts → 外部转简体 → read_translate → replace_translation → 回写 _sc.csf`

```
python csf_reader.py <source.csf> <translations.txt> [output.csf]
```

## 与社区工具的关系

- 社区主流 CSF 编辑器（如 XCC / RA2CSFEditor）基于同一格式规范
- 本仓库的 Python 实现是**独立完整读写**版本，无需外部工具即可验证
  （解析 + 回写字节一致即验证通过）

## 验证方式

- 对任意 `ra2md.csf` 运行 `parse_csf` → 统计 NumLabels/NumStrings 与头部一致
- `parse_csf` → `save` 回写 → 与源文件二进制比对（应完全一致，验证无损）

## 取证文件

| 文件 | 内容 |
|---|---|
| `code/csf_reader.py` | 完整实现（解析 + 保存 + 翻译替换，含 main 演示） |
