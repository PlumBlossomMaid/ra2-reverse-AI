# Ghidra Jython 脚本踩坑清单

Ghidra headless 用 Jython 2.7，与 CPython 差异巨大。以下每条都是实际踩过的坑。

## 语法/API 差异

1. **`int()` 不认 0x 前缀**
   ```python
   int("0x4c9c70")          # ValueError!
   int("0x4c9c70".lstrip("0x"), 16)  # 正确
   ```

2. **`open(..., encoding=)` 不支持**
   ```python
   open(OUT, "w", encoding="utf-8")  # TypeError
   codecs.open(OUT, "w", "utf-8")    # 正确
   ```

3. **`True/False` 不能强转 ghidra SourceType**
   ```python
   createLabel(addr, name, False)      # 报错
   createLabel(addr, name, SourceType.USER_DEFINED)  # 正确
   ```

4. **`DefinedDataIterator.getDefinedDataIterator()` 不存在**（Ghidra 11.1）
   字符串遍历需换 API。

5. **`mem.getBytes(addr, n)` 有重载陷阱**
   Jython 会选错重载（`byte[]` 版本）。用 `mem.getInt(addr, False)` 读 4 字节。

## Windows 平台坑

6. **文本模式写 `\r\n`**
   `open(...,"w")` 写出的行尾是 `\r\n`，读回必须 `rstrip("\r\n")`，
   否则 `"FUNC"` 变 `"FUNC\r"` 导致字符串比较失败。

## Ghidra 行为坑

7. **函数边界可能缺失**
   小函数（如 `GetCostPerStep @ 0x4CA180`）夹在函数之间时 Ghidra 不建函数，
   用 `CreateFunctionCmd(addr).applyTo(...)` 强制创建。

8. **vtable 槽位可能指向纯虚桩**
   抽象基类的 vtable 中多个槽位指向同一 `return 0` 函数（如 TechnoClass vtable
   `0x7F4960` 中 `[0x0C]/[0x2C]/[0x30]` 都指向 `0x4C9150`）。
   判定派生类行为要看实际对象的 vtable，不是抽象基类的。

9. **反编译对浮点逻辑会失真**
   `FCOMPP` 比较方向、钳制条件容易被反编译器搞乱。
   处理 float/double 时必须 dump 汇编逐条核对（`dump_*_asm.py`）。

10. **全局常量值必须从 PE 实测**
    反编译器标 `_DAT_00XXXXXX`，值别信反编译注释。
    且必须区分 float/double：`0x7E1718` 是 double 1.0，
    按 float 读只有 0.0（`read_constants.py` 可验证）。

11. **`os`/`_REPO` 不是内置的**
    部分历史脚本直接 `os.path.join(_REPO, ...)`，依赖旧会话注入的环境变量，
    新会话 headless 环境没有——报 `NameError: name 'os' is not defined`。
    新脚本一律显式：
    ```python
    import os
    _REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ```
    （脚本位于 `<repo>/code/ghidra_scripts/`，上两级即仓库根）

12. **字符串 xref 可能混入相邻字符串的引用**
    .data 里字符串连续存储时，`getReferencesTo(0x829abc)` 会把引用同一 data
    块内相邻地址（如 0x82bb24"RandomMap"）的指令也算进"RandMap.img"的 xref。
    判真伪要看引用指令 PUSH/MOV 的立即数是否精确等于目标地址。
    顺带：这也让"找字符串引用者"变成"找整个字符串块引用者"，按引用地址
    聚类（如全部落在 0x597xxx 区）反而能直接定位相关类的方法区。

## 常用运行命令

```
analyzeHeadless.bat E:\code\ra2-reverse\ghidra_proj RA2 ^
  -process gamemd.exe -noanalysis -postScript script.py ^
  -scriptPath E:\code\ra2-reverse-AI\code\ghidra_scripts
```

`-noanalysis` 跳过重新分析（已有分析结果时），只跑脚本，速度快很多。
