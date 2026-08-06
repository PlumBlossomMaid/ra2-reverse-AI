# 工具链与环境踩坑清单

本轮（随机地图生成器逆向）实际踩到的坑，写脚本前必读。

## Shell 转义层

1. **写文件时双反斜杠会被折叠成单个**

   用 heredoc 写文件时，`\` 落盘变成 `\`，而单个 `\` 原样保留。
   后果极隐蔽——正则 `rb"[A-Za-z]:\[ -~]+"` 落盘成 `:\[`，
   regex 把 `\[` 当成字面量 `[`，**静默匹配 0 条**（不报错，只是结果为空）。

   对策：
   - 正则里的路径分隔符写成字符类 `[\x5c]`，别写反斜杠
   - C++ 源码里 `"\n"` 安全（单反斜杠），`"\\"` 不安全
   - 需要双反斜杠时用 Edit 工具直接改文件，绕开 shell

2. **命令行参数里的 Windows 路径同样被吞**

   `-scriptPath E:\code\x` 会变成 `E:codex`（连单反斜杠也在参数解析时丢失）。
   对策：**一律用单引号包正斜杠路径**：`'E:/code/ra2-reverse-AI/code/ghidra_scripts'`，
   Ghidra/Java/cmd 都接受正斜杠。

3. **`cd /d E:\path` 在 bash 里报 "too many arguments"**

   这是 git-bash 不是 cmd。用 `cd /e/code/...` 或直接在仓库根跑。

## 编码

4. **终端是 GBK，`print` 中文 UTF-8 内容会抛 `UnicodeEncodeError`**

   Python 脚本往 stdout 打中文会崩。对策：结果写文件（UTF-8）再用 Read 工具看，
   别指望终端。Ghidra 脚本一律 `codecs.open(OUT, "w", "utf-8")` 落盘。

## 原版二进制的执行验证

5. **进程内映射 gamemd.exe 到 0x400000 行不通**

   `gamemd.exe` 代码含绝对地址（常量表 `0x839644` 等），必须映射到原始基址才能跑。
   但 64 位 Windows 上宿主进程堆在**加载器阶段**就已铺满 `0x400000-0x6C0000`：
   - `VirtualAlloc(0x400000, ...)` 返回 `ERROR_INVALID_ADDRESS(487)`
   - 即使 `/BASE:0x30000000 /DYNAMICBASE:NO /FIXED` 把宿主挪走也没用
   - 连 `/ENTRY:PreMain` 抢在 CRT 初始化前预留都来不及（加载器更早）

   对策：**用 Unicorn 指令级模拟**（`code/randmap/emu_oracle.py`）。
   映射节 → 建栈 → 设哨兵返回地址 → `emu_start`，不受宿主地址空间限制，
   而且全程只读文件，更符合"只做静态分析"的边界。

6. **`__thiscall` 在模拟/harness 里怎么调**

   - 模拟器：直接设 `ECX = this`，参数按从右往左压栈，压入哨兵返回地址
   - C++ harness：用 `__fastcall(void* ecx, void* edx_unused, ...)` 模拟，
     第二个占位参数吃掉 EDX，其余参数自然走栈

## MSVC

7. **`vcvars32.bat` 报 `vswhere.exe` 找不到但仍能用**

   报错可忽略，环境实际初始化成功（会打印 `Environment initialized for: 'x86'`）。

## `.bss` 里的表怎么取到内容

8. **"`.bss` 全局 = 运行期由 theater/ini 决定"这个假设会骗人**

   `.bss` 在文件里没有内容，看不到值。但**不代表它是运行期数据**——
   MSVC 的 C++ 静态初始化器（namespace 作用域对象的构造）也会填 `.bss`，
   而且是在 `WinMain` 之前由 CRT `_initterm` 跑掉的。

   典型症状：这类初始化器**没有任何 `call` 指向它**（`E8 rel32` 全扫零命中），
   只有一个函数指针躺在 `.data` 的初始化器指针表里。

   取内容的三步：

   1. 先 `xref_abs.py` 找到唯一的**读**点，确认表基址与步长
   2. 在整个文件里搜 `struct.pack("<I", 函数VA)`——命中在 `.data` 的一片
      连续函数指针表里，就说明它是 CRT 静态初始化器
   3. `emu_oracle.py` 的 `run_range(初始化器起, 结尾 ret)` 直接跑，
      跑完 `uc.mem_read(表基址, 长度)` dump 出来

   实例：岸线贴砖的落点偏移表 `[0xABDB64]`（43 项 `{int16 dx, int16 dy}`），
   初始化器 `0x57A9C0`，指针在 `0x813AB4`。见
   `docs/random-map/stage-seeding-water.md` §七末与 `memory/data/rmg/shore_tables.txt`。

9. **判"这张表随不随 theater 变"要看它在哪个节**

   `0x82A7F4` / `0x82A89C` 这两张 42 项表一度被记成"`.bss`，由 theater 决定"，
   实际在 `.data`——写死在 exe 里的 `int[42]`，直接读文件就有。
   写文档前先用 `read_const.py` 试读一下：读得出来就是静态数据，读不出来才是 `.bss`。
