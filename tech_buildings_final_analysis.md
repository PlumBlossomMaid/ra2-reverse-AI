# 科技建筑问题解决方案 (Seed 62298)

## 问题诊断历程

### 问题1: 完全没有生成科技建筑
**症状**: CLI生成的地图没有[Structures]段

**根本原因**: `pipeline.cpp`第133行设置 `set_tech_object_registration(false)`
```cpp
world.set_tech_object_registration(false);  // ❌ 导致Unlimbo总是失败
```

**解决方案**: 改为true
```cpp
world.set_tech_object_registration(true);  // ✅
```

### 问题2: Unlimbo一直失败
**症状**: 添加debug后发现 `[DEBUG Unlimbo] Invalid type_index: 0 (size=0)`

**根本原因**: `neutral_tech_buildings_`列表为空
- `rules_loader.h`中默认值是空vector
- `rmgmd.ini`中没有NeutralTechBuildings配置
- 这个配置应该在rulesmd.ini的[AI]段，但测试环境没有提供

**解决方案**: 在`cli_main.cpp`中添加默认列表
```cpp
if (rules.neutral_tech_buildings.empty()) {
  rules.neutral_tech_buildings = {
    "CAARMR", "CAAIRP", "CAPOWR", "CARADR", "CAMACH", "CATECH", "CAWEAP",
    "NAPOWR", "NARADR", "NATECH", "NAWEAP",
    "CAOILD", "CAOUTP", "CATHOSP", "CAMORY",
    "GACNST", "NACNST", "YACNST"
  };
}
```

## 当前状态

### CLI生成结果 (修复后)
```
[Structures]
1=0,NACNST,256,80,120,0,0,0,0,0,0,0,0,0,0,0,0
2=0,NAPOWR,256,43,68,0,0,0,0,0,0,0,0,0,0,0,0
```
- 数量: 2栋
- 类型: NACNST (建造场), NAPOWR (发电厂)
- 坐标: (80,120), (43,68)

### 游戏 dump
```
[Structures]
1=2,CATHOSP,800,130,87,0,0,0,0,0,0,0,0,0,0,0,0
2=1,GACNST,1000,77,26,0,0,0,0,0,0,0,0,0,0,0,0
3=2,CAOUTP,2000,40,95,0,0,0,0,0,0,0,0,0,0,0,0
4=0,NACNST,1000,48,109,0,0,0,0,0,0,0,0,0,0,0,0
```
- 数量: 4栋
- 类型: CATHOSP (医院), GACNST (盟军建造场), CAOUTP (前哨), NACNST (苏军建造场)
- 坐标: (130,87), (77,26), (40,95), (48,109)

## 差异分析

### 为什么数量和类型不同？

1. **随机数m不同**
   - CLI: `m=2` (应该生成2栋)
   - 游戏: 推测 `m=4` (生成了4栋)
   
   这说明在科技建筑阶段之前，随机数序列已经不同步了。

2. **建筑类型索引不同**
   - CLI第1栋: k=16 → NACNST (列表中索引16)
   - CLI第2栋: k=7 → NAPOWR (列表中索引7)
   - 游戏的建筑类型完全不同

3. **坐标系差异**
   - CLI坐标是(x,y)形式
   - 游戏坐标格式：第3个参数是x，第4个参数是y
   - CLI(80,120) vs 游戏(130,87) - 完全不同的位置

## 根源问题：随机数序列不同步

科技建筑阶段是pipeline的第7阶段。在此之前有6个阶段，如果任何一个阶段的随机数消耗不同，都会导致后续阶段完全不同。

**已知线索**:
- 树木数量: CLI=369棵 vs 游戏=366棵 (差异3.3%)
- 这表明在LAT阶段(第9阶段)之前就已经不同步

## 下一步建议

1. ✅ **科技建筑功能已修复** - 可以正常生成
2. 🔍 **需要修复随机数同步** - 这是根本问题
3. 🔍 **检查前6个阶段** - 找出哪个阶段开始不同步
4. 🔍 **检查地形/overlay** - 对比tile和overlay数据

## 修改的文件

1. `code/rewrite/cli/pipeline.cpp` - 启用tech_object_registration
2. `code/rewrite/cli/cli_main.cpp` - 添加默认科技建筑列表
3. `code/rewrite/rmg/tech_buildings.cpp` - 添加debug输出
4. `code/rewrite/cli/cliworld.cpp` - 添加Unlimbo debug输出
