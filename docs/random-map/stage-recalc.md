# 阶段 5：绿地扩张 + Recalculating cell attributes

> 状态：**`0x59B740` 全流程与唯一取数点已实锤，已落 C++ 并整段对拍通过**；
> `CellClass::RecalcAttributes` 的可见效果也已逐条钉死（§四，纠正了旧版
> 「它与地形无关」的说法）。砖块语义靠「TileSet 全局 ↔ theater ini 键名」
> 映射反查得到（§三），并与 `IsWaterOrShore` 的成员表互相印证。
> 取证：`memory/data/decomp/rmg_starting_points.txt`、
> `memory/data/rmg/tileset_globals.txt`、反汇编 `0x59B740`–`0x59B938`
> 与 `0x47D2B0`–`0x47DD61`。
> 实现 `code/rewrite/rmg/recalc.{h,cpp}`，基准
> `memory/data/rmg/green_spread_golden.txt`（18 个用例），见 §五。

## 一、它在流水线里的真实位置

主流程 `FUN_00598960` 的写法是「先打日志、再干活」，唯独这里有个例外：

```
00598E18  mov  ecx, esi                 ; 白设的——0x59B740 根本不读 ecx
00598E1A  call 0x59B740                 ; ← 没有自己的阶段名
00598E1F  push 0x82BE9C
00598E24  call 0x4068E0                 ; "RMG: Recalculating cell attributes"
00598E31  call 0x578350                 ; MapClass::CellIterator_Reset
00598E3B  call 0x578290                 ; ...Next
00598E44  push -1
00598E48  call 0x47D2B0                 ; CellClass::RecalcAttributes
```

也就是说 **`0x59B740` 夹在两条日志之间、被挂在了下一条日志名下**。
两件事毫无关系：

- `0x59B740` 是真正的地形算法，本阶段**唯一的取数点** `0x59B842` 在它里面；
- `RecalcAttributes` 那一遍**一个随机数都不取**，但它**不是空转**——
  它会把非法砖号/空洞子块的格子整格作废成 `0xFFFF`（§四）。

`xref_rel32.py 0x59B740` 全镜像只有 `0x598E1A` 这一处调用者。

## 二、`0x59B740`：绿地随机蔓延

### 数据结构

局部一个 `DynamicVectorClass<CellClass*>`（构造 `0x5AD820(0,0)`，随后
`0x59B759`–`0x59B765` 把虚表改成 `0x7ED9BC`、个数清 0、扩容步长写成 **10000**），
布局这次也顺带钉死了：

| 偏移 | 内容 |
|------|------|
| `+0x00` | vtable |
| `+0x04` | 数据指针 |
| `+0x08` | 容量 |
| `+0x0C` | `IsInitialized`（推断） |
| `+0x0D` | `IsAllocated`——为 0 且容量非 0 时**拒绝扩容**（借来的缓冲区） |
| `+0x10` | 元素个数 |
| `+0x14` | 扩容步长 |

`AddItem` 被完整内联两遍（`0x59B7A4`–`0x59B7E6`、`0x59B8B8`–`0x59B8F8`）：
`count < capacity` 直接写；否则要求 `IsAllocated || capacity == 0`
且 `growth > 0`，再调虚函数 `+0x08` 的 `Resize(capacity + growth, 0)`。

> **这套容量机器整个不可观测，可以不建模。** `Resize`（`0x5AD010`）
> 搬 `min(newcap, oldcap) == oldcap` 个元素（一个不丢）、把 `IsAllocated` 置 1、
> 再把 `count` 夹到容量以内（`newcap > count` 恒成立，夹不掉）。
> 首次进来 `capacity == 0` 过关，之后 `IsAllocated == 1` 永远过关。
> 于是整条 `AddItem` 等价于一次 `push_back`——C++ 侧直接用 `std::vector`。

### 流程

```c
Vector<Cell*> front;                       // 前沿：贴着绿地的空地

// 第一遍：扫全图，把「绿地格」的 4 个正方向邻居里的空地收进前沿
for (Cell* c : MapCells)
    if (IsGreen(c))                        // 0x4867B0
        for (int d = 0; d < 8; d += 2)     // 方向 0/2/4/6
            if (IsPlain(c->GetNeighbourCell(d)))   // 0x486380
                front.push_back(neighbour);

int rounds = min(front.Count / 3, 1000);   // 整数除法，向零截断

for (; rounds > 0; --rounds) {
    // 唯一取数点 0x59B842：PickBelow(front.Count)
    int n = front.Count;
    int k; do { k = (int)((double)(uint32)Random() * (double)n * (1.0/4294967295.0)); }
           while (k > n - 1);

    Cell* c = front[k];
    front.erase(k);                        // 保序前移，Count--
    c->TileIndex = GreenTile;              // +0x38 = [0xAA0E18]

    for (int d = 0; d < 8; d += 2)         // 新绿地的邻居继续入队
        if (IsPlain(c->GetNeighbourCell(d)))
            front.push_back(neighbour);
}
```

四个必须照抄的细节：

1. **`rounds` 在第一遍扫完之后一次算定**（`0x59B7FF` 的 `imul 0x55555556`
   加 `edx + (edx >>> 31)` 是编译器把「有符号 `/3`、向零截断」变成的乘法，
   `0x59B80F` 的 `cmp edx,0x3E8 / jle` 是 1000 上限，
   `0x59B81C` 的 `jle 0x59B912` 是「≤ 0 就整段跳过」），
   后面前沿再怎么涨都不改
2. **抽样的 `span` 每轮重算**——`front.Count` 每轮减 1 又加 0..4，
   `0x59B828`–`0x59B83E` 就在循环体内
3. **先取元素、再删**（`0x59B875 mov ebp,[ecx+eax*4]` 在搬移循环之前），
   且**删除是保序前移**（`0x59B885`–`0x59B897`），不是「拿末尾顶上」。
   这决定了后续下标含义，抄错就全岔
4. 入队去重？**没有**。同一个空地格可以被多个绿地邻居重复收进前沿，
   也可以在被涂绿之后再次作为别人的邻居入队——照抄即可

涂色那一笔（`0x59B899 mov [ebp+0x38], [0xAA0E18]`）**只写砖号**，
子块 `+0x11A` 与层高 `+0x11B` 一概不动。

### ⚠️ 共享哑元也会被收进前沿、还会被涂绿

出图的邻格不会被拦下来：`CellClass::GetNeighbourCell`（`0x481810`）只判
`dir >= 8`，坐标相加之后交给 `MapClass::TryGetCellAt`（`0x5657A0`），
后者越界时**把打包坐标写进 `[0xABDC74]`（哑元的 `+0x24`）并返回哑元
`0xABDC50`**。而哑元开局的 `+0x38` 是 0，`IsPlain`（`0x486380`：
`== 0xFFFF || == 0`）判真——于是**哑元被真的收进前沿表**，
可以被抽中、可以被涂成 `GreenTile`。

一旦被涂绿，它就不再「未定砖」，后面所有出图邻格都会被跳过。
所以哑元的 `+0x38` 与 `+0x24` 都是**可观测的产物状态**，基准里两样都比
（`green_spread_golden.txt` 的 `din=` / `dout=`）。18 个用例里
`s2-mt1-real` 就是靠这条从 `din=0` 变成 `dout=493`。

### ⚠️ 为什么这段 25 年前的代码不会越界

`0x59B830 lea esi,[ecx-1]` 把 `hi` 算成 `count - 1`。要是前沿被抽空，
`hi` 就是 −1，`ja` 恒假 → 第一次抽的数**必被接受**（哪怕是几十亿），
接着 `front[k]` 越界读。

它没崩，靠的是一条算术事实：`rounds <= count/3` 且**每轮只删 1 个**，
所以 `count` 在整段循环里至少还剩 `count - count/3 > 0` 个。
（这也解释了 `/3` 这个看着莫名其妙的常数——它同时兼任「扩张幅度」与
「防越界余量」。）

### 取数点汇编（`0x59B828`–`0x59B869`）

```
0059B828  mov   ecx, [esp+0x3C]          ; front.Count
0059B82C  mov   [esp+0x18], ebx          ; 高 32 位清零
0059B830  lea   esi, [ecx-1]             ; n-1，拒绝上界
0059B833  lea   edx, [esi+1]
0059B836  mov   [esp+0x14], edx
0059B83A  fild  qword ptr [esp+0x14]     ; (double)n
0059B83E  fstp  qword ptr [esp+0x24]     ; span 存好
0059B842  mov   ecx, 0xABE890
0059B847  call  Randomizer::Random
0059B84C  mov   [esp+0x1C], eax
0059B850  mov   [esp+0x20], ebx          ; 高 32 位清零 -> 无符号
0059B854  fild  qword ptr [esp+0x1C]     ; (double)(uint32)r
0059B858  fmul  qword ptr [esp+0x24]     ; x span
0059B85C  fmul  qword ptr [0x7ED898]     ; x 1/(2^32-1)
0059B862  call  0x7C5F00                 ; _ftol
0059B867  cmp   eax, esi
0059B869  ja    0059B842                 ; > n-1 就重抽
```

乘法顺序是 `r x span x scale`，与 `0x598030` / `0x58CAF9` / `0x594935` 完全一致。

## 三、砖块语义是怎么定下来的：TileSet 全局 ↔ ini 键名

`IsGreen`（`0x4867B0`）判的是 `cell->+0x38 == [0xAA0E18]`
或落在 `[[0xAA0748], +0x10)` 区间内；`0x59B740` 涂的也是 `[0xAA0E18]`。
这两个 `.bss` 全局的值不在文件里，得找运行期的填写者。

填写者是 `FUN_00545150`（theater 装载），做法分两段：

1. `FUN_005276D0("General", <键名>, -1)` 把 theater ini 里几十个 TileSet 键名
   对应的**组序号**读进栈上的局部变量
2. 再遍历全部 TileSet，`cmp edi, [esp+OFF]` 命中就把当前 TileSet 的
   **起始砖序号**写进对应全局

第一段的存槽指令 `mov [esp+OFF], eax` 存的是**上一次调用**的返回值，而它出现时
`esp` 已经被下一组的若干 `push` 压低了，所以真实槽位 =
`OFF − 4 × (call 与 store 之间的 push 个数)`——**不是**固定的 `0xC`。
据此把「键名 → 栈槽」和「栈槽 → 全局」拼起来，得到 46 个全局的键名，
54 个键名全部落位、无二义——
工具 `code/randmap/dump_tileset_globals.py`，结果 `memory/data/rmg/tileset_globals.txt`。

> 早前版本按"两段 `esp` 恒差 `0xC`"配对。因为大多数组恰好只压三个参数，
> 那张表**多数行是对的**，只在 push 数不同的那几组错位——
> 表现就是 4 个全局配不上键、3 组键名二义、还漏掉 1 个全局。
> 教训记在这里：**局部正确的配对规则最难发现**，
> 它给出的表看着自洽（`IsWaterOrShore` 那六个区间照样对得上），错的只有边角。
> 现在的版本在文件尾部自动生成一段
> 「硬编码宽度 vs `TilesInSet`」的交叉校验，五组吻合、三组不吻合（照抄不修）。

关键几个：

| 全局 | ini 键名 | 用在哪 |
|------|---------|--------|
| `0xAA0E18` | **GreenTile** | `IsGreen` 主判据；本阶段涂的就是它 |
| `0xAA0748` | **ClearToGreenLat** | `IsGreen` 的过渡砖区间（16 个） |
| `0xAA0738` | **WaterSet** | `IsWaterOrShore` 的水（14 个） |
| `0xABAD28` | **ShorePieces** | `IsWaterOrShore` 的岸（42 个） |
| `0xAA073C` / `0xABB110` / `0xAA1050` / `0xAA10A0` | **WaterfallEast / West / South / North** | `IsWaterOrShore` 的四个瀑布（各 4 个） |
| `0xAA10B0` | ClearTile | 空地 |
| `0xABC2B8` | RoughTile | 粗糙地 |
| `0xABB10C` | Rocks | 石头 |

**这张表本身就是一次交叉验证**：`IsWaterOrShore`（`0x4865D0`）认的六个区间，
反查出来正好是 `WaterSet + ShorePieces + 四个方向的瀑布`——语义严丝合缝。
（注意：这一条在旧的错位版本里**也成立**，见上面那段教训；
真正把配对钉死的是文件尾部那五组"硬编码宽度 == TilesInSet"。）

## 四、`CellClass::RecalcAttributes(-1)` 不是空转——纠正旧结论

旧版这里写着「真正的 `RecalcAttributes` 遍历一个随机数都不取」，
容易被读成「它对地形没影响」。**不对。** 把 `0x47D2B0`–`0x47DD61`
七百多条指令全部反汇编、逐条筛「写格子字段」的指令，
碰**地形三件套**（`+0x38` 砖号 / `+0x11A` 子块 / `+0x11B` 层高）的只有四条：

| 地址 | 写什么 | 触发条件 |
|------|--------|---------|
| `0x47D58D` | `+0x38 = 0xFFFF` | 砖号 `>= [0xA8ED38]`（已装载砖总数，**有符号** `jl`） |
| `0x47D5E6` | `+0x38 = 0xFFFF` | 子块在 `.tmp` 里是空洞 |
| `0x47D5E9` | `+0x11A = 0` | 同上，跟着一起清 |
| `0x47D94E` | `+0x11B = 栈参` | **仅当栈参 != −1** |

也就是说这一遍是一次**校验/清理**：非法砖号、指向空洞的子块，
统统整格作废成 `0xFFFF`。它还无条件重算坡道类型 `+0x11C`（`0x5471B0`）。

### 那个 `-1` 的语义已经钉死了

`0x47D2B0` 的序言是 `sub esp,0x38` 加 4 个 `push`，所以返回地址在 `esp+0x48`、
唯一的栈参在 `esp+0x4C`。而：

```
0047D945  mov eax, [esp+0x4C]
0047D949  cmp eax, -1
0047D94C  jne ...              ; 只有 != -1 才走到 0x47D94E 写 +0x11B
```

于是 `-1` 不是旧版猜的「不指定 theater」，而是 **「不覆盖层高」**。
RMG 里三处调用全传 `-1` ⇒ 这一遍**一格的层高都不会改**。
（§六 原第 2 条未确认关闭。）

### 前提条件是实锤，不是假定

函数开头 `0x47D2FF` 判 `CellClass +0x44`（挂在这一格上的 overlay 序号）：
不是 −1 就走 `0x47D306` 那条完全不同的分支。RMG 阶段 5 必然走
`0x47D57B` 这条，因为：

- 阶段 1 的清格子循环（`memory/data/decomp/rmg_init_map.txt` 263–267）
  逐格写 `+0x11C=0 / +0x11B=4 / +0x38=0 / +0x11A=0 / **+0x44=0xFFFFFFFF** / +0x11E=0`；
- 全流程里**第一个**改写 `+0x44` 的是阶段 8 的矿
  （`rmg_ore.txt` / `rmg_tiberium.txt` 里的 `cell+0x44 = ...`），排在阶段 5 **之后**。

另外砖号 `== 0xFFFF` 时 `0x47D595` 跳去 `0x47DB22`，那条分支不碰这三样。

### 两个被叫到的小函数

| 地址 | 语义 |
|------|------|
| `0x544C20(subtile, flag)` | 「`.tmp` 里这一格有真子块吗」：`n = hdr[0]*hdr[1]`，`subtile >= n` 判假；偏移表 `hdr[0x10 + subtile*4] == 0`（空洞）判假。`flag` 只控制「`.tmp` 还没装载就现装」 |
| `0x5471B0(subtile)` | 坡道类型：`idx = subtile % (w*h)`（`idiv` **取余**，不是边界判定），偏移表该项为 0 → 0，否则子块头 `+0x2A` 的**有符号字节** |

`0x47D5BA` 那里有个「`+0x11C` 非 0 就用 `flag=1` 再判一次子块存在性」的重试。
theater 全部装载完的前提下两次调用同值 ⇒ **重试不可观测**，C++ 侧不建模，
也因此不必给 `TileWorld` 加 `+0x11C` 的读接口。

## 五、C++ 落地与对拍

实现 `code/rewrite/rmg/recalc.{h,cpp}`：`SpreadGreen` / `RecalcAttributes` /
`RecalcAttributesPass`。对拍 `test/test_green_spread.cpp`（ctest 目标
`green_spread`），基准 `memory/data/rmg/green_spread_golden.txt`
（18 个用例，生成脚本 `code/randmap/dump_green_spread_golden.py`）。

基准是把 `0x59B740` **整个函数**丢进 Unicorn 跑原版机器码取的，一个桩都不留：
`IsGreen` / `IsPlain` / `GetNeighbourCell` / `TryGetCellAt` 的哑元退化 /
`DynamicVectorClass` 构造与 `Resize` / `Randomizer::Random` 全是原身。
每例给一张输入地形（`IN` 行）与哑元进场状态，跑完比四样：
随机数调用次数、菱形内每格终态 `(tile, subtile, level)`、
哑元 `+0x38` 终值、哑元 `+0x24` 终值。

> 对拍侧要单独测 `0x59B740`，而 `s2-*` 那批用例会先跑阶段 2 拿真实地形、
> 顺手消耗掉一大截随机数。所以脚本在进 `0x59B740` **之前重播一次种子**
> （`reseed()` 调 `0x65C6D0`），基准里的随机流就从「刚播种」算起，
> C++ 那边一个 `Randomizer(seed)` 正好对上。

18 个用例分两类：

- `s2-*` 8 例：先跑阶段 2 调度器拿真实地形（绿地是沿岸刷出来的那一圈），
  覆盖 MapType 0/1/2、三套砖尺寸剖面、10×10 到 40×40。
  其中 `s2-allwater` 与 `s2-mt2-p4` 前沿为空 → `rounds == 0`，
  专打 `0x59B81C` 那条 `jle` 守卫（`rng=0`）。
- `syn-*` 10 例：合成地形，前沿的形状与规模攥在手里。
  ⚠️ 底色必须铺**未定砖（tile 0）**——`IsPlain` 只认 0 与 0xFFFF，
  铺 `tile 1` 的话一格都收不进前沿，整条循环退化成空转（踩过这个坑）。
  `syn-checker` 866 个取数点、`syn-cap1000` 正好顶到 **1000 上限**、
  `syn-lat` 专走 `IsGreen` 的 `ClearToGreenLat` 那条判据、
  `syn-corners` / `syn-all-green` 专驱哑元那条路。

变异测试确认下列细节**都有对拍覆盖**（改了就炸）：
保序前移 vs 拿末尾顶上、先取元素 vs 先删、`rounds` 一次算定 vs 每轮重算、
`/3` 的除数、1000 上限（去掉 / 改成 1001 都炸）、只看 4 个正方向 vs 8 邻域、
入队不去重、哑元的那一笔写。

`RecalcAttributesPass` 目前是**有取证、无对拍**：它要读 theater 的砖表
（`.tmp` 子块的空洞与 `+0x2A`），等运行期能从 mix 里读 `.tmp` 之后再补基准。

## 六、未确认

1. ~~四个全局没匹配到键名、另有三组二义~~ —— **全部关闭**（修了配对算法，见上）：
   `0xAA0E1C = RoughGround`、`0xAA1024 = MonorailSlopes`、`0xAA1090 = WaterBridge`、
   `0xAA10A4 = MiscPaveTile`；二义定为 `0xAA0744 = HeightBase`、
   `0xABB108 = TrackTunnels`、`0xAA109C = MMRampBase`；
   另补回一个此前整个漏掉的 `0xABC1D8 = RampBase`（那处比较写成
   `mov eax,[esp+OFF] / cmp edi,eax`，与其余的 `cmp edi,[esp+OFF]` 不同形）。
   `0xAA1020 = CliffSet` 这次拿到了**第三条独立佐证**：
   `ToSnowTheater` / `ToTemperateTheater` 这两个键根本不回填任何全局
   （前两条：两套 theater ini 里都没定义这两个键；`CliffSet` 的 `TilesInSet = 40`
   与 `0x5A17F0` / `0x4863D0` 硬编码的 `0x28` 吻合，
   见 [stage-making-regions.md](stage-making-regions.md) §八）。
   `0xAA10A4 = MiscPaveTile` 与 `0xABC2B0 = PaveTile` 随后又被阶段 4 的
   `0x486650` / `0x486670` 独立印证（§五之三）
2. ~~`CellClass::RecalcAttributes(-1)` 的参数 `-1` 语义（推测是"不指定 theater"）~~
   —— **已关闭**：`0x47D949 cmp [esp+0x4C], -1` 证明它是「不覆盖层高」（见 §四）
3. `0x59B740` 为什么没有自己的阶段日志——多半是后加的一段，作者懒得加
4. `[0xA8ED38]`（已装载砖总数）的填写者还没查；C++ 侧当参数 `tile_count` 传入
5. `RecalcAttributes` 里剩下那六百多条指令（阴影/地面类型/可通行性缓存等）
   与地形输出无关，只做了「不写地形三件套」的排除性核对，没有逐条逆向
