# Structures 序列化取证（2026-08-21）

## 入口和格式

`FUN_0044FE60` 遍历 `BuildingClass::Array @ 0xA8EB40`，跳过 `+0x81 != 0` 或 `BuildingType + 0x16BF != 0` 的对象，然后调用 `FUN_0044FEC0`。

`FUN_0044FEC0` 最终调用 `INISection::WriteString @ 0x528660`，格式串 `0x8190E4` 的原始内容为：

```text
%s,%s,%d,%d,%d,%d,%s,%d,%d,%d,%d,%d
```

对应 `.map` Structures 的字段 1..12；外层键 `1=`, `2=` 由调用者的对象数组索引写出。EA 编辑器和 CLI 使用的 17 字段表在该 12 字段之后追加 5 个零字段。

## 已确认参数来源（按格式占位符）

从 `0x44FF3A` 起的逆序压栈为：

1. `%s`：`[building + 0x520] + 0x24`，BuildingType ID，例如 `CAOILD`。
2. `%s`：`[building + 0x21C] + 0x15FF4`，HouseClass 的 PlainName / owner 字符串基址；空 owner 使用 `None`。
3. `%d`：`GetMapCoords` 返回的 `CellStruct` 第一坐标，经 `FUN_005F5C60` 后的 health 百分比换算结果。`FUN_005F5C60` 为 `Health / GetType()->Strength`，随后乘常量、加常量、`_ftol` 转成整数；运行样本和 `.map` 的 `256` 需要继续用同一 BuildingClass 对拍确认。
4. `%d`：`[building + 0x6DC]`（`mov dl,[esi+0x6DC]`）。
5. `%d`：`[building + 0x6CA]`（`mov cl,[esi+0x6CA]`）。
6. `%d`：`[building + 0x660]`（`mov dl,[esi+0x660]`）。
7. `%d`：`GetMapCoords` 返回值第二次调用返回结构的 `short [result+2]`，再与下一次返回结构的 `short [result]` 作为参数调用 `FUN_005F5C60`；这是方向/坐标相关值，不能把 TechnoClass 的 `FacingStruct` 原始 16 位值直接当 `.map dir`。
8. `%s`：`[building + 0x5EC]` 的字符串内容；空值使用 `None`。该字段由一个 3 次循环写入辅助缓冲区，可能是升级/附加结构字段。
9-12. `%d`：由 `[building + 0x6CB]`、`[building + 0x6C9]` 转成 bool 的两个值，以及前述局部/辅助缓冲区内容参与 `WriteString`；需要继续按 `FUN_00528660` 的栈帧和目标 `INISection` 结构拆解，确认四个 `%d` 的准确字段顺序。

注意：汇编中 `push` 的实际顺序是格式参数逆序；必须以栈位置重建，不能按出现顺序直接映射。

## Owner 结论

运行态只读采集已确认：`TechnoClass +0x21C` 是 `HouseClass*`，反查 HouseClass::Array 的 `+0x30` 得到 ArrayIndex：

- Arabs `0x0AA84E18` -> `0`
- Americans `0x0AB8DD38` -> `1`
- Neutral `0x18E80DE0` -> `2`
- Special `0x0AFC2458` -> `3`

但 Structures 序列化使用的是 `HouseClass +0x15FF4` 的 `PlainName` 字符串，不是直接把 ArrayIndex 写入 `%s`。`.map` 中 owner 数字/House 名称的最终映射仍需通过真实 WriteString 输出核对；不能提前改 CLI 的 `BuildingRecord.owner`。

## FacingStruct

YRpp `FacingStruct` 布局为 0x18：`Value +0x0`、`Initial +0x4`、`Timer.StartTime +0x8`、Timer padding `+0xC`、`Timer.TimeLeft +0x10`、`ROT +0x14`。TechnoClass 偏移：Barrel `0x3A0`、Facing `0x3B8`、Turret `0x3D0`。正式脚本 `code/randmap/dump_live_techno_mapping.py` 已按该布局输出 target/initial/timer/rot，并扩大读取窗口到 `0x740`。

代表对象采集结果已保存到 `snapshot_gamemd_62298/live_oracle_20260821_a/techno_mapping_facing_final.txt`。其中 MTNK Barrel target=`19119`、rot=`1280`；这些是 16-bit DirStruct 原值，尚不能直接等同 Structures 的 `%d` 方向字段。
