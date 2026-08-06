# 原版数学基元：查表 sqrt 与正态分布

> 状态：**已实锤并逐位对拍**。C++ 复刻在 `code/rewrite/rmg/fast_math.{h,cpp}`，
> 回归 `code/rewrite/rmg/test/test_fast_math.cpp`
>（74 组 sqrt + 16384 项表自检 + 101 组 ChopLog 黄金值 + 5 种子 × 16 正态）。
>
> §四记录了本模块唯一一次**结论被推翻**的经历：`fyl2x` 的语义先在 80 个样本上
> 得出错误结论，后被 20000 样本 + 真 CPU 探针纠正。方法论教训见 §4.6。

地图上那些不规则的海岸线，数学上只由两样东西决定：**候选格的优先级排序**，
以及**引力中心的随机游走**。前者用 `FastSqrt`，后者用正态分布发生器。
两者都不能用标准库替代——`std::sqrt` 与原版系统性不等，
`std::log` 的舍入方向与原版相反。

## 一、`FastSqrt` @ `0x4CAC40`——16384 项查表，只有 14 位精度

原版根本没调用 `fsqrt`。它把 float 的位形拆开，**用尾数高 14 位查表**：

```c
double FastSqrt(double x) {
    if (x == 0.0) return 0.0;             // 0x4CAC44 fcom 0.0f
    if (x <  0.0) x = x * -1.0;           // 0x4CAC67 fmul [0x7E4900]（-1.0）
    float  f = (float)x;                  // 0x4CAC6D fstp dword —— 受 RC=chop
    uint32 bits = bit_cast(f);
    uint32 m = bits & 0x7FFFFF;
    int16  e = (int16)((bits >> 23) - 0x7F);
    if (e & 1) m |= 0x800000;             // 奇指数：把 2 折进尾数
    uint32 out = table[m >> 10] + ((uint32)((int16)(e >> 1) + 0x7F) << 23);
    return (float)bit_cast(out);          // 0x4CACA9 fld dword —— 返回值是 float
}
```

表在 `0x8650BC`，16384 项 × 4 字节。误差实测最大 **5.7e-5**，正好是 2⁻¹⁴——
即结果只有约 14 位有效尾数，与 `std::sqrt` 在绝大多数输入上都不相等。

### 表可以重算，不必搬运二进制资产

索引 `i` 对应尾数区间下界 `m = i·1024`，被开方数

```
m <  2²³ :  v = 1 + m/2²³        （偶指数）
m >= 2²³ :  v = 2m/2²³           （奇指数）
```

而 `sqrt(v)·2²³ = sqrt(v·2⁴⁶)` 恰好是整数开方，于是

```
table[i] = isqrt( m >= 2²³ ? 2m·2²³ : (2²³+m)·2²³ ) - 2²³
```

**纯整数运算，16384/16384 与 exe 中的表逐位相同**（脚本验证）。

> 这也说明表本身就是**数学常量**，不是"作品"。

复刻代码把这张表**写死**在 `code/rewrite/rmg/sqrt_table.h`，而不是运行期现算：
CI 目标可能是 x86 / ARM / 龙芯，写死让"结果与架构无关"成为**编译期就成立的事实**。
回归测试 `FastSqrt.TableIsPureMath` 用纯整数 `isqrt` 把 16384 项重算一遍做自检，
既证明它是数学常量、又保证写死的内容没有搬运痕迹。

## 二、均匀源 @ `0x598000`

```c
double U() { return (double)(int64)(uint32)Randomizer[0xABE890].Random()
                    * (1.0 / 4294967295.0); }   // 常数 0x7ED898，位形 3DF0000000100000
```

`fild qword`（先补成 64 位有符号）是 x87 没有无符号转换的标准绕法，
与 `docs/random-map/fpu-and-random-idioms.md` 里的取数惯用法同源、同常数。

## 三、正态分布 @ `0x5980C0`——Marsaglia 极坐标法

对象在 `0xABDFB8`，由 `FUN_00598960` 在给全局 Randomizer 播种时一并初始化：

| 偏移 | 内容 | 初值 |
|------|------|------|
| `+0x00` | char 缓存标志 | 0 |
| `+0x08` | double 缓存值 | 0.0 |
| `+0x10` | 均匀源函数指针 | `0x598000` |

```c
double Normal() {
    if (have) { have = 0; return cached; }
    double u, v, s;
    do {
        u = 2*U() - 1;                    // 0x5980E1 fadd st,st / fsub 1.0
        v = 2*U() - 1;
        s = v*v + u*u;                    // 顺序：先 v*v
    } while (!(s < 1.0) || s == 0.0);     // 落在单位圆外或正中心则整轮重抽
    double f = FastSqrt((-ln(s) - ln(s)) / s);   // = sqrt(-2·ln(s)/s)
    have = 1; cached = f * v;
    return f * u;
}
```

三个必须照抄的细节：

1. **一次算两个，缓存第二个。** 调用次数的奇偶性直接改变随机数流的消耗节奏，
   省略缓存会让后续整张地图偏移。
2. **拒绝会多消耗随机数。** 每轮吃 2 个 `Random()`，落在单位圆外就再来一轮。
3. **`ln` 用 x87 `fyl2x`**（`0x598126: fldln2 / fxch / fyl2x`），不是 `std::log`——
   舍入方向就不同（截断 vs 就近），约半数取值差 1 ulp。详见 §四。

## 四、`fyl2x` 的语义——一次被自己的对拍推翻的结论

这一节记录了一个**先下错结论、后被更强的证据推翻**的完整过程，因为教训比结论重要。

### 4.1 错误的第一版结论

最初用 Unicorn 模拟执行 `fyl2x`，与「高精度精确 ln 再向零截断」对拍 **80/80 一致**，
于是写下"`fyl2x` ≡ 精确 ln 向零截断"，并据此在 x86 上内联同一条指令了事。

**这个结论的证据是坏的。** 后来把样本量从 80 提到 20000 时才发现：

- Unicorn 的 `fyl2x` **完全不理会 FPU 控制字**——`PC=53/RC=chop`、`PC=53/RC=near`、
  `PC=64/RC=chop`、`PC=64/RC=near` 四种组合返回**完全相同**的值；
- 它返回的其实是宿主的 long double 计算结果，接近 `near53(精确 ln)`。

也就是说：**模拟器在这条指令上是坏的 oracle**，80 个样本恰好没撞上分歧点而已。

### 4.2 去真 CPU 上取真值

写了只读探针 `build/scratch/fyl2x_probe.cpp`：32 位原生程序，
`_controlfp(_RC_CHOP, _MCW_RC)` 后内联 `fldln2 / fld / fyl2x / fstp`，
打印 20000 组 `(输入位形, 结果位形)`。实测确认进入 `main` 时控制字为 `0x027F`，
设置后为 **`0x0E7F`**——与原版 WinMain `_control87` 的效果一致。

用任意精度（`Decimal` 90 位 + `Fraction` 精确截断）逐条判定，结果：

| 模型 | 与真 CPU `fyl2x` 的分歧 |
|------|------------------------|
| `chop53(精确 ln)` | **7 / 20000 = 0.035%** |
| `chop53(near64(精确 ln))` | 8 / 20000 |
| `near53(精确 ln)`（即 `std::log`） | 10005 / 20000 ≈ 50% |

7 例分歧的形态高度一致：**`fyl2x` 一律比精确截断低 1 ulp**，
且精确 ln 距离 53 位截断边界的余量只有 0.00012 ~ 0.0004 个 double ulp
（换算成 80 位 ulp 是 0.2 ~ 0.4 个）。

> 真正的结论：`fyl2x` 内部按 64 位近似算 `log2` 再乘 `ln2`，然后按 PC=53/RC=chop 截断。
> 它**本身就不是正确舍入的**——当精确值离截断边界不足半个 80 位 ulp 时，
> 内部误差会把它推到边界另一侧，于是多截一步。

存档：`memory/data/rmg/fyl2x_native.txt`（全量）、`fyl2x_mismatch.txt`（7 例分歧）。

### 4.3 由此改变的设计：所有架构都不用本机 `fyl2x`

既然 `fyl2x` 不是正确舍入的，那"以本机 `fyl2x` 为准"就等于**把输出绑死在具体 CPU
实现上**——x87 超越指令的低位行为属于实现细节，不同厂商/不同代次是否一致**未确认**，
而这正是最难排查的一类不可复现性。

所以 `ChopLog` 的语义被定为唯一确定的数学值：

> **`ChopLog(s)` ≡ 精确 `ln(s)` 向零截断到 double。**

实现在 `code/rewrite/rmg/dd_log.h`：双双精度（106 位）算 `ln`，再手工截断。
**x86 也走这条路**，`ChopLogX87` 只在测试里保留作取证。
实测该实现与任意精度模型 **20000 / 20000 逐位一致**（探针 `build/scratch/dd_probe.cpp`）。

代价是：与真 `fyl2x` 有 0.035% 的 1 ulp 分歧。下一节论证它不会传下去。

### 4.4 为什么这 1 ulp 不影响地图

`ChopLog` 的**唯一消费者**是 `FastSqrt((-l - l) / s)`。而 `FastSqrt`：

1. 先把 double 收窄成 float（相对量化 2⁻²⁴）；
2. 再只取尾数高 14 位查表（`m >> 10`，相对量化 **2⁻¹⁴**）。

`l` 上 2⁻⁵² 的扰动要想改变查表下标，真值必须落在离 2⁻¹⁴ 量化边界不到 2⁻⁵² 的地方，
概率约 **2⁻³⁸**。20000 例实测：ln 层分歧 7 例，**穿透到 `FastSqrt` 层 0 例**。

更强的旁证：`NormalDeviate` 的基准值是用 Unicorn（≈`near53`）取的，
而复刻代码用的是 `chop53`，真 CPU 用的是 `fyl2x`——**三种互不相同的 ln，
经过 `FastSqrt` 之后产出同一批位形**，5 种子 × 16 个正态偏差全部逐位相同。

回归测试把这条性质本身钉死了（`ChopLog.X87DeviatesOnlyWhereItIsNotCorrectlyRounded`）：
不要求逐位相同，但要求「只会低 1 ulp」且「穿透 `FastSqrt` 的次数为 0」，
任何一条被打破立刻失败。

### 4.5 留给 CI 的架构无关基准

`ChopLog` 的黄金值由 Python 的任意精度算术算出，**全程不经过硬件浮点**，
所以 x86 / ARM / 龙芯上都必须逐位相同。101 组存档在
`memory/data/rmg/choplog_golden.txt`，内联在 `ChopLog.MatchesArchIndependentGolden`。

### 4.6 方法论教训

1. **样本量 80 得出的"逐位一致"不叫实锤。** 分歧率 0.035% 时，80 个样本有 97% 的
   概率什么都发现不了。凡是声称"逐位一致"的结论，样本量必须与目标分歧率匹配。
2. **oracle 也要被验证。** 检验模拟器是否可信的最省事办法：**改变一个本该改变结果的
   输入**（这里是 FPU 控制字），若结果纹丝不动，说明它压根没在模拟那个语义。
3. **不是所有指令都值得逐位复刻。** 当某条指令本身就不是正确舍入的，
   "复刻它"是个伪目标；正确的做法是定义一个确定的数学语义，
   再**证明**残余差异在下游被吸收。

## 五、其余算术为什么可以用 C++ 原生 double

原版控制字 `0x0E7F` = `PC=53`（x87 中间结果就按双精度舍入）+ `RC=chop`。
x64 的 SSE2 与 x86 的 x87-PC53 在 `+ - × ÷` 和 double→float 转换上，
只要 MXCSR/FPCW 的 RC 一致，结果就完全相同——
`FpuChopScope`（`_controlfp(_RC_CHOP, _MCW_RC)`）同时设置两者。
所以只有**超越函数**需要特殊处理。

这条"只要 RC 一致就相同"的保证，前提是编译器不擅自改写浮点语义。
`code/rewrite/rmg/fp_guard.h` 把这些前提写成了编译期断言与运行期自检：

| 坑 | 后果 | 防线 |
|----|------|------|
| `FLT_EVAL_METHOD == 2`（x87 超精度） | 中间结果按 80 位算，与原版不符 | `static_assert` + `-mfpmath=sse` |
| FMA 收缩（ARM / 龙芯默认开启） | `a*b+c` 只舍入一次，与两步舍入差 1 ulp | `-ffp-contract=off` / `/fp:strict` |
| `-ffast-math` | `fesetround` 直接失效，且允许重结合 | `-fno-fast-math` + 运行期自检 |
| 有符号右移实现定义 | `e >> 1` 在负指数上分岔 | `static_assert((-2 >> 1) == -1)` |

其中"`fesetround` 是否真的生效"用**运行期**自检兜底
（`FpEnvironment.RoundingModeActuallyTakesEffect`）：`0.1` 在就近与截断下的位形
必须不同。编译期断言挡不住的构建配置，会在第一条测试上炸掉。

## 六、取证脚本与基准

| 脚本 | 作用 |
|------|------|
| `code/randmap/dump_fastmath_golden.py` | Unicorn 执行原版 `0x4CAC40 / 0x598000 / 0x5980C0` 取基准（**注意：该模拟器的 `fyl2x` 不可信，见 §4.1**） |
| `build/scratch/fyl2x_probe.cpp` | 真 CPU 只读探针：FPCW=0x0E7F 下取 20000 组 `fyl2x` 真值 |
| `build/scratch/dd_probe.cpp` | 把 `dd::ChopLogPortable` 的输出导出，交给任意精度判定 |
| `code/ghidra_scripts/decompile_blob_growth.py` | 反编译涂块核心的全部下挂零件 |
| `code/ghidra_scripts/find_writers.py` | 定位 .bss 全局的运行期初始化点 |

| 基准存档 | 内容 |
|----------|------|
| `memory/data/rmg/fastmath_golden.txt` | sqrt / 均匀 / 正态（Unicorn 取，已由真 CPU 侧面印证） |
| `memory/data/rmg/fyl2x_native.txt` | 真 CPU `fyl2x` 20000 组 |
| `memory/data/rmg/fyl2x_mismatch.txt` | `fyl2x` 与精确截断分歧的全部 7 例 |
| `memory/data/rmg/choplog_golden.txt` | `ChopLog` 架构无关黄金值 101 组（纯任意精度算出） |
