# 随机数发生器 R250 —— 字节级复刻的地基

> 原版地址（`gamemd.exe`，ImageBase `0x400000`）
> | 函数 | 地址 | 调用约定 |
> |---|---|---|
> | `Randomizer::Randomizer(uint seed)` | `0x65C6D0` | `__thiscall`, `RET 4` |
> | `Randomizer::Random()` | `0x65C780` | `__thiscall`, `RET 0` |
> | `Randomizer::RandomRanged(int,int)` | `0x65C7E0` | `__thiscall`, `RET 8` |
>
> 取证：`memory/data/decomp/randomizer_asm.txt`（逐条汇编 + 机器码）
> 基准：`memory/data/rmg/rng_golden.txt`（原版机器码实跑输出）
> 重写：`code/rewrite/rmg/randomizer.h/.cpp`　测试：`code/rewrite/rmg/test/test_randomizer.cpp`

为什么它是地基：生成流程里每一次地形抉择都从这条随机序列上取数。
**只要多取或少取一次，后续整张地图全盘偏移**。因此必须先把它逐位钉死。

## 对象布局（0x3F4 字节）

| 偏移 | 类型 | 含义 |
|---|---|---|
| `+0x00` | `char` | `Disabled`，非 0 时 `Random()` 恒返回 0 |
| `+0x04` | `int` | `Index1`，初值 `0` |
| `+0x08` | `int` | `Index2`，初值 `0x67`(103) |
| `+0x0C` | `uint[250]` | 状态字数组 |

双索引初值 `0` 与 `103`、数组长 `250`，正是 **R250**（滞后 103/250 的移位寄存器）的特征。

## 播种（0x65C6D0）

250 个状态字逐个生成，每个由 `(种子, 下标)` 经 **4 轮 Feistel 混淆**得到：

```
L = seed;  R = i
每轮 r = 0..3:
    x  = KEY1[r] ^ R
    hi = (int32)x >> 16            // SAR，算术右移
    lo = x & 0xFFFF
    t  = ~(hi*hi) + lo*lo
    m  = (uint32)(t >> 16) | ((uint32)t << 16)
    R' = ((m ^ KEY2[r]) + hi*lo) ^ L
    L = R;  R = R'
state[i] = R
```

轮常量（PE `.data` 实读，各表原有 20 个 dword，本算法只用前 4 个——
内层循环终止条件 `CMP EBX,0x10` 实锤 4 轮）：

| 轮 | `KEY1` @ `0x839644` | `KEY2` @ `0x839694` |
|---|---|---|
| 0 | `0xBAA96887` | `0x4B0F3B58` |
| 1 | `0x1E17D32C` | `0xE874F0C3` |
| 2 | `0x03BCDC3C` | `0x6955C5A6` |
| 3 | `0x0F33D1B2` | `0x55A7CA46` |

### 易错点：那不是 ROR16

```asm
0065c734  c1 f9 10   SAR ECX,0x10      ; 算术右移，符号位铺满高 16 位
0065c737  c1 e2 10   SHL EDX,0x10
0065c73a  0b ca      OR  ECX,EDX
```

看起来像 16 位循环移位，实际**不是**：`SAR` 会把符号位铺满高 16 位，
再与 `t << 16` 做 `OR`——`t` 为负时高半字被污染成 `0xFFFF`。
反编译输出的 `t >> 16 | t * 0x10000` 是对的，但极易被顺手写成 `_rotr(t,16)`，
那样从第一个状态字起就全错。**这类失真只能靠汇编逐条核对抓出来。**

同理 `hi` 必须是 `SAR`（有符号），写成逻辑右移同样全盘皆错。

## 取数（0x65C780）

```
if (Disabled) return 0;
State[Index1] ^= State[Index2];
result = State[Index1];          // 注意：返回异或之后的新值
Index1++;  Index2++;
if (Index1 >= 250) Index1 = 0;
if (Index2 >= 250) Index2 = 0;
return result;
```

## 区间随机（0x65C7E0）

闭区间 `[min(a,b), max(a,b)]`，用**掩码 + 拒绝采样**消除取模偏差：

```
if (a == b) return a;
lo = min(a,b);  hi = max(a,b);  range = hi - lo
bits = (range 的最高置位序号) + 1        // 第 31 位单独判，见下
mask = ~(0xFFFFFFFF << bits)
do { v = Random() & mask; } while ((int32)v > range);   // 有符号比较
return v + lo
```

两个原版怪癖，如实保留：

1. **求位数只扫到第 30 位**：`TEST EDI,0x80000000` 命中就直接取 `bits = 32`，
   循环从 31 递减且**先减后测**，故第 31 位从不在循环里被测试
2. **`bits == 32` 时掩码退化为 0**：x86 移位只取 `CL` 低 5 位，`SHL by 32` 实际不移动，
   `~0xFFFFFFFF = 0`。重写里写成 `<< (bits & 31)` 保持同样行为
3. **`range+1` 溢出短路**：`LEA EAX,[EDI+1]; CMP EAX,EDI; JLE` —— 溢出时不采样，
   直接返回 `(range+1) + lo`

### 血缘：这套写法直接继承自 RA1

EA 开源的红警 1 源码 `REDALERT/RANDOM.CPP:125`
（https://github.com/electronicarts/CnC_Remastered_Collection ）里，
`RandomClass::operator()(int minval, int maxval)` 的注释原文就是：

> *"Keep picking random numbers until it fits within the magnitude desired."*

它的三步——**求覆盖 `range` 的最小位掩码 → 反复取数直到落进区间 → 加回下界**——
与上面 `0x65C7E0` 的骨架逐句对应。RA2 换掉的只是底层发生器（LCG → R250）
和位宽（15 → 32），三个"怪癖"全部是位宽变大之后才可能出现的边界情况。
详见 [cncremastered-source.md](../references/cncremastered-source.md) §随机数系统前身。

**但地图生成器一次都没调它**：150 处取数点全是 `Random()`，
`MapGen.cpp` 自带的 `PickRange`（`0x598030`）用的是浮点比例而非掩码——
见 [fpu-and-random-idioms.md](fpu-and-random-idioms.md)。
拒绝采样这个**习惯**是同源的，**手法**是另起的炉灶。

## 验证结果

用 `code/randmap/emu_oracle.py` 在 Unicorn 里跑原版机器码取真值，
与独立重写逐项比对，**8 个种子 × (播种状态 + 16 次取数 + 3 组区间随机) 全部逐位一致**：

```
seed=0      state[0..7] = AD2EAA18 667051BB BEB8385D 1293A6D6 9A1C341D 4868B99C 803D17D0 569B65AF
            random[0..15] = 49466CF3 6A2B5556 9089E025 9CEFDB3B ...
            ranged(0,99)  = 86 37 59 14 20 17 6 16 74 19 8 4 79 90 68 82
seed=60556  state[0..7] = 11733A82 FC380E0C 521FDB19 A2DD1150 070396C8 4E79B005 E2EB4E22 B2E749B1
```

Google Test 7 项用例全部通过（`ctest -R randomizer`）。
