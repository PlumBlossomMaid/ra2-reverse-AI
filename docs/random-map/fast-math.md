# 原版数学基元：查表 sqrt 与正态分布

> 状态：**已实锤并逐位对拍**。C++ 复刻在 `code/rewrite/rmg/fast_math.{h,cpp}`，
> 回归 `code/rewrite/rmg/test/test_fast_math.cpp`（74 组 sqrt + 5 种子 × 16 正态）。

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
复刻代码因此在首次使用时用 `IntSqrt` 现算出这张表，仓库里不留任何提取的二进制。

> 顺带：这也说明表本身就是数学常量，不是"作品"。

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
3. **`ln` 用 x87 `fyl2x`**（`0x598126: fldln2 / fxch / fyl2x`），不是 `std::log`。

## 四、`fyl2x` 的语义与复刻方案

用高精度有理数把整个发生器重演了一遍（`Fraction` 精确算术 + 显式向零截断，
`Decimal.ln()` 取 60 位精度的对数），与模拟器取的基准 **80/80 逐位一致**。
结论：

> 在 `PC=53 / RC=chop` 下，`fyl2x` 求出的 `ln(s)` ≡ **精确 ln 向零截断到 double**。

`std::log` 是就近舍入，约半数取值上差 1 ulp——**不能直接替换**。因此复刻模块
把整个工程切到 **32 位 x86**，`ChopLog` 直接内联同一条 x87 指令：

```cpp
__asm { fldln2 ; fld qword ptr [s] ; fyl2x ; fstp qword ptr [result] }
```

这不只是为了 `ln`：下游阶段还会遇到 `fsin` / `fpatan` / `f2xm1` 等 x87 超越指令，
一次性钉在 x86 上，后面每一条都能这样"用同一颗指令"精确复刻，
而不必逐条论证某个库函数是否等价。

非 x86 目标保留 `std::log` 兜底，但**该路径未验证逐位一致**，代码里已注明。

## 五、其余算术为什么可以用 C++ 原生 double

原版控制字 `0x0E7F` = `PC=53`（x87 中间结果就按双精度舍入）+ `RC=chop`。
x64 的 SSE2 与 x86 的 x87-PC53 在 `+ - × ÷` 和 double→float 转换上，
只要 MXCSR/FPCW 的 RC 一致，结果就完全相同——
`FpuChopScope`（`_controlfp(_RC_CHOP, _MCW_RC)`）同时设置两者。
所以只有**超越函数**需要特殊处理。

## 六、取证脚本

| 脚本 | 作用 |
|------|------|
| `code/randmap/dump_fastmath_golden.py` | Unicorn 直接执行原版 `0x4CAC40 / 0x598000 / 0x5980C0` 取基准 |
| `code/ghidra_scripts/decompile_blob_growth.py` | 反编译涂块核心的全部下挂零件 |
| `code/ghidra_scripts/find_writers.py` | 定位 .bss 全局的运行期初始化点 |

基准存档：`memory/data/rmg/fastmath_golden.txt`
