# 阶段：Seeding water（水域播种）

> 状态：**结构已摸清，数值细节未完成**。本文只记录已取证的部分，
> 推测一律标注「未确认」。涂块核心 `FUN_0059BBC0` 留待下一轮。

## 一、这个阶段其实是「从整张水里抠出陆地」

`FUN_0059A6C0`（调度，`RMG: Seeding water` 日志之后第一个调用）开头：

```c
for (cell : MapClass::Instance)         // 0x578350 Reset / 0x578290 Next
    cell->IsoTileTypeIndex = [0xAA0738];   // 全图先铺成水
```

`CellClass::IsoTileTypeIndex` 在 `+0x38`（YRpp 布局：AbstractClass 0x24 +
MapCoords 4 + FoggedObjects 4 + BridgeOwnerCell 4 + unknown 4 + LightConvert 4 = 0x38 ✅）。
`[0xAA0738]` 是水面 tile 组的起始索引（运行期由 theater 决定，**未确认**具体来源）。

所以"Seeding water"名不副实：**地图初态就是一片汪洋，后续是在水里画陆地**。
这也解释了为什么原版随机地图容易出岛——陆地是"画"出来的，画不到就是海。

## 二、三种 MapType 三套布局

```c
switch (MapSeedClass->MapType /* +0x3C */) {
  case 0: FUN_0059AD10(); break;   // 按点列表撒块
  case 1: FUN_0059AFA0(); break;   // 按目标覆盖率生长
  case 2: FUN_0059B200(); break;
}
// MapType 3/4 走另一条路：FUN_0059C580 里判断 (MapType==3||4) && WaterAmount>0x14
```

`MapType` = `.sed` 的 `MapType=`（实测样例值 3）。三个分支都共用涂块核心
`FUN_0059BBC0`，只是**投放策略**不同。

### MapType 1（`FUN_0059AFA0`）：目标覆盖率驱动

```c
target = (0.5 - 0.45) * (1.0 - WaterAmount * 0.01) + 0.45;   // 常数 0x7ED728 / 0x7ED720 / 0x7E3808
// WaterAmount=0   -> 0.50
// WaterAmount=100 -> 0.45
double total = FUN_0042B1F0();          // 分母（可用格子总数，未确认）
int painted = 0;
for (int iter = 0; iter < 100; ++iter) {
    painted += FUN_0059BBC0(..., 0.75 /* 0x3FE80000 */, 1);
    if (painted / total >= target) break;
    // 再找一个离当前中心「曼哈顿距离最近、且工作记录 +0x3C == 0」的格子作为下一个中心
}
```

注意目标覆盖率区间只有 **0.45-0.50**，而且 `WaterAmount` 越大目标越**小**
——与"陆地是画出来的"这一层反转正好自洽（画得越少水越多）。

### MapType 0（`FUN_0059AD10`）：按点列表撒块

```c
n = max(NumPlayers /* +0x50 */ / 2, 2);
do { r = PickBelow(n); } while (...);            // 浮点比例+拒绝采样惯用法
FUN_0059A8F0(&list, NumPlayers + r, &bounds);    // 生成 (x,y,w,h) 四元组列表
for (each record in list)
    重复最多 10 次 FUN_0059BBC0(..., 0.25 /* 0x3FD00000 */, 0) 直到成功
```

每成功一块，`MapSeedClass+0x308` 自增——**该字段是"已投放块编号"计数器**，
涂块时会写进工作记录 `+0x3C`，等于给每块地一个 ID（区域划分的雏形）。

## 三、涂块核心 `FUN_0059BBC0`（3 处取随机数）

**已确认**的部分是它的"随机选点"前半段：

```c
// 未指定起点时（param_3 的两个分量都为 0）随机找一个格子，最多试 200 次
do {
    rx = PickBelow([0x87F8DC]);        // 地图宽
    ry = PickBelow([0x87F8E0]);        // 地图高
    X  = rx + ry + 1;                  // 等距坐标变换
    Y  = ry - rx + [0x87F8DC];
} while (work[(Y * side + X) * 0x50 + 0x38] != 0 || !FUN_004865D0(cell));
```

这就是 RMG 的**随机取格**通用写法：先在矩形 (W×H) 里取，再变换到菱形可用区。
`PickBelow` 即 `docs/random-map/fpu-and-random-idioms.md` 里那套浮点惯用法。

后半段是一个**带浮点优先级的二叉堆（最小堆）驱动的扩散**：
`operator_new(0x14)` 出来的 5 字段结构 `{count, capacity, array, max, min}`，
节点是 `{packed(X,Y), float priority}`，`param_2` 传进来的 `(w, h)` 被换算成
两个 `1/(r·s)²` 系数（`0x7E1738`）用于把距离归一化成椭圆——
**即"以给定长宽比向外长一团椭圆形陆地"**。具体推进规则与终止条件**未确认**，
下一轮专门做。

## 四、已确认的谓词与全局

| 地址 | 判定 | 依据 |
|------|------|------|
| `0x486380` | `tile == 0 \|\| tile == 0xFFFF`（地块未指定） | 反编译，只读 `+0x38` |
| `0x4865B0` | `tile ∈ [ [0xABAD28], +0x2A )`（某 42 格 tile 组内） | 反编译；**组的语义未确认** |
| `0x4865D0` | 取格可用性谓词 | 被随机选点循环用作接受条件；**语义未确认** |
| `0x87F8DC` / `0x87F8E0` | 地图宽 / 高 | 多处用作随机上限；`FUN_00599650` 用 `W/2+H/2` 算中心 waypoint |
| `0x89C2DC` | 工作数组边长（方阵） | `0x578E60` 按 `side²` 分配 |
| `0xABED10` | 每格 0x50 字节工作记录数组 | `0x578E60` 分配 `side²×0x50` |
| `0xAA0738` | 水面 tile 组起始索引 | 全图初始化与 `FUN_0059C630` 都用它做基准 |
| `0xAA0E18` | 岸边 tile 索引 | `FUN_0059A6C0` 末尾给水陆交界格赋值 |

工作记录已知字段（**其余未确认**）：

| 偏移 | 已观察到的行为 |
|------|----------------|
| `+0x38` | 阶段开始置 `0xFFFFFFFF`；随机选点要求 `== 0` |
| `+0x3C` | 布局前清 0；涂块时写入 `MapSeedClass+0x308`（块编号） |
| `+0x4A` | 数组分配后置 1 |

## 五、水面 tile 细化 `FUN_0059C630`（已完整还原）

对每个"是水且 `+0x11A == 0`"的格子：

```c
if (右/下/右下三邻格也都是水且 +0x11A == 0) {
    a = PickFrom1(10);                       // 1..10，常数 0x7ED9E8
    if (a != 1) {
        b = PickBelow(242);                  // 0..241，常数 0x7ED9E0
        shape = (b >= 240) ? (247 - b) : (b / 10);     // 0..23 或 6..7
        FUN_005A6C10(work[cell].field_38, -1);          // 用 shape 贴大块水面
        continue;
    }
}
c = PickBelow(201);                          // 0..200，常数 0x7ED9D8
cell->IsoTileTypeIndex = [0xAA0738] + 8 + c / 40;      // 8..13 号小水面
```

三处取数的比例常数、拒绝阈值、整数除法魔数（`0x66666667`→/10、
`0xCCCCCCCD`→/40）都已逐条汇编核对，
基准值见 `memory/data/rmg/scaled_golden.txt`，重写已对拍通过。

## 六、下一轮

1. 啃 `FUN_0059BBC0` 的堆扩散规则（这是陆地形状的全部来源）
2. 确认 `FUN_0059A8F0` 生成的 `(x,y,w,h)` 四元组含义（疑似按玩家数分区）
3. 统计本阶段在各 MapType 下消耗的随机数个数
4. 确认工作记录 0x50 字节的完整字段布局
