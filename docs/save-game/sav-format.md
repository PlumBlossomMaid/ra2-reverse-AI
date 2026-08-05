# YR 存档格式逆向（.sav）

对应原版 `gamemd.exe` 的存档系统——`ScenarioClass::SaveGame/LoadGame` +
`SavegameInformation` 元数据。本次逆向回答两个社区长期困惑：

1. **为什么 mod1 保存的对局用 mod2 打开会"鬼畜"**
2. **逆天存档（如 V3 发射核弹）是怎么做出来的**

## 一句话结论

`.sav` 是 **OLE 复合文档（CFB / IStorage）**，与 .doc/.xls 同族。
内部含 `CONTENTS` 主数据流（全部游戏状态序列化）和 13 个元数据流。
**外壳层（CFB + 元数据）跨 mod 完全兼容**——跨 mod 鬼畜与逆天存档都发生在
`CONTENTS` 内部的**对象序列化数据**上。

## 文件格式：OLE CFB 复合文档

所有存档文件头 8 字节均为 `D0 CF 11 E0 A1 B1 1A E1`（CFB 魔数）。

| 字段 | 值 | 含义 |
|---|---|---|
| 魔数 | `D0 CF 11 E0 A1 B1 1A E1` | OLE CFB |
| Major version | `3E 00` (62) | CFB v3 |
| Byte order | `FE FF` | little-endian |
| Sector shift | `09` | 扇区 512 字节 |
| Mini sector shift | `06` | 迷你扇区 64 字节 |

## 内部结构（olefile 实证，三个存档一致）

```
.sav (OLE CFB)
├── SummaryInformation   (OLE 标准属性流, 364-384 B, PIDSI_* 属性)
├── CONTENTS             (主数据流! 1.1 - 2.6 MB, 全部游戏状态)
├── Internal Version     (4 B = 0x2B898)
├── Version              (4 B = 1)
├── Campaign             (4 B: 1=战役序号 / -1=无战役)
├── Scenario Number      (4 B)
├── GameType             (4 B: 0=战役, 5=联机/遭遇战)
├── Scenario Description (UTF-16LE 关卡名, 如 "[苏] 05 - …")
├── Player House         (UTF-16LE 阵营名, 如 "苏军")
├── Player Name / Player Name2 (UTF-16LE)
├── Executable Name      (UTF-16LE "SUN.EXE")
├── Start Time / Play Time / Last Save Time (FILETIME 8 B)
```

## SavegameInformation（元数据读写 @ 0x6812E0 / 0x681840）

`SavegameInformation::Write` 反编译确认：通过 **IPropertySetStorage**
逐个写入 13 个属性（PIDSI_*），字段与 stream 一一对应。

| 属性 ID | 字段 | 类型 |
|---|---|---|
| 0x10 (PIDSI_CHARCOUNT) | InternalVersion | int（= 0x2B898） |
| 9 (PIDSI_REVNUMBER) | Version | int（= 1） |
| 2 (PIDSI_TITLE) | ScenarioDescription | UTF-16LE string |
| 3 (PIDSI_SUBJECT) | PlayerHouse | UTF-16LE string |
| 100 | ScenarioNumber | int |
| 0x65 | Campaign | int |
| 0x66 | GameType | int |
| 4 / 8 (PIDSI_AUTHOR/LASTAUTHOR) | PlayerName / PlayerName2 | UTF-16LE string |
| 0x12 (PIDSI_APPNAME) | ExecutableName | UTF-16LE string |
| 0xC (PIDSI_CREATE_DTM) | StartTime | FILETIME |
| 0xD (PIDSI_LASTSAVE_DTM) | LastSaveTime | FILETIME |
| 0xA (PIDSI_EDITTIME) | PlayTime | FILETIME |

- 结构定义：YRpp `LoadOptionsClass.h` `class SavegameInformation`
- 魔数：`Game::Savegame_Magic @ 0x83D560` = **0x0002B898**（写入 Internal Version）
- Phobos 兼容标记：保存时 `InternalVersion += SAVEGAME_ID`，加载时减回
  （Phobos `GameSave_SavegameInformation @ 0x67D04E`）

## 元数据实证（三个存档对比）

| 字段 | SAVE79C7.SAV (2002) | SOV05UMD.sav (2002) | SAVE4AFC.SAV (2022) |
|---|---|---|---|
| Internal Version | 0x2B898 | 0x2B898 | 0x2B898 |
| Version | 1 | 1 | 1 |
| GameType | 0（战役） | 0（战役） | **5（联机/遭遇战）** |
| Campaign | 1 | 1 | **-1（无战役）** |
| Scenario Number | 5 | 5 | 1 |
| Player House | "苏军" | "苏军" | （另一阵营） |

→ **外壳层 2002 原版与 2022 mod 完全兼容**，跨 mod 差异必在 CONTENTS 内部。

## CONTENTS 主数据流（初步）

头部含可读地图名（`GDI2A.map`、`GDI9C.MAP`）+ 结构化头部 + 对象序列化数据。
游戏状态通过 `AbstractClass::Save/Load` 虚函数逐对象序列化，指针用
**SwizzleManager** 编号重映射（Phobos `LoadGame_PostSwizzle_Phobos @ 0x67E685`：
"SwizzleManagerClass::Process has remapped every registered pointer"）。

- 保存主流程：`ScenarioClass::SaveGame`（0x67CEF0 区，Phobos 挂 4 个 hook）
- 对象序列化：见 YRpp `AbstractClass.h` `virtual HRESULT Save(IStream*)`

## 保存主流程（ScenarioClass::SaveGame @ 0x67CEF0）

反编译还原（Phobos hook 点 0x67CEF0/0x67D04E 所在函数）：

```
1. StgCreateDocfile(name, 0x1012, 0, &storage)  创建 OLE 复合文档
2. 填充 SavegameInformation（魔数/版本/标题/玩家/时间戳）
   SavegameInformation::Write(storage)          写属性集 (SummaryInformation)
3. IStorage::CreateStream("CONTENTS", ...)      创建主数据流
4. CoCreateInstance(CLSID 0x7E9540) + OleRun
   → QueryInterface(IPersistStream)             启动 COM 序列化器
5. FUN_0067D300(stream)  序列化主体：
   对每个"对象数组"批次:
     写元素个数 (4 字节 int)
     逐个: QueryInterface(IID_IPersistStream)
           → OleSaveToStream(obj, stream)       对象内存镜像落盘
```

**对象数组批次**（写入顺序，全局数组地址）：

| 全局 | 推测内容 |
|---|---|
| DAT_008B4160 / DAT_008B4148 | 全局可序列化对象 |
| LayerClass::Save | 地图图层 |
| DAT_00A83CA8 / DAT_00A83C9C | 场景对象 |
| DAT_00A80238 / DAT_00A8022C | **HouseClass 数组** |
| DAT_008B4118 / DAT_008B410C 等一串 | TechnoClass/其他对象数组 |
| DAT_00B0E790 / DAT_00B0E730 / DAT_00B0F1B0 | 更多对象数组 |
| FUN_0067FDF0 ~ FUN_00680DF0（30 个） | 特殊类型序列化辅助 |
| Kamikaze::Save / FUN_004391C0 | 特殊对象 |
| SessionClass==5 时 FUN_0069B560 | 遭遇战/联机会话选项 |

## 加载主流程（ScenarioClass::LoadGame @ 0x67E440）

与保存完全对称：

```
1. StgOpenStorage(name, READ)        打开 CFB
2. SavegameInformation::Read          读属性集 (SessionClass::Instance = GameType)
3. SwizzleManagerClass::Instance 初始化  (FUN_006CF230)
4. StgOpenStorage(name, 0x20)         第二次打开
5. IStorage::OpenStream("CONTENTS")   打开主数据流
6. CoCreateInstance(CLSID 0x7E9540) + OleRun
   → QueryInterface 序列化器 (同一个 COM 对象!)
7. [序列化器+0xC](stream)            读回对象
8. 后处理链:
   SwizzleManagerClass::Process       重映射所有指针 (关键!)
   FUN_00685120 / FUN_006D03A0 / FUN_006D04F0
   MapClass::sub_657CE0               场景激活
   MovieInfo::ScenarioStarted = 1
```

**机制核心**：存档序列化 = **COM IPersistStream 逐对象序列化**。
YRpp 证实 `AbstractClass : public IPersistStream`——**每个游戏对象本身就是
COM 对象，自带 IPersistStream::Save/Load**，指针字段经 SwizzleManager
编号序列化、加载时 `Process` 统一重映射（Phobos hook
`LoadGame_PostSwizzle_Phobos @ 0x67E685` 证实）。

## 对象序列化格式（第三层完成）

`AbstractClass::Save @ 0x410320`（FUN_00410320）完全还原：

```
每个对象:
  IStream::Write(&this, 4)          ← 写对象自身地址（4 字节，swizzle 依据）
  IStream::Write(this, Size())      ← 写整个对象原始内存镜像（Size() 字节）！
```

**对象镜像 = [保存时对象地址] + [原始内存 dump]**。镜像内所有指针字段
（Type、Owner、Target 等）= 保存时的原始内存地址，加载时由 SwizzleManager
统一重映射。

### UnitClass::Save 调用链（字段标注）

```
UnitClass::Save    @ 0x744600  (vtable+0x18, UnitClass vtable 0x7F5C70)
  └─ FootClass::Save     FUN_004DB690: +0x588/+0x5AC 两个 DynamicVectorClass 写元素
                                          +0x674 Locomotor (YRComPtr) 嵌入 OleSaveToStream
  └─ TechnoClass::Save   FUN_0070C250 → FUN_0065AC40: +0xE4 数组写元素
  └─ AbstractClass::Save FUN_00410320: 写 [this 地址] + [整个内存镜像]
```

关键字段偏移（镜像内，相对对象起点）：

| 偏移 | 字段 | 序列化方式 |
|---|---|---|
| +0x0 | vtable 指针 | 内存镜像（原样） |
| +0x588 | DynamicVectorClass（FootClass） | 写元素 |
| +0x5AC | DynamicVectorClass（FootClass） | 写元素 |
| +0x670 | **Type（TechnoTypeClass\*）** | 内存镜像（**保存时地址**） |
| +0x674 | Locomotor（YRComPtr） | 嵌入 OleSaveToStream |

### SwizzleManager 机制（0x6CF230/0x6CF2C0）

```
Register (FUN_006CF2C0): 映射表追加 8 字节 {保存时地址, 加载时新对象}
Process   (FUN_006CF230): 遍历重映射所有对象指针 (FUN_006CF350)
```

加载时每个序列化对象 Read 后 Register；Process 按映射表把镜像里的地址
重映射为新对象指针。**只有"进存档的对象"才有映射条目**。

### V3 核弹篡改点定位（原版实锤）

用户实测（原版 YR）：开局苏联基地 + 早期 V3，V3 火箭爆炸效果为核弹。

机制推论：**单位对象镜像中 +0x670（Type 字段）的保存时地址被替换**——
加载时 SwizzleManager 把该地址重映射到另一类型的对象（如核弹弹道
V3Rocket 的 Type → NukeCarrier），V3 发射的火箭即携带核弹弹头。

- 篡改方式：CONTENTS 里定位目标单位对象镜像 → 把 +0x670 的 4 字节
  （保存时 TypeClass 地址）改为另一类型的保存时地址
- 约束：替换地址必须在 SwizzleManager 映射表内有条目
  （TypeClass 不进存档——验证 SwizzleManager::Process 对无条目地址的处理）
- 待验证：FUN_006CF350 重映射细节 + 实际篡改实验（需要开游戏）

## 跨 mod 鬼畜 / 逆天存档原理（推论）

外壳层验证通过（Internal Version 一致）→ 游戏直接信任 CONTENTS 内容。
CONTENTS 是**按对象数组批次序列化的内存镜像**（见保存主流程）：

- **跨 mod 鬼畜**：对象镜像的布局依赖 mod 的类型注册与结构偏移。
  mod 改了 TechnoType 数组顺序/结构 → 加载时按 mod 的注册表解释存档字节
  → 类型索引错位、字段错位 → "鬼畜"
- **逆天存档（V3 射核弹）**：直接改 CONTENTS 里某单位对象镜像的字节——
  把 V3 发射器的武器/弹头类型索引（swizzle 编号映射到类型数组）改成核弹的
  → 加载后 V3 发射核弹。原理同"存档编辑器"修改内存镜像。
- 修改点定位：对象镜像在 CONTENTS 里的偏移 = 批次计数头 + 各对象序列化长度累计，
  可结合同类存档 diff 定位（保存两局仅目标不同的对局，diff CONTENTS）

## 验证方式

- `olefile` Python 库直接解析 .sav（本仓库 `code/analyze_sav.py`）
- 游戏内实测：改 CONTENTS 字节 → 加载 → 观察变化

## 取证文件

| 文件 | 内容 |
|---|---|
| `code/analyze_sav.py` | CFB 结构解析脚本 |
| `code/compare_sav_meta.py` | 多存档元数据对比脚本 |
| `code/ghidra_scripts/decompile_savegame.py` | SavegameInformation 反编译 |
| `code/ghidra_scripts/decompile_savegame_main.py` | 保存主流程反编译 |
| `memory/data/decomp/savegame_decomp.txt` | SavegameInformation + MainLoop 反编译 |

## 未确认

- CONTENTS 内部完整布局（对象流顺序、Swizzle 表格式）——需要专项逆向
- Scenario Description 的编码细节（GBK 宽字符在 UTF-16LE 里的转换）
- GameMode 枚举完整值表（0=战役 已知，5=联机 为实测值）
