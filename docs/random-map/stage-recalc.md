# 阶段 5：绿地扩张 + Recalculating cell attributes

> 状态：**`0x59B740` 全流程与唯一取数点已实锤**；砖块语义靠新做的
> 「TileSet 全局 ↔ theater ini 键名」映射反查得到（见 §三），并与
> `IsWaterOrShore` 的成员表互相印证。
> 取证：`memory/data/decomp/rmg_starting_points.txt`、`memory/data/rmg/tileset_globals.txt`。

## 一、它在流水线里的真实位置

主流程 `FUN_00598960` 的写法是「先打日志、再干活」，唯独这里有个例外：

```c
FUN_004068e0("RMG: Making regions");
if (MapType == 3 || MapType == 4) { ...五个函数... }   // 条件分支，默认不跑
...
FUN_0059b740();                                        // ← 没有日志，无条件执行
FUN_004068e0("RMG: Recalculating cell attributes");
MapClass::CellIterator_Reset();
while (cell = MapClass::CellIterator_Next())
    CellClass::RecalcAttributes(cell, -1);             // 不取随机数
```

也就是说 **`0x59B740` 夹在两条日志之间、没有自己的阶段名**。
按执行顺序它归在这一阶段，但它跟"重算格子属性"没有关系——
它做的是**给绿地长个边**。真正的 `RecalcAttributes` 遍历一个随机数都不取。

## 二、`0x59B740`：绿地随机蔓延

### 数据结构

局部一个 `DynamicVectorClass<CellClass*>`（构造 `0x5AD820(0,0)`，vtable `0x7ED9BC`，
扩容步长 **10000**），布局这次也顺带钉死了：

| 偏移 | 内容 |
|------|------|
| `+0x00` | vtable |
| `+0x04` | 数据指针 |
| `+0x08` | 容量 |
| `+0x0C` | `IsInitialized`（推断） |
| `+0x0D` | `IsAllocated`——为 0 且容量非 0 时**拒绝扩容**（借来的缓冲区） |
| `+0x10` | 元素个数 |
| `+0x14` | 扩容步长 |

`AddItem` 被完整内联（`0x59B7A4`–`0x59B7E6`）：
`count < capacity` 直接写；否则要求 `IsAllocated || capacity == 0`
且 `growth > 0`，再调虚函数 `+0x08` 的 `Resize(capacity + growth, 0)`。

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

三个必须照抄的细节：

1. **`rounds` 在第一遍扫完之后一次算定**（`0x59B7FF` 的 `imul 0x55555556` 是
   编译器把 `/3` 变成的乘法），后面前沿再怎么涨都不改
2. **抽样的 `span` 每轮重算**——`front.Count` 每轮减 1 又加 0..4，
   `0x59B828`–`0x59B83E` 就在循环体内
3. **删除是保序前移**（`0x59B885` 的搬移循环），不是"拿末尾顶上"。
   这决定了后续下标含义，抄错就全岔
4. 入队去重？**没有**。同一个空地格可以被多个绿地邻居重复收进前沿，
   也可以在被涂绿之后再次作为别人的邻居入队——照抄即可

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

## 四、未确认

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
2. `CellClass::RecalcAttributes(-1)` 的参数 `-1` 语义（推测是"不指定 theater"）
3. `0x59B740` 为什么没有自己的阶段日志——多半是后加的一段，作者懒得加
