# 原版数学基元：查表 sqrt / sin / cos / atan 与正态分布

> 状态：**已实锤并逐位对拍**。C++ 复刻在 `code/rewrite/rmg/fast_math.{h,cpp}`，
> 回归 `code/rewrite/rmg/test/test_fast_math.cpp`
>（74 组 sqrt + 16384 项表自检 + 82 组 sin/cos + 10240 项表自检
> + 105 组 atan + 4097 项表自检 + 101 组 ChopLog 黄金值 + 5 种子 × 16 正态）。
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

## 四之二、`Sin` @ `0x4CACB0` / `Cos` @ `0x4CAD00`——10240 项查表

原版全程没调过 `sin`/`cos`。这两个函数各只有二十来条指令，主体是查
`float[0x2800] @ 0x84F084`：

```c
double Sin(double x) {
    int32 idx  = (int32)_ftol(x * 2607.594482421875f);   // 常数 = chop_f(8192/π)
    bool  odd  = idx & 1;
    int32 half = trunc(idx / 2) & 0x1FFF;                // cdq/sub/sar：向零截断的 /2
    if (odd && half < 0x1FFF) ++half;                    // 奇数位补半步 ≈ 四舍五入
    return (float)table[half];
}
// Cos 完全一样，只是下标再 +0x800（四分之一周期），上限改成 0x27FF
```

`and eax, 0x80001FFF` 之后那段 `dec / or 0xFFFFE000 / inc` 加两次 `jns` 的
13 位符号扩展，逐条走一遍会发现**恒等于 `& 0x1FFF`**（`m == 0` 走 0 分支，
`m > 0` 先减 `0x2000` 再加回 `0x2000`）。复刻里照抄了这十几条——
删掉它才需要举证，留着不需要。

### 表的来历

逐项比对：**`table[i] == chop_to_float(sin(i · 2π / 8192))`，10240 项里 10238 项
逐位相符**。唯二例外是 `i = 4096`（`1.2246063538223773e-16`）与
`i = 8192`（`−2.4492127076447545e-16`），数学上都该是 0。

这两个数是 x87 `fsin` 的**指纹**：`fsin` 内部只用 66 位的 π 做参数归约，
`fsin(M_PI)` 返回 `1.2246063538223773e-16`，而正确舍入的
`sin(nearest_double(π))` 是 `1.2246467991473532e-16`。
所以这张表当年是**用 x87 `fsin` 离线生成的**，两处 ~1e-16 的噪声原样留在了里面。

排除过的其他参数构造式（`i*(2π/8192)`、`(i*2π)/8192`、`2π*(i/8192)`、
`(i/8192)*2π`、15 位 π 字面量）都给出同样这 2 项偏差，
说明差异出在**正弦求值**而不是参数构造——这是把锅扣到 `fsin` 头上的直接证据。

表有 `0x2800 = 10240` 项而一个周期只需 8192 项：余弦靠 `+0x800` 的相位偏移
查同一张表，把下标顶到 `0x27FF`，多出来的 2048 项就是给它留的。

### 三条必须照搬的性质

1. **角度被量化到 1/8192 圈**（≈ 0.0439°），返回值只有 float 精度；
2. **`f(−x) ≠ −f(x)`**：`_ftol` 与折半都是向零截断，正负两侧落在**相邻的两个表项**上。
   这不是精度噪声而是结构性的——河流生成器每步 `x += Cos(θ)`、`y −= Sin(θ)`，
   这份不对称会实打实改变河道走向。专门写了 `Trig.NegativeArgumentIsNotMirrored` 钉住它；
3. `sin π` / `sin 2π` 那两项不是 0。

### 表的自检为什么不能像 sqrt 那样做到逐位

sqrt 表能用纯整数 `isqrt` 重算，所以自检是**精确等于**。
sin 表绕不开 `std::sin`，而 `std::sin` 的最后 1 bit 是**逐平台**的
（x86 / ARM / 龙芯的 libm 各有实现），拿它做逐位比对反而会让测试在某些平台上假失败。
于是 `Trig.TableIsPureMath` 分成两层：

* **数值层**：允许 1 个 float ulp 的相对误差（`std::sin` 的 double 误差 < 2⁻⁵²，
  远小于 float 的 2⁻²³，任何合规 libm 都过得去）；
* **结构层**：`table[i+4096] == table[i] ^ 0x80000000`（反对称）、
  `table[2048+i] == table[2048−i]`（关于 π/2 对称）、
  `table[i+8192] == table[i]`（周期 8192）——这三条**逐位精确**，
  因为向零截断对取负是对称的。两处 `fsin` 残渣单独钉死位形。

这一层结构校验的说服力其实不比 sqrt 那条弱：它把 10240 个数压成了
"一个四分之一周期 + 三条对称性"，能通过的表只可能是正弦表。

> 顺带：`0x4CAD50` 起还有一个用 `[0x7E8970] = 651.8986469044033`（≈ 2048/π）的
> 同族函数，查同一张表但步长是 1/2048 圈。RMG 不走它。

## 四之三、`Atan` @ `0x4CADE0`——4097 项查表，粗到肉眼可见

同一套「西木快速数学库」的第四件。全 exe **511 处**调用点，
RMG 这边的消费者是角度扩张 `0x5A08D0`（配一句 `if (dx < 0) a += π`
手写成了 atan2）。

```
0x4CADE0  atan:
  fld  qword [esp+4]           ; x（double）
  fst  dword [esp+4]           ; 窄化成 float 存回去，**不出栈**——只为后面取符号
  fdiv dword [0x8650B8]        ; / 0.024413354694843292f
  call 0x7C5F00                ; _ftol，向零截断
  cdq / xor eax,edx / sub eax,edx      ; 整数 abs
  cmp  eax, 0x1001
  jge  越界                     ; → fld dword [0x7E897C] = 1.5707963705062866f
  fld  dword [eax*4 + 0x8610B4]        ; float atanTable[0x1001]
  越界汇合:
  fld  dword [esp+4]           ; 取回窄化的 float x
  fcomp dword [0x7E1748]       ; 与 0.0f 比
  fnstsw ax / test ah,1 / jz   ; C0（即 x < 0）
  fchs                         ; 是负数才取反
  ret
```

表尾正好落在 `0x8650B4`，紧跟着就是步长常量 `0x8650B8`——
两者相邻这一点顺带确认了表长就是 `0x1001 = 4097` 项。

### 表的来历：这次连生成式都还原了

```c
float x = 0.0f;
for (i = 0; i <= 0x1000; ++i) {
    table[i] = (float)atan(x);   // 向零截断存 float
    x = x + h;                   // float 累加，也向零截断
}
```

**4097/4097 逐位相符。** 两处「向零截断」缺一不可：换成就近舍入只对得上 36 项，
「累加改成 `i * h`」也对不上（正因为逐步截断，表尾的自变量比 `4096·h` 低了约
0.0155，于是 `table[4096]` 比 `atan(4096·h)` 低 13 个 float ulp）。

这条结果本身就是一份取证：**这张表当年是被同一套
`_control87(_RC_CHOP)` 环境下的代码生成出来的**——
与 sin 表那两处 `fsin` 残渣一样，是「离线生成器就是游戏自己」的指纹。

步长 `h = 0.024413354694843292`（`0x3CC7FE84`）本身的来历**未确认**：
它不是 `100/4096 = 0.0244140625`（那个值恰好是二进制精确的），
也不是 `1/cot(0.01)/4096`。不影响复刻——照原样用就是了。

### 三条必须照搬的性质

1. **自变量的量化是线性的、而且很粗**：一档 `0.0244133547`。
   注意这不是角度均分而是**正切值**均分，所以 x 小时精度奇高、x 大时一档能跨好几度。
   最刺眼的例子：`atan(1)` 的真值是 `π/4 = 0.7853982`，
   原版给 **`0.7735263`**——差 **0.68°**。因为 `1/h = 40.96118` 被 `_ftol`
   截成 `40`，查的是 `atan(40h) = atan(0.9765)`。
   `Atan.DeliberatelyCoarseAndDiscontinuous` 专门钉住这一条，防止有人"修好"它；
2. **|x| 跨过约 100.0215 时结果会跳 0.01 弧度**：下标 `≥ 0x1001` 那条出口给的是
   float `π/2 = 1.5707964`，而 `table[0x1000]` 只有 `1.5607948`。函数不连续；
3. **它是严格奇对称的**——`abs` 排在 `_ftol` **之后**，
   而 `_ftol` 向零截断满足 `trunc(−y) = −trunc(y)`。
   这与 Sin/Cos **正相反**（那两个因为没有 `abs` 而不对称），
   是同一个作者在同一个文件里的两种写法，复刻时不能想当然地统一。
   唯一例外：`|x| < 2⁻¹⁴⁹` 时窄化成 `±0.0`，符号被吃掉，恒返回 `+0`。

`_ftol` 溢出（`|x/h| ≥ 2⁶³`）时 x87 存的是 64 位「整数不定值」
`0x8000000000000000`，**低 32 位恰好是 0**，而调用点只用 `eax`——
所以极端自变量会静默落到 `table[0] = 0.0`，不是越界读。
C++ 侧用 `FtolLow32` 显式建模这一条（顺带把 Sin/Cos 里同样的 UB 一起消掉了）。

### 自检可以做到逐位

与 sin 表不同，这里**不需要**两层校验：生成式里的两处向零截断把
`std::atan` 最后 1 bit 的平台差异挡在了外面。`Atan.TableIsPureMath`
直接逐位重算全表；只在极小概率（某平台 `std::atan` 差 1 ulp 且恰好跨过截断边界）
下降级为「差 1 ulp 以内」，并把允许的分歧项数上界钉在 0.5%。
本机 x86/MSVC 实测 **0 项分歧**。

### 模拟器可信性

`0x4CADE0` 的 oracle 自检做了两层（见 `dump_atan_golden.py` 的开头注释）：

* 直接把控制字从 `0x0E7F` 换成 `0x027F`，**399 个样本无一变化**——
  乍看像模拟器失灵，实际是这个函数本身几乎与舍入模式无关：
  取下标的 `_ftol` 自己会 `fnstcw` 比对 `[0x822D80]`，不一致就走强制截断的慢路径；
* 于是改用指令级探针单独验它用到的两条指令：`fdiv` 在 1521 组 `a/b` 里有
  **546 组**随控制字改变结果，`fst dword`（`1/3` 窄化）给出
  `0x3EAAAAAA`(chop) / `0x3EAAAAAB`(near)。两条都吃舍入模式，**模拟器在此可信**。

这也是对 §4.6 那条方法论的一次补充：**"改输入没反应"不等于"oracle 坏了"，
也可能是"这段代码本来就不敏感"。要分清楚，就得把敏感的那条指令单独拎出来测。**

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
| `code/randmap/dump_sincos_golden.py` | Unicorn 执行原版 `0x4CACB0 / 0x4CAD00` 取基准（只有 `fmul` + 整数运算，此处模拟器可信） |
| `code/randmap/gen_sin_table.py` | 从 exe 导出 `0x84F084` 的 10240 项正弦表成 `sin_table.h`，导出时当场与 `sin(i·2π/8192)` 比对 |
| `code/randmap/dump_atan_golden.py` | Unicorn 执行原版 `0x4CADE0` 取基准（含两层 oracle 自检，见 §四之三） |
| `code/randmap/gen_atan_table.py` | 从 exe 导出 `0x8610B4` 的 4097 项反正切表成 `atan_table.h`，导出时当场用还原出的生成式重算全表 |
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
| `memory/data/rmg/sincos_golden.inc` | 查表 sin/cos 82 组（直接 `#include` 进测试） |
| `memory/data/rmg/atan_golden.inc` | 查表 atan 105 组（直接 `#include` 进测试） |
