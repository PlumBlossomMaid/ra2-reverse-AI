# 记忆区

逆向过程的**知识沉淀与原始取证数据**——踩坑记录、地址笔记、反编译输出。

```
memory/
├── README.md               ← 本文件
├── notes/                  ← 知识笔记（可长期复用）
│   └── ghidra-jython-pitfalls.md   # Ghidra Jython 踩坑清单
└── data/                   ← 原始取证数据（不可再生的逆向产物）
    ├── symbols/            # 三层符号表（yrpp_symbols.tsv 等）
    ├── decomp/             # 反编译/汇编输出（生产系统取证）
    └── bilibili/           # B 站 RA2 逆向系列文章（参考资料）
```

## 使用规范

- `data/` 是不可再生资产（需要 Ghidra + 原版二进制才能重新生成），
  修改需谨慎
- `notes/` 是经验教训，新踩坑随时补充
- 文档区（docs/）引用这里的取证文件作为证据链

## 溯源

- 反编译取证：`memory/data/decomp/*.txt`
- 符号表：`memory/data/symbols/*.tsv`
- Ghidra 工程本体在本地 `E:\code\ra2-reverse\ghidra_proj\RA2`（体积大，不入库）
