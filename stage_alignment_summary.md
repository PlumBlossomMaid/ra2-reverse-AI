# Stage 对齐情况分析 (Seed 62298)

## 完全对齐的阶段 ✓

1. ✓ seeding_water - 水域生成
2. ✓ refine_water - 水域细化
3. ✓ init_regions - 初始化区域
4. ✓ making_regions - 创建区域
5. ✓ spread_green_raw - 绿地扩散（原始）
6. ✓ spread_green_recalc - 绿地扩散（重算）
7. ✓ tech_buildings - 科技建筑

## 不对齐的阶段 ✗

**高度相关阶段（Stage 4之后的高度/地形处理）**:
- ✗ seed_probabilities
- ✗ generate_height_field
- ✗ mark_height_anchors
- ✗ raise_all_cells
- ✗ build_corner_lattice
- ✗ apply_lattice
- ✗ flatten_ramp_blocks

**LAT阶段（树木/岩石/装饰）**:
- ✗ lat_setup
- ✗ lat_pick_sizes
- ✗ lat_main_blocks
- ✗ lat_trees
- ✗ lat_rocks
- ✗ lats
- ✗ clear_lat_queued
- ✗ lats_recalc

**矿场阶段**:
- ✗ tiberium
- ✗ tiberium_recalc

## 关键发现

### 第一个分歧点：seed_probabilities

在 spread_green_recalc 之后，下一个阶段就是 seed_probabilities，这是第一个出现差异的地方。

**Pipeline 顺序**:
```
Stage 1: seeding_water        ✓ 对齐
Stage 2: refine_water          ✓ 对齐
Stage 3: init_regions          ✓ 对齐
Stage 4: making_regions        ✓ 对齐
Stage 5: spread_green_raw      ✓ 对齐
Stage 5: spread_green_recalc   ✓ 对齐
Stage 6: seed_probabilities    ✗ 第一个不对齐！
Stage 6: generate_height_field ✗ 
Stage 6: mark_height_anchors   ✗
...
```

### 问题定位

**seed_probabilities** 是起始点生成阶段的一部分（Stage 6: Creating starting points）

这个阶段包含：
1. seed_probabilities - 为每个区域计算起始点概率
2. 后续的高度场生成也在这个阶段

## 下一步行动

需要检查 `start_points.cpp` 中的 seed_probabilities 相关代码，找出为什么会出现差异。
