# CSF 字符串资源格式区

RA2/YR 文本资源（.csf）格式逆向——FSC 头 + LBL Label + UTF-16LE 取反编码。

## 索引

| 文档 | 内容 | 状态 |
|---|---|---|
| [csf-format.md](csf-format.md) | .csf 完整格式：FSC 头 + Label/Value 结构 + 字节取反编码 + 字段表 | ✅ 已完成（算法完整） |

## 资产

- `code/csf_reader.py`：完整可运行实现（解析 + 无损回写 + 繁简翻译替换）
