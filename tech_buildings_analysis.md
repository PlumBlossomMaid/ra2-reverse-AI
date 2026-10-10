# 科技建筑对比分析 (Seed 62298)

## 发现问题

### 游戏 dump 中的科技建筑
```
[Structures]
1=2,CATHOSP,800,130,87,0,0,0,0,0,0,0,0,0,0,0,0    # Hospital (Allied)
2=1,GACNST,1000,77,26,0,0,0,0,0,0,0,0,0,0,0,0    # Construction Yard (Allied)
3=2,CAOUTP,2000,40,95,0,0,0,0,0,0,0,0,0,0,0,0    # Outpost (Allied)
4=0,NACNST,1000,48,109,0,0,0,0,0,0,0,0,0,0,0,0   # Construction Yard (Soviet)
```

**游戏生成了 4 个科技建筑**

### CLI 生成结果
```
[Terrain] 段有 1668 个对象
但是完全没有 [Structures] 段！
```

**CLI 没有生成任何科技建筑！**

## 需要检查

1. 科技建筑生成代码是否被调用？
2. 科技建筑生成的条件判断是否正确？
3. 是否有地图类型/参数相关的限制？

## 下一步

检查 `tech_buildings.cpp` 中的代码逻辑

## 根本原因定位

### MapType 值
- Seed 62298 的 MapType = **3**
- 代码第 12 行：`if (ctx.map_type == 2)` - 只对 MapType=2 生成科技建筑（分支A）
- MapType=3 会走分支B，但分支B也会生成建筑！

### 分支B逻辑（MapType != 2）
```cpp
// 第 36 行：随机决定生成多少栋（0-4栋）
int m = PickBelow(*ctx.rng, 5);
if (m <= 0) return;  // 如果 m=0，不生成任何建筑
```

**问题：需要确认这次随机数抽取的结果是否为 0**

### 验证方法
添加 debug 输出查看：
1. AddTechBuildings 是否被调用
2. map_type 的值
3. PickBelow(*ctx.rng, 5) 返回的 m 值
4. 如果 m > 0，为什么没有成功放置建筑

## 游戏 dump 建筑列表
```
130,87  - CATHOSP (Hospital)
77,26   - GACNST (Construction Yard) 
40,95   - CAOUTP (Outpost)
48,109  - NACNST (Construction Yard)
```
坐标范围在 26-109，说明这些是真实坐标（地图是 74x82）
