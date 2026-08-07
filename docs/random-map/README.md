# 遭遇战随机地图生成器（RMG）逆向

目标：**字节级复刻**原版 `gamemd.exe` 的随机地图生成算法——同一组参数 + 同一种子
必须产出与原版完全相同的地图。生成地图只是副产品，真正的产出是"可验证的对齐"。

## 文档索引

| 文档 | 内容 | 状态 |
|---|---|---|
| [mapgen-tu.md](mapgen-tu.md) | MapGen.cpp 编译单元测绘 + 生成流水线全景 | ✅ 骨架完成 |
| [randomizer.md](randomizer.md) | 随机数发生器 R250（播种 + 取数 + 区间随机） | ✅ 逐位对齐 |
| [parameters.md](parameters.md) | MapSeedClass 参数集与 `.sed` 文件格式 | ✅ 已实锤 |
| [fpu-and-random-idioms.md](fpu-and-random-idioms.md) | 原版全局浮点环境 + 生成器取随机数的惯用法 | ✅ 已实锤并对拍 |
| [fast-math.md](fast-math.md) | 查表近似 sqrt + Marsaglia 正态分布（形状的数学来源） | ✅ 已实锤并对拍 |
| [rng-ledger.md](rng-ledger.md) | **随机数消耗总账**：104 个取数点 + 阶段归属 + 46 处死代码 | ✅ 清单已实锤 |
| [map-geometry.md](map-geometry.md) | **等距地图几何**：格子集合（菱形四不等式）+ 扫全图顺序 + 总格数 | ✅ 已实锤并对拍 |
| [stage-init.md](stage-init.md) | 阶段：Init random map（搭空 scenario，只抽 1 个随机数）+ **地图尺寸的来源** | ✅ 取数点与尺寸算法已实锤 |
| [stage-init-regions.md](stage-init-regions.md) | 阶段：Init regions（把地图切成连通区域，只抽 1 个随机数） | ✅ 流程与取数点已实锤 |
| [stage-tiberium.md](stage-tiberium.md) | 阶段：Creating tiberium（布矿） | ✅ 外层 + 矿田生长全链已实锤 |
| [stage-tech-buildings.md](stage-tech-buildings.md) | 阶段：Adding tech buildings（中立科技建筑，两条互斥分支） | ✅ 已实锤，含 `PickRange` 语义 |
| [stage-recalc.md](stage-recalc.md) | 阶段：绿地蔓延 + Recalculating cell attributes | ✅ 已实锤，附 TileSet 全局↔ini 键名映射 |
| [stage-starting-points.md](stage-starting-points.md) | 阶段：Creating starting points（出生点，**整段可重试**） | ✅ 结构与 3 个取数点已实锤 |
| [stage-hills.md](stage-hills.md) | 阶段：Creating hills（造山，唯一随机源是高度场） | ✅ 随机部分 + 角点抬升子系统全链已实锤 |
| [stage-lats.md](stage-lats.md) | 阶段：Creating LATs, rocks etc（地表装饰，取数点最密） | ✅ 18 个取数点全部实锤 |
| [stage-cleanup.md](stage-cleanup.md) | 阶段：Cleanup / Radar + **阶段之间的隐藏动作** | ✅ 已实锤，纠正了“Cleanup 改地形”的误记 |
| [stage-seeding-water.md](stage-seeding-water.md) | 阶段：Seeding water（从整张水里抠陆地） | ✅ 算法全还原；**`MapType 0/1/2` 主路径 + 水面细化整条落成 C++ 并对拍** |
| [stage-water-maptype34.md](stage-water-maptype34.md) | 阶段 2 支路：`MapType ∈ {3,4}` 的**河流、湖泊与瀑布**（含查表 sin/cos） | ✅ 八个函数全部实锤；sin/cos 已落 C++ 并对拍；整支路待落 C++ |

## 进度

- [x] 生成器本体定位（`MapSeedClass`，YRpp 符号交叉验证）
- [x] MapGen.cpp 编译单元范围测绘（调试宏反推）
- [x] 生成流水线阶段全景（原版调试日志字符串自带阶段名）
- [x] 随机数发生器逐位复刻 + Google Test 回归
- [x] 原版全局 FPU 状态实锤（`_control87(_RC_CHOP,_MCW_RC)`，控制字 0x0E7F）
- [x] 生成器全局 `Randomizer @ 0xABE890`：**整张地图只播种一次**，种子取自 `.sed`
- [x] 浮点比例 + 拒绝采样取数惯用法复刻 + 对拍（`scaled_random`）
- [x] **`0xABE890` 的使用边界实锤**：153 处引用全在 `0x58B772-0x5AC192`，引擎侧一处没有
- [x] 生成器取数点全清单：**150 处 `Random`、0 处 `RandomRanged`**（按 this 配对）
- [x] **总账落地**：150 处里 46 处是链接器留下的死代码（22 个函数零引用），
      真正会执行的是 **104 处**，且已逐点归到 12 个阶段名下 → [rng-ledger.md](rng-ledger.md)
- [x] `UniformUnit@0x598000` 只经函数指针 `[0xABDFB8+0x10]` 被 `NormalDeviate` 调用；
      正态分布的缓存标志**跨阶段串**，只在每张地图开始时清零
- [ ] 各阶段算法还原（地形/水域/区域/起始点/矿/山丘/装饰）
  - [x] Init random map：**全阶段只消耗 1 个随机数**（`0x59A4C7`，25% 抛硬币）+ 格子/工作数组初值
  - [x] Init random map：**地图真实尺寸的算法实锤**——`.sed` 的 `Width`/`Height` 是 0..3 档位不是格数，
        `W/H = lerp(表Lo[NumPlayers-2], 表Hi[NumPlayers-2], min(档位/3, 1.2))` 后 `_ftol` 截断；
        四张 7 项表已 dump；`side = W + H + 1` 实锤（`0x42AC5C`）；
        **已复刻并全网格对拍**（1260 例，`code/rewrite/rmg/map_size.cpp`）——
        实锤了「插值必须逐步 chop 舍入」：精确算术会把 `70×(1-1.2)+80×1.2` 算成 82，原版是 81
  - [x] Seeding water：阶段结构已还原
  - [x] Seeding water：涂块核心 `FUN_0059BBC0`（最小堆扩散 + 中心布朗运动）算法已还原
  - [x] Seeding water：**最小堆的并列语义已实锤并对拍**——下沉/上浮一律「严格大于」，
        相等不交换（堆序就是陆地生长顺序，这里错一位整张图就散）；
        另钉死两条怪癖：门槛 `count+1 >= cap` 让最后一格永远空着、堆满时静默丢弃候选格
  - [x] 数学基元 `FastSqrt` / 正态分布 / `ChopLog` 复刻并逐位对拍（含架构无关黄金值）
  - [x] Seeding water：三种 MapType 的投放策略 + 区域网格 `FUN_0059A8F0` 已还原
  - [x] Seeding water：岸线平滑 `FUN_0057A0C0` 四遍结构 + **它会消耗随机数**（传递闭包取证）
  - [x] Seeding water：`FUN_0057B210` 八邻域「水」掩码的位定义与三种 mode 门槛已实锤
  - [x] Seeding water：四遍平滑的贴砖三张表已实锤——`tile = TileTypes[ShorePieces + i - 1]`、
        落点偏移表 `[0xABDB64]`（CRT 静态初始化器 `0x57A9C0` 填，已模拟执行 dump）、
        两张 `.data` 静态属性表 `0x82A7F4`(族相等) / `0x82A89C`(朝向差 ∈ [3,5])
  - [x] Init regions：四步流程 + 唯一取数点 `0x58CAF9`（内联 `PickRange(0,n-1)`）+ 三个谓词已实锤
  - [x] Recalc cell attributes：真正的 `RecalcAttributes` 遍历**不取随机数**；那 1 个随机数来自没有阶段日志的 `0x59B740`（绿地随机蔓延）
  - [x] **TileSet 全局 ↔ theater ini 键名映射**（45 个），`IsWaterOrShore` 的六个区间反查出正好是 WaterSet+ShorePieces+四瀑布，互为佐证
  - [x] Creating tiberium：**全链已实锤**——外层 `0x5A23A0`（真值表开关 / 出生点重心 / 正负 2 sigma 窗口）
        + 矿田预算 `size = (int)((avgDist[w] - mn) * 15) + 500`（**离矿区最近的出生点恰好拿 500**）
        + 内层 `0x5A28C0` 最小堆扩散（overlay = 102 矿 / 27 宝石 `+ PickBelow(12)`，厚度在 `+0x11E`）
  - [x] Adding tech buildings：`MapType==2` / 其它 两条互斥分支 + 6 个取数点已实锤；业主是 Neutral，建筑表 = `rules.ini [General] NeutralTechBuildings=`
  - [x] Creating starting points：**外层重试循环**（失败一轮的随机数不退回）+ 面积比例配额 + 3 个取数点已实锤
  - [x] Creating hills：入口 `0x5A35F0` 六步已拆开；**全部随机性只在 `0x5A2F50`**（顺序推进的分形高度场，每格 2 次截断正态，`Ruggedness < 10` 时一次都不抽）；高度取整是 round-half-away-from-zero，**不是 ties-to-even**
  - [x] Creating LATs：温带 `0x5A38C0`+`0x5A3AE0` / 非温带 `0x5A4280` 两条互斥分支的主体已实锤；18 个取数点归属到函数；新查实 `0x486790 = IsClearToSandLat`、`0x42B1F0 = (H+4)*W*2`
  - [x] Creating LATs：补丁生长 `0x5A45E0`（最小堆扩散 + `PickRange(1,25)` 选 `TREE01`..`TREE25`，**`TREE00` 永不出现**）与 LAT 写入器 `0x5A4B60` 已还原，**18 个取数点全部落地**
  - [x] 顺手关掉阶段 8 的未确认：`0x71BB90` = `TerrainClass::TerrainClass(type, cell)`（Ghidra 误标为 `WhatAmI`）
  - [x] Cleanup / Radar：**纯拆解，不碰地形**；另查实两件事——
        布矿之后有一段无日志的“区域清算”（work.+0x38/+0x3C 写回 -1 并销毁全部 RegionClass）；
        `RecalcAttributes` 全程跑 **4 遍**，其中 #2/#3 之间什么都没干
  - [x] Creating hills：角点抬升子系统全链已实锤（`0x6B2A70` 建格网 / `0x6B4100` 读当前级 /
        `0x6B4240` 选角掩码 / `0x6B3E60` 抬一级+撤销栈 / `0x6B3A80` 递归验坡 / `0x6B3850` 刷砖）——
        无随机，但影响地形字节；顺手查实 `RampTable`(0x83FF18) 19 项全表与 `RampBase = [0xABC1D8]`，
        并登记 7 条原版怪癖
- [ ] 📌 **TODO（延后）**：把那 22 个死代码函数（27+ 取数点集中在 `0x5A5020` 一棵子树）单独逆一遍——疑似 TS 时代或 YR 开发期废弃的整套地形生成方案，对"重写红警2"那一层目标有算法参考价值。**前置条件**：104 个活取数点全部还原、端到端逐字节闭环跑通之后再动，详见 [rng-ledger.md](rng-ledger.md) §二
- [x] **地图几何实锤**：格子集合 = 菱形四不等式（`X+Y>W`、`|X−Y|<W`、`X+Y<=W+2H`），
      扫全图顺序 = `Reset 0x578350`/`Next 0x578290` 的斜线走位，总格数 `N = H(2W−1)`；
      两条独立证据链（迭代器算术 / 边界比较指令）算出的集合**完全相等** → [map-geometry.md](map-geometry.md)
- [x] **`MapClass::Instance = 0x87F7E8` 实锤**（3693 处 `mov ecx` 立即数）——
      一并关掉两条老未确认：`[0x87F8DC]/[0x87F8E0]` 就是 `+0xF4/+0xF8` 成员，
      格子指针数组是 `+0x138` 那个内嵌 `VectorClass<CellClass*>`，
      由 `0x565B87` 的 `Resize(0x40000, NULL)` 分配并整表置 `NULL`（菱形外确为 `NULL`）；
      另纠错：早前记的迭代器 `this = 0x87F978` 是漏扫 `E8 rel32` 造成的**错误结论**
- [x] **涂块核心 `0x59BBC0` 整函数落成 C++ 并对拍**（`code/rewrite/rmg/blob_paint.{h,cpp}`）——
      对拍的不是标量而是**整条生长顺序**：Unicorn 整函数模拟 + 涂格钩子导出黄金序列，
      14 组用例约一万格**逐格坐标完全一致**，随机数取数次数也逐组吻合；
      顺手关掉 `MapSeedClass +0x308`（= 块编号/生成戳计数器），
      并新记四条此前漏掉的行为：起点是 `int[2]` 而目标点是打包 `int16`、
      平滑成功后有一次**无条件**的全图岸砖清扫、循环末尾弹出的最后一格被白白丢掉、
      排空循环的 `painted++` 无条件执行且就是返回值
- [x] **岸线平滑的八邻域掩码 `0x57B210` 落成 C++ 并对拍**（`shore_mask.{h,cpp}`）——
      1299 次查询（2 张随机图 × 全部格子 × 3 种 mode）、122 个不同掩码值全中；
      同时纠错两处：**第 2 遍 `0x57A320` 的返回值被调用方彻底丢弃**
      （它自己也从没设过 `eax`），"任一遍失败就作废"对第 2 遍不成立；
      第 2 遍的判水区间是 `[water, +0x0C)` 而非公共谓词的 `+0x0E`。
      另查实贴砖的交接方式：`[0x880990]` 存砖对象、`MapClass +0x1174`（= `[0x88095C]`）存落点，
      由 `0x57B440` 读走（贴砖器已复刻，见下）
- [x] **区域网格 `MakeGrid 0x59A8F0` 落成 C++ 并对拍**（`make_grid.{h,cpp}`）——
      16 组用例比对整条矩形序列 + 取数次数，外加"产出恒等于 n"的性质测试；
      顺手把两处共用的取数惯用法收进 `scaled_random.h`：
      `PickBelowMul`（上限不折叠的两次乘法）与 `PickRange`（`0x598030`，
      实测 `0x598089` 是 `ja` **无符号**比较，且 `lo`/`span` 都按 `uint32` 读）
- [x] **三种 MapType 投放策略落成 C++ 并对拍**（`place_blobs.{h,cpp}`）——
      12 组用例比对整张地图的陆地生长顺序 + 取数次数 + 块编号；
      顺手纠了文档两处实质性错误：MapType 0 的区域数下界是 `NumPlayers + 1` 不是
      `NumPlayers`；MapType 1/2 的**目标点与最近格参照点是焊死的**，
      循环里只有起点在动（原版把它们留在 `ebx`/`ebp` 里从没回写）
- [x] **theater 数据链打通**（[theater-data.md](theater-data.md)、`theater.{h,cpp}`）——
      写了只读 mix 解析器取出 `TEMPERATMD.INI` / `SNOWMD.INI`，
      查实全局里存的是 TileSet 的**起始砖号**（一条前缀和，`0x545C6B` 的 `ebx` 累加），
      不是组序号；`IsWater` 硬编码的 `0x0E` 与岸砖清扫的 `0x2A`
      正好等于 WaterSet / ShorePieces 的 `TilesInSet`，两条独立交叉验证
- [x] **贴砖器 `0x57B440` 落成 C++ 并对拍**（`place_tile.{h,cpp}`）——
      21 组用例比对返回值、出参 `ok` 与每个被写格子的 `(砖号, 子格号, 层高, 归属)`。
      `TileTypeClass +0x2E4/+0x2E8` 实锤为 `.tmp` 文件头的两个**字节**（`0x546B4A`），
      空洞子格与逐子格高度增量也都验过。三处反直觉行为照原样复刻：
      撞上"别人的、同族的"砖会**当场判成功返回**、剩下子格一格不贴；
      一条出口返回 `false` 却**不动 `*ok`**（调用方只看 `*ok`，等于静默丢砖）；
      归属判定排在 allow 区间之前，所以 allow 包住旧砖号时也可能失败。
      另查实两份地形保护判据 `0x4863D0` / `0x578D80` **判据不一致**（组数与 -1 检查都不同）
- [x] **岸线平滑 `0x57A0C0` 四遍编排整体落成 C++ 并对拍**（`shore_smooth.{h,cpp}`）——
      21 组用例（`flag ∈ {0,1}`、圆盘/地峡/孤水点/五档密度噪声/全陆/全水、两套砖尺寸剖面）
      比对返回值、`Random()` 取数次数、以及每格终态 `(砖号, 子格号, 层高, 归属)`；
      `0x57B12D` 选中的 40 个砖下标**全部走到**。至此阶段 2 的四个零件
      （投放策略 → 涂块 → 掩码 → 贴砖 → 岸线）串成一条完整链路。
      过程中挖出四条：**落点偏移表 `0xABDB64` 在文件映像里是全零**，由 CRT 静态初始化器
      `0x57A9C0`–`0x57ACE8` 运行期填出——模拟执行的夹具必须先跑它，否则每块砖都落在中心格；
      第 1 遍向八邻的**递归返回值被丢弃**，深处的"撞到别的陆块"传不回来；
      `0x57B0E8` 的 `cmp eax,-1` 是死代码（`tile = -1` 初始化式的残留）；
      只有"东边是水"那一支给 `r%3` 多套了一层 `abs`。
      并纠正文档一处错误：贴砖器第六个参数（清零归属）第 3/4 遍传的是
      `Shoreline` 自己的 `flag`，不是先前写的常数 0。全镜像 5 个 `Shoreline`
      调用点里 `flag` 有 1、0、变量三种（涂块收尾传 1，阶段 2 调度收尾传 0），
      两种取值基准里都覆盖到了
- [x] **阶段 2 调度器 `0x59A6C0` 落成 C++ 并对拍**（`stage_water.{h,cpp}`）——
      目前最"整"的一条对拍链：一次调用把**铺水 → 投放策略 → 涂块核心 →
      岸线四遍 → 掩码 → 贴砖器**的原版真身全跑一遍（模拟执行侧只留
      `IsPaintable` 一个桩），12 组用例比对随机数次数、块编号、共享哑元的
      `+0x38`、工作数组的 `+0x38/+0x3C` 与每格终态。
      至此 `MapType ∈ {0,1,2}` 的阶段 2 主路径**整条闭合**。
      新记三条：孤水格回填判水用的是**等值比较**而非判水区间；
      工作数组清账写的是 **-1** 不是 0；收尾刷绿写的是**邻格**，
      出图的邻格就是共享哑元——它一旦被写成 GreenTile 就不再"未定砖"，
      后续所有出图邻格都被跳过（对拍中确认这条副作用会改变产物）。
      另确认一条原版真实行为：MapType 0 在小图上每块陆地都铺不出岸线、
      整块回滚，**终态全是水**（基准 `mt0-allwater` 专门盯住它）
- [x] **水面细化 `0x59C630` + 压砖器 `0x5A6C10` 落成 C++ 并对拍**
      （`water_refine.{h,cpp}` + `place_tile.cpp` 的 `StampTile`）——
      基准是**先跑完阶段 2 调度器再跑细化**的连贯产物，
      两段取数次数分开记账（阶段那一栏与 stage_water 基准逐例相同，互为交叉验证）；
      纠了旧文一处实质错误：大水面形状号是 `n / 40` 不是 `n / 10`
      （魔数 `0x66666667` 配 `sar edx,4`），于是 `shape ∈ [0,7]` 与小浪花的 `[8,13]`
      恰好拼满 WaterSet 的 `TilesInSet = 0x0E`——砖号分配自洽，反过来作证 `/40`；
      新记两条：`0x5A6C10` 会写 `CellClass +0x11C`（坡度，来自 `.tmp` 子块头 `+0x2A`）
      而贴砖器 `0x57B440` 从不碰它，且它的层高公式多一个 `-4`
- [x] **真实 `.tmp` 几何进基准**（`memory/data/rmg/tile_geometry.txt`，
      生成脚本 `code/randmap/dump_tile_geometry.py`）——
      岸砖 42 + 水砖 14、温和与雪地共 112 条，只取几何数值（占地宽高 / 空洞 /
      高度增量 / RampType），无任何像素资产。三份基准各加一档 `tiles=real` 用例，
      C++ 侧 `test/tile_geometry_fixture.h` 读同一份表，两边唯一真值来源。
      三条新结论：**水砖恰是前 8 块 2×2 + 后 6 块 1×1**，与 `0x59C630` 的
      `shape = n/40 ∈ [0,7]` / 小浪花 `[8,13]` 严丝合缝——给「除数是 40 不是 10」
      补了第二条独立证据；`shore41/42`（6×4 与 9×5，唯二带空洞的）
      **岸线平滑永远选不到**（组内下标最大 `0x28` → `shore40`）；
      温和与雪地这 56 块砖**几何完全相同**。合成剖面不删——真实数据的
      高度增量与 RampType 全是 0，压不出「层高累加」「空洞跳过」两条分支
- [x] **`TheaterData` 接进涂块/岸线两侧**——删掉 `WaterTile()` /
      `ShorePiecesBase()` 两个虚函数，去掉重复真值来源；
      顺带把格指针/哑元辅助抽成 `cell_ref.{h,cpp}` 供阶段调度器复用
- [x] **`MapType ∈ {3,4}` 支路（河流与湖泊）算法全还原** → [stage-water-maptype34.md](stage-water-maptype34.md)：
      调度 `0x59C580`（`WaterAmount > 20` 才挖河，河/湖各最多重试 10 次）、
      湖 `0x59C920`（陆地里涂水，优先级 `10*u + 0.5*dist − 0.02*ring`，
      靠 `Erode(0,2,−2)` 在每块已归属区域外围留出 2 格禁涂带）、
      河 `0x59D510`（递归生支流、逐步 `x+=cos / y−=sin` 画横截面、
      期望 200 步、河口再挖一个湖、没出瀑布的河 70% 概率被下切成峡谷）、
      以及三个公共区域算子 `CollectBorderCells 0x5A0700` / `Erode 0x5A0410` /
      `Dilate 0x5A0160`（后者兼作「有没有和第三方区域贴上」的隔离检测）。
      **顺手关掉一条老未确认**：`MapSeedClass +0x310`（Init random map 那个 25%
      抛硬币）唯一的用途是**决定这张图的河上有没有瀑布**
- [x] **瀑布 `0x59E740` 与角度扩张 `0x5A08D0` 全还原**（同上文档 §六 / §七）——
      瀑布十二步：净空扫描（不取数，所以「跳过」不扰动 RNG 流）→ 涂上游水面
      （**每格 2 次取数**：`PickBelow(6)` 选砖 + `PickBelow(4)` 选子格）→
      `AngularGrow(weight=0.003f)` 平出台地 → 岸线 → `Dilate` 2 圈 →
      整块 `Level += 4` → 涂下游水面（又是每格 2 次）→ 按 `[+0]/[+3]/[+2]/[+1]`
      的顺序贴四块瀑布砖 → **河道游标直接瞬移 12 格**（`dx={0,12,0,-12}`、
      `dy={-12,0,12,0}`）。角度扩张破掉了 `weight` 的作用：
      `n = _ftol(0.5·max(ln(边界格数), 1.0) / weight)`，再 `+ PickBelow(n/2+1)` 轮；
      那串 `1/(weight/L)` 的双重倒数在浮点上不抵消，必须照顺序算。
      **同时更正一条此前的误判**：`0x59E740` 曾被我记成「架桥」，
      实为瀑布——它取砖的唯一来源是四个 `Waterfall{North,East,South,West}`
      TileSet 全局，一处 `BridgeSet` 都没有；成因是拿调用点语境替了取证，
      教训记在该文 §八末尾
- [x] **一个真 bug 入账**：河流决定放瀑布的那道判据（`0x59DCCB`–`0x59DCF6`）写的是
      `|θ − θ₀| < π/4`，但作者用了**整数** `abs` 惯用法，差值先被 `_ftol`
      向零截断成整数才去比 `0.785…`——实际语义是 `< 1.0 弧度`，门槛放宽了 27%。
      按「bug 也要复刻」原样保留
- [x] **查表 sin/cos `0x4CACB0` / `0x4CAD00` 落成 C++ 并对拍**（`fast_math` + `sin_table.h`）——
      原版不调 `sin`/`cos`，查 `float[0x2800] @ 0x84F084`，角度量化到 1/8192 圈；
      表的来历已考证：`table[i] = chop_to_float(sin(i·2π/8192))`，10240 项里 **10238 项逐位相符**，
      唯二例外 `i=4096`/`i=8192` 存的是 ~1e-16 的 x87 `fsin` π 归约残渣——
      **这张表当年是用 x87 `fsin` 离线生成的**，指纹留在里面。
      `_ftol` 与折半都向零截断，于是 `f(−x) ≠ −f(x)`（专门写了用例钉死），
      河流每步都吃这份不对称
- [x] `.sed` 参数读取复刻（含西木 `Get_Int` 的 `$hex` / `Xh` 三条路径）
- [ ] `.map` 输出（IsoMapPack5 编码）
- [ ] 端到端验证：同种子生成结果与原版逐字节比对

## 验证方法论：模拟执行取真值

本模块建立了一套**对拍基准**做法（`code/randmap/emu_oracle.py`）：
用 Unicorn 指令级模拟器把 `gamemd.exe` 的节映射到原始基址 `0x400000`，
直接执行原版机器码取返回值，作为独立重写的标准答案。

- 属于对文件的**只读分析**，不修改、不重打包、不分发原版二进制
- 比"进程内映射后直接调用"可靠：64 位 Windows 上宿主进程堆在加载器阶段就已占据
  `0x400000-0x6C0000`（实测 `VirtualAlloc` 返回 `ERROR_INVALID_ADDRESS(487)`，
  连 `/ENTRY` 抢跑都来不及），而模拟器不受宿主地址空间限制
- 基准数据存档在 `memory/data/rmg/`，测试用例把关键数值内联为期望值
- 除了"整函数调用"，还支持**任意代码片段执行**（`Oracle.run_range`）：
  给定起止地址、初始寄存器与 FPU 控制字直接跑一段机器码。含 x87 的取数惯用法
  就是这样取到真值的——不必先把整个阶段的引擎状态都伪造出来

### ⚠️ 模拟器的已知失效面：x87 超越指令

**Unicorn 的 `fyl2x` 完全不理会 FPU 控制字**——`PC=53/RC=chop`、`PC=53/RC=near`、
`PC=64/RC=chop`、`PC=64/RC=near` 四种组合返回**完全相同**的值，实际算的是宿主的
long double。同类的 `fsin` / `fpatan` / `f2xm1` 是否也如此，**未确认**。

因此立下一条规矩：**凡结论依赖 x87 超越指令，必须去真 CPU 上取真值**
（`build/scratch/fyl2x_probe.cpp` 那样的只读原生探针）。
`+ - × ÷` / `fild` / `fstp` / `_ftol` 这些 IEEE 精确运算不受影响，模拟器可信。

更一般的做法：**验证 oracle 的最省事办法是改一个本该改变结果的输入**——
若结果纹丝不动，说明它压根没在模拟那个语义。详见 [fast-math.md](fast-math.md) §四。

### ⚠️ 「这段代码动不动随机数」必须做传递闭包

只搜 `call Randomizer::Random` 会漏掉包装层。岸线平滑的四个函数体内
一处直接调用都没有，差点被写成"平滑不消耗随机数"——实际隔了一层
`0x57ACF0 → 0x598030(PickRange) → Random`，而且它是本阶段消耗的**大头**。

补救办法是 `code/ghidra_scripts/rng_closure.py`：从根函数出发做调用树传递闭包，
并且**用 Ghidra 的真实函数边界（`Function.getBody()`）而不是线性反汇编猜边界**——
同一次扫描，猜边界得到"可达 2525 个函数"（跑飞了），真边界只有 93 个。
结果存档 `memory/data/decomp/rng_closure.txt`。

后来把这套做法又推翻了一次：**Ghidra 的调用图在 MapGen 这段并不完整**
（相当多的函数它根本没定义，图走到那儿就断），而且 `jmp` 跳转表与函数指针
两种边它都不给。最终形态是 `code/randmap/call_closure.py`——
从阶段入口做**指令级递归下降**，记录访问过的每一条指令地址，
解 switch 跳转表，人工补已实锤的函数指针边。三条经验：

1. 判"这段代码取不取随机数"要看 `this`，不是看调了哪个函数
2. 判"这个函数会不会被执行"要往上追引用，**零引用就是死代码**——
   150 个取数点里有 46 个根本不会跑
3. 判"哪个阶段用得到"要用指令级可达性，函数边界靠不住

## 工具箱（本模块新增，均为纯只读 PE 分析，不依赖 Ghidra）

| 脚本 | 作用 |
|------|------|
| `code/randmap/scan_source_macros.py` | 调试宏反推「源文件 ↔ 代码区间」，全 exe 33 个编译单元 |
| `code/randmap/scan_rng_callsites.py` | 扫出所有随机数消耗点并归属到函数/源文件 |
| `code/randmap/disasm.py` | capstone 直读 PE 按 VA 反汇编（含已知符号注释） |
| `code/randmap/xref_abs.py` | 按小端 4 字节搜绝对地址引用（全局变量/虚表项/跳转表项） |
| `code/randmap/xref_rel32.py` | 按字节搜 `E8/E9 rel32` 找**直接 call/jmp**——绝对值搜法对此完全失明 |
| `code/randmap/emu_oracle.py` | Unicorn 模拟执行取基准（整函数 / 任意片段）**——x87 超越指令不可信** |
| `code/randmap/dump_scaled_golden.py` | 导出浮点取数惯用法的对拍基准 |
| `code/randmap/dump_fastmath_golden.py` | 导出查表 sqrt / 均匀源 / 正态分布的对拍基准 |
| `code/randmap/dump_sincos_golden.py` | 导出查表 sin/cos 的对拍基准（纯 fmul+整数，Unicorn 在此可信） |
| `code/randmap/gen_sin_table.py` | 把 `0x84F084` 的 10240 项正弦表导出成 `sin_table.h`，并当场与 `sin(i·2π/8192)` 自检 |
| `code/randmap/dump_tile_geometry.py` | 从 theater 的 `.tmp` 取砖的**几何元数据**（宽高/空洞/高度/坡度）当对拍夹具 |
| `code/ghidra_scripts/find_writers.py` | 定位 .bss 全局的运行期初始化点（表内容不在文件里） |
| `code/randmap/scan_rmg_rng.py` | 只列**生成器自己那个** Randomizer(0xABE890) 的取数点（按 this 判定） |
| `code/ghidra_scripts/rng_closure.py` | 调用树传递闭包，找出某根函数下**所有**随机数取数点（真函数边界） |
| `code/randmap/dump_tileset_globals.py` | 把 theater 的 45 个 TileSet `.bss` 全局对回 ini 键名（栈槽两段拼接） |
| `code/randmap/read_const.py` | 按 VA 直读 PE 里的 double/float/dword/字节串常量 |
| `code/randmap/emu_oracle.py` `run_range` | 也用来**跑 CRT 静态初始化器**，把 `.bss` 里「文件中不存在」的表 dump 出来 |
| `code/randmap/call_closure.py` | **指令级**递归下降可达性：把取数点归到阶段、并判出死代码（解 switch 跳转表） |

## 复刻代码的可移植性立场

**产品代码不含任何架构相关指令**，x86 / ARM / 龙芯上必须逐位相同。这一条是硬性要求：
CI 跑在哪种机器上都得给出同一张地图。为此定了三条规矩——

1. **超越函数不用本机指令。** 原版用 x87 `fyl2x` 求 `ln`，但实测证明 `fyl2x` 本身
   **不是正确舍入的**（真 CPU 20000 例中 7 例比精确截断低 1 ulp）。跟着本机指令走
   等于把输出绑死在具体 CPU 实现上。改为定义唯一确定的数学语义
   （「精确 ln 向零截断」，双双精度实现），再**证明**残余差异被下游吸收。
   全过程见 [fast-math.md](fast-math.md) §四。
2. **常量表写死。** 16384 项 sqrt 表与 10240 项 sin 表硬编码，让"结果与架构无关"
   在编译期就成立；同时各配一条自检证明它是数学常量而非搬运的二进制资产——
   sqrt 表用纯整数运算重算全表；sin 表因为绕不开 `std::sin`（它的最后 1 bit 逐平台不同），
   改成「数值层 1 float ulp + 结构层反对称/对称/周期性逐位精确」两层校验。
3. **浮点语义显式加固。** `fp_guard.h` 用 `static_assert` 挡住 x87 超精度、
   有符号右移歧义，用编译选项挡住 FMA 收缩与 `-ffast-math`，
   再用一条运行期自检确认 `fesetround` 真的生效。

`build32/`（`vcvars32` + NMake）保留下来的作用是**取证平台**：只有在 x86 上才能
把复刻结果与真 x87 指令直接对撞，从而给出"我们与原版差多少、差在哪里"的量化答案。
`+ - × ÷` 不需要这么麻烦：SSE2 与 x87-PC53 在同一 RC 下结果相同。
