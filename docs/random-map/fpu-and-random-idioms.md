# 原版浮点环境与随机数取用惯用法

> 复刻随机地图生成器时，**先把浮点环境对齐，再谈算法**。原版整个进程的 x87
> 舍入模式不是默认值，而且生成器取随机数从不直接用 `RandomRanged`，而是
> 一套"浮点比例 + 拒绝采样"的宏。这两件事任何一件不对，地图就会从某一格开始分岔。

## 一、全局 FPU 状态：双精度 + 向零截断

三条独立证据锁死同一结论。

### 证据 1：WinMain 里显式设置舍入模式

```
0x6BBFB7  push 0x300
0x6BBFBC  push 0x300
0x6BBFC1  call 0x7CBF49        ; _control87(new=0x300, mask=0x300)
0x6BBFC9  call 0x7C5EE4        ; 把当前控制字缓存到 [0x822D80]
```

`0x7CBF49` 确认是 `_control87` 包装（内部把 `0x00080000`(_DN) 位剔掉后转调
`0x7CBF14`，末尾 `fldcw`）。MSVC 位定义里 `_MCW_RC = 0x0300`、
`_RC_CHOP = 0x0300`，因此这一句等价于：

```c
_control87(_RC_CHOP, _MCW_RC);   // 舍入模式 -> 向零截断
```

### 证据 2：缓存下来的控制字实测值

`0x7C5EE4` 的全部内容就是"读当前控制字 → 存进 `[0x822D80]`"：

```
0x7C5EEC  fnstcw word ptr [ebp-2]
0x7C5EF9  mov dword ptr [0x822D80], eax
```

PE 里 `[0x822D80]` 的初值 = **0x0E7F**，逐位拆开：

| 位段 | 值 | 含义 |
|------|----|------|
| 0-5 异常屏蔽 | 0x3F | 全部屏蔽 |
| 8-9 PC | 0b10 | **53 位双精度**（不是 80 位扩展） |
| 10-11 RC | 0b11 | **向零截断（chop）** |

### 证据 3：`_ftol` 的快路径判据

`0x7C5F00` 是 `_ftol`（float→int64）：

```
0x7C5F03  fnstcw word ptr [esp]
0x7C5F13  cmp edx, dword ptr [0x822D80]
0x7C5F19  jne 0x7C5F26          ; 不等 -> 先改控制字再转换
0x7C5F1B  fistp qword ptr [eax] ; 相等 -> 直接转换
```

快路径直接 `fistp` 而不改舍入模式，只有当运行期控制字**本身**就是
`chop` 时才是正确的。这与证据 1、2 互相印证。

### 对复刻的要求

```cpp
ra2::rmg::FpuChopScope chop;   // _controlfp(_RC_CHOP, _MCW_RC)，析构时还原
```

- PC=53 位这一点让我们省事：C++ `double` 本身就是 53 位，x64 下走 SSE2 也是
  53 位，不存在 MSVC6 x87 那种"中间结果 80 位"的坑。
- RC=chop 必须显式设置。`(int)d` 这一步 C++ 本来就是截断，**与舍入模式无关**；
  真正受影响的是 `fmul` / `fadd` / `fdiv` 自身的舍入。

**实测边界（诚实记录）**：对下文的取数惯用法，我们用 6 个 n 值 ×
200 种子 × 2000 次抽取（各 40 万次）对比 chop 与 round-to-nearest，
**分歧 0 次**。原因是乘积落在整数边界半个 ulp 内的概率约 `n·2^-53`，可忽略。
所以舍入模式对"取随机数"这一步不构成风险；但它是**全局**设置，
地形高度/坡度/比例那些真正做浮点累加的阶段仍必须在同一模式下跑。
不要因为这里测不出差异就把它当无关项。

## 二、生成器专用的全局随机数发生器

生成流程 `FUN_00598960` 开头：

```
0x59897B  mov eax, dword ptr [ebp+0x74]   ; MapSeedClass::Seed（参数表 +0x74）
0x598981  push eax
0x598981  lea ecx, [esp+0x38]             ; 栈上临时 Randomizer
0x598985  call 0x65C6D0                   ; Randomizer::Randomizer(seed)
0x59898A  mov ecx, 0xFD                   ; 253 dword = 0x3F4 字节 = 整个对象
0x598996  mov edi, 0xABE890
0x59899B  rep movsd                       ; 拷进全局
```

结论：

- **整张地图只播种一次**，种子直接来自 `.sed` 的 `Seed=`（`MapSeedClass+0x74`）
- 生成期间全体阶段共用 **全局 Randomizer @ `0xABE890`**
  （所有取数点前面都是 `mov ecx, 0xABE890`）
- 因此"阶段顺序 + 每阶段取数个数"就是种子兼容性的全部内容

按**函数地址**数出来的分布见 `memory/data/rmg/rng_callsites.txt`
（工具 `code/randmap/scan_rng_callsites.py`，扫 0x594000-0x5B0000 得 142 处：
播种 1、`Random` 127、`RandomRanged` 14）。

> ⚠️ **这份计数不能直接当作"生成器的取数点"**：它只看调了哪个函数，
> 不看 `this` 是哪个对象。按 `this` 重新配对后，`RandomRanged` 那 14 处
> **一处都不属于 `0xABE890`**。正确的清单见下一节。

### 边界实锤：这个 `Randomizer` 只被 MapGen 自己用

全 `.text` 按小端 4 字节搜立即数 `0xABE890`，共 **153 处**引用，
全部落在 **`0x58B772 – 0x5AC192`** 这一段（其中 151 处是 `mov ecx, imm32`，
另两处 `mov edi/esi` 是播种时的 `rep movsd` 拷贝）。**区间之外一处都没有。**

这条边界很重要，因为它把"随机流对齐"的搜索面一次性锁死：

> 生成阶段会调进大量引擎通用代码（`ScenarioClass` 初始化、`HouseClass`、
> 显示层……），那些地方**也**在调 `Randomizer::RandomRanged`，
> 但用的是**引擎自己的那个 Randomizer 对象**，与 `0xABE890` 是两份独立状态。
> 只按"调用了哪个函数"做传递闭包会把整个引擎都算进来（实测阶段 1 一路展开到
> `0x726400` 之类的渲染代码），结论完全不可用。
> **判定必须以「取数点的 `this` 是不是 `0xABE890`」为准。**

反过来说：只要复刻代码把 `0x58B772–0x5AC192` 这段里的取数点全部对齐，
引擎其余部分做什么都不影响地图。

把这 153 处逐一配对到后续的 `call`（工具 `code/randmap/scan_rmg_rng.py`，
存档 `memory/data/rmg/rng_callsites_mapgen.txt`）：

| 类别 | 处数 | 说明 |
|------|------|------|
| `Randomizer::Random` | **150** | 生成器全部的随机来源 |
| `Randomizer::RandomRanged` | **0** | ⚠️ 一处都没有 |
| `Randomizer::Randomizer`（播种） | 1 | `0x58B772`，**CRT 静态初始化**，seed=0，与生成无关 |
| 非取数 | 2 | `0x598996` 播种时的 `rep movsd`；`0x5AC192` 恰好以此地址收尾的无关数组析构 |

再往下筛一层：这 150 处里有 **46 处在死代码里**（22 个函数，入口在整个镜像里
零引用），真正会被执行的是 **104 处**。逐点的阶段归属见
[rng-ledger.md](rng-ledger.md)。

三条要点：

1. **生成器一次 `RandomRanged` 都不用。** 先前"14 处 `RandomRanged`"的计数
   是按函数地址数出来的，那些调用的 `this` 是别的对象。要区间随机时，
   生成器一律走自己的 `PickRange`（`0x598030`，浮点比例 + 拒绝采样）。
2. `0x58B770` 是个只有三条指令的全局对象构造函数（`push 0 / mov ecx / call`），
   在 CRT 初始化表里（指针 `@0x813B68`），进程启动时以 seed=0 跑一次。
   真正的播种仍然是 `0x598985` 那一处，**每张地图只此一次**。
3. 有一处取数点（`0x598003`，即 `UniformUnit@0x598000`）**没有任何直接调用者**——
   它的地址被写进全局对象 `0xABDFB8` 的 `+0x10` 字段，由 `NormalDeviate@0x5980C0`
   以 `call dword ptr [esi+0x10]` 调用。纯静态扫描会漏掉，见 [rng-ledger.md](rng-ledger.md) §四。

## 三、浮点比例 + 拒绝采样惯用法

以 `Seeding water` 阶段 `FUN_0059C630` 的三处为样本（最干净）：

```
0x59C6DA  mov ecx, 0xABE890
0x59C6DF  call Randomizer::Random
0x59C6E4  mov [esp+0x10], eax            ; 低 32 位
0x59C6E8  mov [esp+0x14], ebx            ; 高 32 位 = 0
0x59C6EC  fild qword ptr [esp+0x10]      ; 借 64 位 fild 做 uint32 -> double
0x59C6F0  fmul qword ptr [0x7ED9E8]      ; × 10/(2^32-1)
0x59C6F6  fadd qword ptr [0x7E1718]      ; + 1.0
0x59C6FC  call 0x7C5F00                  ; _ftol（截断）
0x59C701  cmp eax, 0xA
0x59C704  ja  0x59C6DA                   ; > 10 则重抽
```

比例常数实测**与 `n / 4294967295.0` 的 double 字节逐位一致**：

| 常数地址 | double 值 | = n/(2^32-1) |
|----------|-----------|--------------|
| 0x7ED9E8 | 2.3283064370807974e-09 | n = 10 |
| 0x7ED9E0 | 5.6345015777355296e-08 | n = 242 |
| 0x7ED9D8 | 4.6798959385324027e-08 | n = 201 |

于是两个等价形式（重写实现见 `code/rewrite/rmg/scaled_random.{h,cpp}`）：

```cpp
// 0..n-1：只有 Random()==0xFFFFFFFF 才会取到 n 被拒
int PickBelow(Randomizer& rng, int n) {
  const double scale = n / 4294967295.0;
  int v;
  do { v = (int)((double)rng.Random() * scale); } while (v > n - 1);
  return v;
}

// 1..n：多一个 `fadd 1.0`
int PickFrom1(Randomizer& rng, int n);
```

注意 `fild qword` 是**故意**的：x87 没有 uint32→浮点指令，
MSVC 就把无符号数补成 64 位有符号再 `fild`。若误写成 `(int32)` 转换，
一半的取值会变成负数。

### 对拍结果

`code/randmap/dump_scaled_golden.py` 用 Unicorn 把上述三段机器码
在 `FPCW=0x0E7F` 下逐次执行（全局 Randomizer 写在 `0xABE890`），
导出 5 个种子 × 24 次的基准 → `memory/data/rmg/scaled_golden.txt`。

`test_scaled_random`（Google Test 3 项）全部通过：**逐值一致** ✅

## 四、顺带实锤的几个全局

| 地址 | 判定 | 证据 |
|------|------|------|
| `0x87F7E8` | `MapClass::Instance` | YRpp 交叉验证 |
| `0x89C2DC` | 工作数组**边长**（方阵） | `0x578E7B: imul esi,esi` 后按 side² 分配 |
| `0xABED10` | 每格 **0x50 字节**工作记录数组指针 | `0x578E60` 分配 `side²×0x50`，逐个 `call 0x58BDC0` 构造 |
| `0xABE890` | 生成器全局 `Randomizer` | 见第二节 |
| `0x7C5F00` | `_ftol` | `fnstcw`/`fistp qword` |
| `0x7CBF49` | `_control87` | 转调 `0x7CBF14`，末尾 `fldcw` |

索引换算（`0x59C75E`、`0x5A3B14` 等处一致）：

```
offset = (MapCoords.Y * [0x89C2DC] + MapCoords.X) * 0x50
```

`CellClass::MapCoords` 在 `+0x24`（`AbstractClass` 基类 0x24 字节，与 YRpp 一致），
所以 `movsx eax, word[edx+2]` 取的是 Y、`word[edx]` 取的是 X。

**未确认**：这个 0x50 字节记录的字段语义（已知 `+0x38`/`+0x3C` 在
`Seeding water` 开头被填 `0xFFFFFFFF`，`+0x4A` 在分配后置 1）。
它被 0x578/0x585/0x58B-0x58E/0x5A 四个区共 284 处引用，
疑似区域(region)/寻路分区系统的每格记录，待后续阶段确认。

## 五、待办

1. 把 `Seeding water` 三个入口（`0x59A6C0` / `0x59C580` / `0x59C630`）
   完整还原成算法，并记录该阶段消耗的随机数个数
2. 沿流水线继续：Init/Making regions → hills → LATs/rocks → tiberium
3. 每阶段都用 Unicorn 片段模拟取基准，逐值对拍
