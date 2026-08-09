# River() 调试进度报告

## 当前状态（2026-08-09）

### 问题描述
River() 实现与 Oracle 的 RNG 消耗模式不匹配：
- **我的实现**: 消耗 45 RNG，在第 10 步退出
- **Oracle**: 消耗 161 RNG，运行约 34+ 次迭代
- **测试用例**: `r-rnd-basic` (20×20, water=0, level=4, ret=0)

### 已确认正确的部分
✅ Randomizer 实现完全正确（RNG[0-9] 与 Oracle 逐位匹配）
✅ NormalDeviate 缓存逻辑正确（rng=2/0/2/0/4 模式与 Oracle 一致）
✅ 常量定义正确（kInvUintMax = 2.3283064370807974e-10）
✅ 循环退出条件正确（`while (u >= 0.005)`）
✅ FPU 模式设置正确（FpuChopScope）

### RNG 消耗追踪

#### 我的实现（前 50 RNG）
```
RNG[0-2]:   PickBelow (dir, rx, ry)
RNG[3-4]:   初始角度采样（nd() 一次成功）
RNG[5]:     width = PickRange(1, wmax)
RNG[6]:     fallThreshold = PickRange(35, 125)
RNG[7]:     step 0 分支检查
RNG[8-9]:   step 0 width_walk (nd() 消耗 2 RNG)
RNG[10]:    step 1 分支检查
RNG[11-12]: step 1 width_walk (nd() 使用缓存，消耗 0 RNG)
...
RNG[44]:    step 10 退出检查 → u=0.0035602 < 0.005 触发退出
```

#### Oracle（前 50 RNG 返回地址统计）
```
0x59800D (UniformUnit):  22 次调用
0x59DE49:                12 次调用
0x59E16D:                11 次调用
0x59D5C4, 0x59D60C, 0x59D659, 0x59D890, 0x59D901: 各 1 次
```

88 次 UniformUnit 调用 = 44 次 nd() 生成（含缓存共 88 次使用）

### 关键观察

1. **我的 step 10 退出值**:
   - RNG[44] = 0xE9E4DD4 = 15291028
   - u = 15291028 * 2.3283064370807974e-10 = 0.0035602
   - 0.0035602 < 0.005 → 触发退出

2. **Oracle 不在此位置退出**:
   - Oracle 继续运行到 161 RNG
   - 说明 Oracle 在 RNG[44] 之前的消耗模式已经不同
   - 导致 Oracle 的 RNG[44] 不是这个"幸运"的退出值

3. **nd() 调用模式差异**:
   - 我的实现：初始角度采样后，大部分 nd() 使用缓存（rng=0）
   - Oracle：88 UniformUnit 调用说明有更多 nd() 触发了新生成
   - 可能原因：截断正态分布的拒绝采样在不同位置失败

### 下一步调试计划

#### 方案 A：精确追踪 RNG 分歧点
1. 扩展 Oracle hook，追踪完整 161 RNG 序列
2. 映射每个 RNG 调用到返回地址和调用上下文
3. 识别第一个与我的实现不同的 RNG 消耗点
4. 检查该点的 nd() 拒绝采样条件

#### 方案 B：对比主循环迭代数据
1. 让 Oracle 记录每次迭代的 (x, y, ang, width, step)
2. 对比我的实现在相同迭代的值
3. 找到第一个分歧的迭代
4. 检查该迭代的 angle_walk/width_walk 计算

#### 方案 C：Ghidra 重新逆向（后备方案）
如果以上方案无法定位问题，用 Ghidra 重新逆向 0x59D510：
1. 确认主循环的精确结构
2. 检查是否有遗漏的 RNG 调用点
3. 验证 nd() 拒绝采样的条件逻辑

### 代码位置

**主要文件**:
- `code/rewrite/rmg/rivers.cpp`: River() 实现（第 50-431 行）
- `code/rewrite/rmg/test/test_rivers.cpp`: 测试套件
- `code/randmap/dump_river_golden.py`: Oracle 调用脚本
- `memory/data/rmg/river_golden.txt`: 黄金数据

**关键函数**:
- `River()` @ 0x59D510
- `UniformUnit()` @ 0x59800D
- `NormalDeviate()` @ 0x5980C0
- `Random()` @ 0x65C780

### 测试命令

```bash
# 编译测试
cd E:/code/ra2-reverse-AI/code/rewrite/build
cmake --build . --target test_rivers

# 运行测试
cd rmg
./test_rivers.exe --gtest_filter="River.MatchesOriginal"

# Oracle 追踪
cd E:/code/ra2-reverse-AI/code/randmap
python dump_river_golden.py  # 重新生成 golden data
```

### 相关文档
- `docs/random-map/§四-河流与湖泊.md`: 河流算法文档
- `docs/random-map/fast-math.md`: 快速数学函数文档
- `code/rewrite/rmg/randomizer.h`: RNG 内存布局（+0x04/+0x08 索引）

---

**最后更新**: 2026-08-09  
**状态**: WIP - 等待 token 恢复后继续调试  
**提交**: commit 14257ae "WIP: River() debugging - RNG divergence at 45 vs 161 calls"
