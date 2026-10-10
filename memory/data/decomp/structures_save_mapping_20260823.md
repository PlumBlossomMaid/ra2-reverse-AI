# Structures 序列化取证（2026-08-23 存档现场补充）

## 本轮存档现场

当前 `gamemd.exe` PID=`2084`，只读提取目录：
`snapshot_gamemd_62298/live_save_20260823/`

- 地图非空 Cell：`12054`
- 唯一对象：`394`
- BuildingClass 唯一对象：`3`
- MapClass：`Size` 对应宽高 `74×82`，`LocalSize=70×70`，`Local=(2,5)`
- 当前地图 Cell 快照 SHA256：`161d2ec3ff6efba40d9b8ab9f5ff0e3a5bac42423a9c843976b1597c98e69607`

唯一建筑：

| 对象 | 类型 | Owner ArrayIndex | Health | Location |
|---|---|---:|---:|---|
| `0x190144F8` | `GACNST` | 1 | 1000 | `(19840,6784,832)` |
| `0x18AB0488` | `CATHOSP` | 2 | 800 | `(33408,22400,832)` |
| `0x18AAFD40` | `CAOUTP` | 2 | 2000 | `(10368,24448,416)` |

对应占地锚点由 CellClass 反查得到：

- `GACNST` 占地 `(77..80,26..29)`，4×4
- `CATHOSP` 占地 `(130..135,87..90)`，6×4
- `CAOUTP` 占地 `(40..43,95..97)`，4×3

## Owner 结论更新

运行态 `TechnoClass +0x21C` 确认为 `HouseClass*`，并由 `HouseClass +0x30` 得到 ArrayIndex。当前存档实测：

- `GACNST -> Americans -> ArrayIndex 1`
- `CATHOSP -> Neutral -> ArrayIndex 2`
- `CAOUTP -> Neutral -> ArrayIndex 2`
- Units 现场也能得到 Arabs=0、Americans=1。

原版 Structures 序列化并不是直接读取 `HouseClass +0x15FF4` 的固定偏移数值；汇编 `0x44FF7C` 是 `add ebx, 0x15FF4`，其中 `ebx=HouseClass*`，所以目标地址是 `HouseClass + 0x15FF4`。当前 HouseClass 总体内存快照中 `PlainName` 实际位于该绝对偏移；脚本的完整 House 读取已修正，但当前对象的该字段为空，原因是该现场使用对象/国家名回退路径，`houses.txt` 中类型 ID 仍可靠。

## `FUN_0044FEC0` 直接字段证据

格式串 `0x8190E4`：

```text
%s,%s,%d,%d,%d,%d,%s,%d,%d,%d,%d,%d
```

从 `0x44FF33-0x44FF82` 的逆序压栈恢复：

- `%s` type：`BuildingClass +0x520 -> BuildingType +0x24`
- `%s` owner：`HouseClass +0x21C -> +0x15FF4`
- 位置/方向相关局部值：由对象 vtable `+0x1B8` 获取 `CellStruct`，随后调用 `FUN_005F5C60`
- `+0x6DC`、`+0x6CA`、`+0x660`：分别以字节读取后压栈
- `+0x702`：在第一组位置参数前以有符号字节读取

关键现场对象字段：

- `GACNST`: `+0x6DC=1`, `+0x6CA=0x101`, `+0x6CB=1`, `+0x660=1`, `+0x702=0xFF00`
- `CATHOSP`: `+0x6DC=0x101`, `+0x6CA=0`, `+0x6CB=0`, `+0x660=1`, `+0x702=0xFF00`
- `CAOUTP`: `+0x6DC=0x100`, `+0x6CA=0`, `+0x6CB=0`, `+0x660=1`, `+0x702=0xFF00`

这些字段显示对象实际布局中的相邻 bool/byte 不能按 32-bit 原始窗口直接解释：例如 `+0x6C9/+0x6CA/+0x6CB` 应拆成单字节，`+0x702` 也应读取 `*(signed char*)`，否则会误判为 `0xFF00`。

## 位置与方向的重要修正

`FUN_005F5C60 @ 0x5F5C60` 实际反汇编为：

```asm
fild dword ptr [ecx + 0x6c]
call dword ptr [vtable + 0x88]
fild dword ptr [eax + 0xa0]
fdivr qword ptr [esp]
```

它计算的是 `Health / Strength` 的浮点比例，不是方向函数。此前将 Structures 的方向/坐标参数描述为由 `FUN_005F5C60` 产生不准确，已在本补充中纠正：该函数明确是健康百分比转换链的一部分。

`0x44FEC0` 中：

- `+0x388` 的临时结构调用 `FUN_004C93D0`，返回当前帧修正后的 Facing/Dir 相关结构；
- vtable `+0x1B8` 两次读取该结构中的 `short`；
- 第二个值参与 `FUN_005F5C60` 调用前的栈参数；
- 后续 `fmul [0x7E1710]`、`fadd [0x7E1738]`、`_ftol @0x7C5F00` 形成写出整数。

因此，当前仍不能把 `FacingStruct.Value` 原始 16 位值直接写成 `.map dir`；必须把 `+0x388` 返回结构和 `vtable +0x1B8` 的具体返回对象继续拆解。

## BuildingType 字段

`BuildingType +0xEBC` 位置结构由 `FUN_004500A0` 使用：

```asm
call [BuildingClass vtable + 0x48]   ; 返回结构位置
mov ecx, [BuildingType + 0xEBC]
add ecx, x
...
```

当前三种建筑的 `BuildingType +0xEBC..+0xF0C` 已采集到 `unique_buildings.txt`。其中：

- `+0xEF0`：GACNST=`0x12`、CATHOSP=`0x14`、CAOUTP=`0x0C`
- `+0xEF4`：分别 `4`、`7`、`6`
- `+0xEF8`：均为 `3`
- `+0xF08`：分别 `0x1D`、`4`、`1`
- `+0xF0C`：分别 `1`、`0x0D`、`0`

这些值与 foundation/建筑类型的附加布局相关，但尚未足够证明 `.map Structures` 的方向字段映射。

## 当前有效产物

- `snapshot_gamemd_62298/live_save_20260823/game_cells.bin`
- `snapshot_gamemd_62298/live_save_20260823/object_mapping.txt`
- `snapshot_gamemd_62298/live_save_20260823/cell_objects.txt`
- `snapshot_gamemd_62298/live_save_20260823/building_objects.txt`
- `snapshot_gamemd_62298/live_save_20260823/unique_buildings.txt`
- `snapshot_gamemd_62298/live_save_20260823/techno_mapping.txt`
- `snapshot_gamemd_62298/live_save_20260823/houses.txt`
- `snapshot_gamemd_62298/live_save_20260823/rmg_state.json`
