# gamemd.exe frontier 登记终态完成度报告

日期：2026-11-10
口径：`memory/data/callgraph/ranking.tsv`（8655 函数）vs
`memory/data/callgraph/frontier_status.tsv`（8782 行 / 唯一 entry 8671）

## 总量与覆盖

| 维度 | 数量 | 已登记 |
|------|------|--------|
| ranking.tsv 总函数 | 8655 | 8655（100%） |
| uc>0（带扇入） | 5949 | 5949（100%） |
| uc=0（零扇入叶子） | 2706 | 2706（100%） |
| **未登记池** | — | **0（清零）** |

- TSV 行数 8782 > 唯一 entry 8671 > ranking 8655：差异来源为 `_REIMPL` 后缀条目
  （如 `0x005A7440_REIMPL`/`0x005A7640_REIMPL`，与纯地址条目归一化后合并）及
  ranking 外的补充登记条目。
- 心跳批次覆盖：batch 0 → **3294**，共 2837 个批次号（3247–3294 为最后 48 个
  零扇入清零批次，2026-11-10 单日完成）。

## reconstruction_status 分布（TSV 8782 行）

| 状态 | 行数 | 说明 |
|------|------|------|
| semantics_verified | 4436 | 语义完全实锤 |
| partial | 2463 | 证据不足保留 partial（含心跳 48 批全部登记） |
| crt_runtime_verified | 351 | CRT 运行时验证（golden 对拍） |
| cpp_reimplementation_verified | 63+4 | C++ 重写已验证（含前导空格变体 4 行） |
| semantics_verified_partial | 69 | 语义部分实锤 |
| cpp_reimplementation_partial | 27 | C++ 重写部分验证 |
| stub_verified | 3 | 桩验证 |
| crt_runtime_semantics_verified | 4 | CRT 双态 |
| crt_runtime_verified_partial | 4 | CRT 部分 |
| semantics_partial | 5 | 语义部分 |
| no_function | 2 | 非函数条目 |

注：另有约 30 行 reconstruction_status 列被超长 evidence 文本占据（早期
batch 900+ 系列 CRT 行的列书写格式），entry 列完好、不影响覆盖判定；
如需清洗属独立数据治理任务，本报告不改写原行。

## verification_status 分布（主要值）

| 状态 | 行数 |
|------|------|
| assembly_decompile_verified | 5004 |
| assembly_verified_semantics_confirmed | 2334 |
| unit_test_verified | 80 |
| ghidra_export_verified | 2 |
| assembly_verified | 1 |
| emulation_golden_verified 等（截断尾段） | 其余 |

## 三态纪律执行情况

- 心跳 48 批（3247–3294，512 函数）全部 `partial` + 
  `assembly_verified_semantics_confirmed|assembly_decompile_verified`，
  无越级断言；
- 每批被调函数全量核验（缺失即补探针重跑），最终批次（3292/3293）单批被调
  130+ 全部在池——证明扇入排序已覆盖全部被依赖原语；
- YRpp 符号命中（最近批次）：ParasiteClass::UpdateSquid、
  ShipLocomotionClass 构造器、SidebarClass::Scroll/Draw、
  FactoryClass::AddButtons、MPGameModeClass×5、
  ObjectClass::SpawnParachuted、RadSiteClass::DecreaseRadiation、
  Matrix3D::RotateX/RotateY/Translate/LookAt2 等。

## 跨批模板族清单（登记中沉淀的结构性发现）

- 全局对象池登记模板（满则拒→虚表+8→长度++槽写入）：11 例实例
- 链式哈希容器族：ctor/grow/shrink×2/delete×3/批量重建 ≥ 10 函数
- 窗口置窗模板：7 变体；控件 ID 判定分发族：≥ 8 实例
- 容量倍增器：elem={3B,4B,8B,20B,24B,0x104B} 六种尺寸
- x87 矩阵族（0x4cad00/0x4cacb0/0x4cac40/0x4cae30/0x4cade0）：RotateX/Y、
  Translate、LookAt2、旋转矩阵 v2 等全真身核对
- 会话基础设施（0x4eXXXX 索引器族 + 0xa8b2xx 全局 + 0xac11xx 八指针）完整覆盖

## 终点说明

自 batch 3247 起的「未登记池取候选 → 反汇编 → 登记」心跳工作流于
batch 3294 封顶（最大零扇入叶子 0x5A9C40，9537B），未登记池归零。
心跳 cron（bg8v9sfm）已按 loop 卫生纪律关闭。

后续可选方向（未启动，待拍板）：
1. partial 条目证据升级（Ghidra 反编译复核 → verification 提升）
2. 子域重写线（寻路 0x56C510/0x586990、LogicClass::Update 等，转入
   C++ 重写 + 对拍闭环）
3. TSV 数据治理（列错位行清洗、_REIMPL 条目合并口径文档化）

本报告为正式产物，随 batch 3294 后提交入库。
