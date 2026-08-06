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

10. **凡涉及 x87 栈的结论，必须模拟执行验证——"读一眼像 bug"的误判率很高**

    x87 是栈机：`fld` / `fcomp` / `fstp` / `faddp` 会移动栈顶，
    同一条 `fld dword ptr [esp+0x10]` 在不同控制流里喂给的是**不同的下游消费者**。
    只盯单条指令、不跟栈深度，很容易把正常代码读成"复制粘贴 bug"。

    实例（已留档在 `docs/random-map/stage-init.md` §三之二末）：
    `0x5996D7` 的 `fld [esp+0x10]` 被我读成"`MapType 3/4` 时宽度插值误用了 `ht`"，
    还写进了 CLAUDE.md 怪癖清单。实际上那条路径汇合后 `ht` 一直留在栈上，
    是给**高度**插值用的（`fsub st(2)` / `fmul st(2)`），
    宽度插值读的是内存栈槽 `[esp+0x14]`。

    证伪只花了一次模拟执行：**固定一个输入、只动另一个**，
    看该变的变没变。`Width=0` 固定、`Height` 扫一遍，`W` 恒为 70 → 推测不成立。

    立规矩：**登记怪癖之前先跑一次 oracle**。
    怪癖清单是"必须照抄"的硬约束，往里塞错东西比漏掉更糟——
    漏掉只是不知道，塞错会让复刻主动偏离原版。

11. **"精确算术"不是浮点的正确答案——建模必须逐步舍入**

    用 `fractions.Fraction` 建模浮点是个好办法，但**只在每一步之后都显式截断**
    才对。直接把整条表达式当有理数算，会得到"数学上正确、原版给不出"的值。

    实例：`H = 70×(1−t) + 80×t`，`t` 是被夹到 1.2 的插值系数。
    精确有理数下这个式子恒等于 `70 + 10t`，`t = 6/5` 得整整 **82**。
    但 double 里的 1.2 是 `1.1999999999999999556`，两次乘法一次加法各 chop 一轮，
    结果是 `81.99999999999999`，`_ftol` 截断成 **81**——原版给的就是 81。
    全网格 1260 例里，"精确算术"错 252 例。

    同一份数据还量化了舍入模式的分量：全程 round-to-nearest 错 **420 例**。
    也就是说 `RC=chop` 和"逐步舍入"是两条**独立**的硬约束，缺一条都不行。

    正确姿势（`chop_to(fr, prec)` 按位截断到 24 / 53 位尾数）：

    ```python
    def chop(fr, prec):                    # 向零截断到 prec 位尾数
        if fr == 0: return Fraction(0)
        s, a, e = (-1 if fr < 0 else 1), abs(fr), 0
        while Fraction(2)**e > a: e -= 1
        while Fraction(2)**(e+1) <= a: e += 1
        q = Fraction(2)**(e - (prec-1))
        return s * ((a // q) * q)
    ```

    好处是**结论与宿主 FPU 无关**——不必相信本机 x87 / SSE / ARM 的行为，
    也不必担心 Unicorn 哪条指令没模拟对（超越指令那个坑见 README §模拟器失效面）。
    对拍时拿这套模型和模拟器互证：两边一致才算实锤。

12. **写死浮点常量时要区分"编译期"和"运行期"舍入**

    `fesetround(FE_TOWARDZERO)` 管的是**运行期**。源码里写 `static_cast<float>(1.2)`
    是常量表达式，编译器在编译期按 IEEE 默认的就近舍入折叠掉，得 `1.2000000476837158f`；
    而原版 `fstp dword` 在 RC=chop 下得 `1.1999999284744263f`。差一个 ulp，
    过一遍插值就是 W 差 1 格。

    规矩：**凡是原版由运行期舍入产生的常量，源码里直接写死目标值**，
    并在注释里说明它是哪种舍入的产物。见 `code/rewrite/rmg/map_size.cpp` 的 `kClampF`。

13. **找函数调用点：`xref_abs.py` 和线性反汇编**两个都不行****

    这一条差点让我得出"这个函数从来没被调用过"的错误结论——而它实际有 96 处调用。

    - **`xref_abs.py` 搜绝对 4 字节**：对全局变量是对的（MSVC 6 全局一律绝对寻址），
      但 `call 0x578350` 编码成 `E8 <rel32>`，镜像里**根本不存在** `50 83 57 00`
      这四个字节。所以它报 0 处引用，而真相是 96 处。
    - **线性反汇编整个 `.text` 再找 `call`**：`.text` 里混着跳转表、只读数据和
      对齐填充，capstone 从头顺着解会在那里**失步**，之后一大段指令边界全错。
      同一次扫描一处 `call 0x578350` 都没找到。

    正解是**按字节扫 + 校验落点**，不依赖任何指令边界：

    ```python
    if data[i] in (0xE8, 0xE9) and sec_va + i + 5 + rel32(i+1) == target:
    ```

    已落成 `code/randmap/xref_rel32.py`。**判死代码时两个脚本必须都跑**：
    绝对值搜负责虚表项/跳转表项/函数指针赋值，rel32 搜负责直接 call 与尾调用 jmp。
    （[rng-ledger.md](../../docs/random-map/rng-ledger.md) §二判那 22 个死函数时
    两条都查了，结论仍然成立。）

    代价是 rel32 扫有假阳性（数据里凑出合法 rel32）。判据看 `E8` 前一条是不是
    参数传递——真调用点前面基本都是 `mov ecx, <this>` 或 `push`。

