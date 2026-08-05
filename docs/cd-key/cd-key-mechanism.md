# YR 正版校验机制（woldata.key + CD 检查 + -CD 开关）

> 逆向目标：原版 `gamemd.exe`（2001-10-31 编译，TimeDateStamp 0x3bdf544e）
> 回答的问题：YR 的"正版校验"到底长什么样？为什么"改 woldata.key 一个字符还能玩"？
> 结论：**单机门禁=光盘存在性检查（`-CD` 开关可免）；woldata.key 不是签名文件，
> 是"按位减法"的联机身份序列号生成器，解密结果不验证真伪。**

## 一句话结论

YR 的验证体系分三路，**只有"光盘存在性"是真正的启动门禁**：
- **单机启动**：`FUN_004A8270` 用 `CDDriveManagerClass::GetCDNumber()` 检查光驱里有没有游戏盘，
  原版内置 `-CD` 命令行开关可绕过（设置免检标志 `0x89E3A0=1` → 全局安全检查短路）
- **LAN 联机**：只查序列号**唯一性**不查真伪（`TXT_SERIAL_DUP`），序列号来自
  `FUN_005DC170`：注册表 `Serial` 与 woldata.key 逐字节按位减法解密，**解密结果不验证**
- **WOL 在线**：序列号在网络/联机路径的具体流向**未取证**（FUN_005DC170 的调用者未追，
  无反编译证据证明序列号"发给服务器"；社区共识认为 WOL 登录需序列号，本仓库未验证）。
  服务器约 2005 年关停（社区共识）。**注意：[PublicKey] 不是 WOL 验签公钥**——
  已实锤为加密 MIX 数据文件的公钥（见第 5 节），原"推测服务 WOL"的表述作废

**"改 woldata.key 一个字符还能玩"的真相：woldata.key 没有校验。** 它是可逆线性变换，
游戏解密出的任何数字串都被接受——它根本没有"正确序列号"这个概念可对比。

## 证据链

### 1. woldata.key 读取与解密（FUN_005DC170 @ 0x5DC170）

反编译全文见 `memory/data/decomp/woldata_read_fn.txt`。核心逻辑：

```c
// 初始化 local_180 = "00000000000000000000"（全零序列号）
// ① 读注册表（可缺失，缺失则保持全 '0'）：
RegOpenKeyExA(HKLM, "SOFTWARE\\Westwood\\Yuri's Revenge", 0, KEY_READ, &key);
RegQueryValueExA(key, "Serial", NULL, ..., local_180, ...);
RegCloseKey(key);

// ② 打开 woldata.key（文件不存在 → return 0）：
iVar3 = FUN_007ca845("woldata.key", ...);
// ③ 逐字节按位减法：
while (iVar4 = 读字节(iVar3), iVar4 != -1) {
    local_180[i] = ((local_180[i] - '0') % 10 - iVar4 + 1000) % 10 + '0';
}
// 即：序列号第 i 位 = (注册表 Serial 第 i 位 - key 第 i 字节) mod 10
// ④ 结果拷贝到输出缓冲，return 1
```

要点：
- **不是验签**：没有任何"比对期望值"的步骤，解密结果直接可用
- **woldata.key 文件实物**：游戏安装目录下的 `Woldata.key`，112 字节，Base64 编码
  （`7YoH5aVx8JYT25Z9VpH0ya0qMV947c7f...`），解码后 63 字节
- **字符串取证**（文件偏移）：`woldata.key` @ 0x431398、`Serial` @ 0x4313a8、
  `SOFTWARE\Westwood\Yuri's Revenge` @ 0x4313b0（VA 0x831398 区）
- **引用点**：`woldata.key` 字符串仅被 FUN_005DC170 引用（xref: 0x5DC283 DATA / 0x5DC296 PARAM）
  ——它在网络层代码区（0x5DCxxx，旁有 "Received READY_TO_GO packet" 等联机协议字符串）

### 2. CD 检查链（启动门禁）

```
ScenarioClass::Start @ 0x683AB0
  └─ 0x683BE6: MOV [ESP+0x18], 0x7E4C30; CALL [0x7E4C30]   ← 门禁调用
     └─ 失败（返回 0）→ JZ 0x683D35（错误处理）

FUN_004790E0 @ 0x4790E0   ← 0x7E4C30 函数指针目标（全局安全检查入口，11 处调用）
  ├─ param == -2 → return 1（放行）
  ├─ DAT_0089E3A0 == 1 → return 1（免检标志短路）★
  └─ 否则 → FUN_004A8270(param)

FUN_004A8270 @ 0x4A8270   ← 真正检查
  ├─ param == -2 → return 1
  ├─ CDDriveManagerClass::GetCDNumber()   ← 枚举光驱找游戏盘
  ├─ 找不到 → 轮询 GetCDClass 实例 + 虚函数检查 CD 内容
  │      （检查失败 → return 0 = 门禁拒绝）
  └─ 通过 → return 1
```

### 3. -CD 命令行开关（免检标志）

- **开关表** @ VA 0x826590 区（文件偏移 0x426590）：`-SOCKET` / `.` / `-DESTNET` /
  **`-CD`** / `/h` / `-h` / `-?` / `/?` / `-play` / `-record` / `-noaudio` / `-nostr` / `-str` / `-jabber`
- **FUN_0052F620**（命令行解析器）检测到 `-CD`：
  ```asm
  0052f79a  PUSH 0x8265a8            ; "-CD"
  0052f79f  PUSH ESI                 ; 命令行参数
  0052f7a0  CALL 0x007ca4b0          ; 字符串比较
  0052f7aa  JZ 0x0052f7c0            ; 不匹配 → 跳过
  0052f7af  MOV byte ptr [0x0089e3a0],0x1   ; ★ 免检标志 = 1 ★
  ```
- **0x52F7AF 是条件写入，结构完整无 patch 痕迹**（PE TimeDateStamp 2001-10-31 原版）——
  证明该 exe 未被字节级修改

### 4. CDCheck 类方法（易混淆点）

| 函数 | 地址 | 实际行为 |
|---|---|---|
| `CCFileClass::CDCheck` | 0x4739F0 | **不是校验**：`CDFileClass::OpenEx() + SetFileName(scenarioName)`——CD 文件系统初始化，返回值被调用点丢弃 |
| `RawFileClass::CDCheck` | 0x65CA70 | 空函数（3 字节 `return;`） |

名字叫 CDCheck，干的是文件系统初始化——**原版启动路径上不存在"序列号签名门禁"**。

## 用户案例（2026-08-06，局域网"改一个字符"）

用户与他爸同份游戏（同序列号）LAN 联机被拒 → 改 woldata.key 一个字符 → 解密序列号
某位变化 → 与父亲序列号不同 → `TXT_SERIAL_DUP` 检查通过 → 可联机。
单机/遭遇战/战役全程无感：单机门禁靠 `-CD`（启动器传参）免除，woldata.key 不在单机路径。

## 破解全景（免 CD 的本质）

**免 CD 不需要 patch exe 一个字节**：让 gamemd.exe 带 `-CD` 参数启动即可——这是西木
原版内置的官方开关。用户安装目录即实证：YRLauncher.exe（第三方启动器）
负责兼容启动，Ares.dll+Syringe.exe（Ares 平台）、ipxwrapper.dll（IPX 包装）、
Secdrv.sys（SafeDisc 驱动，Win10+ 失效）。

## 关键地址表

| 地址 | 语义 | 依据 |
|---|---|---|
| 0x4A8270 | CD 检查本体（GetCDNumber 门禁） | 反编译 |
| 0x4790E0 | 全局安全检查入口（0x7E4C30 指针目标，11 处调用） | 反编译 |
| 0x7E4C30 | 安全检查函数指针（.rdata，指向 0x4790E0） | 字节取证 |
| 0x89E3A0 | 免检标志（=1 短路全部检查） | 反编译 xref |
| 0x52F7AF | `-CD` 开关写入免检标志处 | 汇编 |
| 0x8265A8 | `-CD` 字符串 | .rdata |
| 0x5DC170 | woldata.key 读取/解密函数 | xref + 反编译 |
| 0x4739F0 / 0x65CA70 | CCFileClass::CDCheck / RawFileClass::CDCheck | 符号表 + 反编译 |
| 0x831398 | `woldata.key` 字符串 VA | 偏移映射 |

## 5. [PublicKey] 公钥的真相（已实锤，非序列号验证）

**不是序列号/WOL 验签——是加密 MIX 数据文件的公钥**（WinMain 启动早期"Init Encryption Keys"模块）：

```c
// WinMain @ 0x6BB9A0（COM 注册之后），引用点 0x6BD735（字符串块起始 0x7E1A80）：
FUN_004068e0("Init Encryption Keys");               // 日志
FUN_004068e0("Init Keys: declarations");
FUN_0065c250("[PublicKey]\n1=AihRvNo...", strlen);  // 公钥 INI → 解析器
FUN_004068e0("Init Keys: Load");
FUN_00525a10(local_bdc, 0);                         // Load 公钥
FUN_004068e0("Init Keys: Init fast key");
FUN_0052a670(local_de0, 1);                         // RSA-512 密钥结构（0x81 dword）
... 拷贝到全局 DAT_00886980 ...
FUN_005b3c20("LANGMD.MIX", &DAT_00886980);          // ★ 用密钥打开加密的 MIX
FUN_005b3c20("LANGUAGE.MIX", &DAT_00886980);
```

- 日志 `"Init Encryption Keys"` 直接点名用途；vtable 0x7E1A64（构造函数 0x40D800/0x40D808/0x40D858）是密钥持有类
- **西木加密体系**：RSA-512 公钥（数据文件验证）+ Blowfish（MIX/网络流/woldata.key 加密）
- 经验教训：搜索字符串引用先确认**字符串块真实起始**（0x7E1A80 含 "[Pub"），曾误搜 0x7E1A88 导致误判"0 引用"

## 未确认 / 待验证

1. **FUN_005DC170 的调用者**：woldata.key 解密出的序列号最终流向哪（LAN 查重？
   WOL 登录？）未追调用链——**"序列号发给 WOL 服务器"无取证依据，社区共识仅作参考**
2. **0x4AB10**（`-CD` 分支后调用）：语义未确认
3. **`FUN_007ca845`/`FUN_007cb80b` 身份**：woldata.key 的打开/读字节函数
   （可能是 CCFileClass 包装），未确认
4. **注册表 Serial 的写入点**：安装程序写注册表，游戏内是否也写，未确认
5. **LANGMD.MIX 的解密算法细节**：RSA-512 如何派生 Blowfish 密钥解密 MIX（未深挖）

## 取证文件

| 文件 | 内容 |
|---|---|
| `code/ghidra_scripts/decompile_cdcheck*.py` (1-4) | CD 检查链反编译脚本 |
| `code/ghidra_scripts/dump_cdcheck_patch.py` | 0x52F7AF 写入点汇编 |
| `code/ghidra_scripts/decompile_woldata*.py` | woldata.key xref + 读取函数 |
| `code/ghidra_scripts/iat_probe.py` / `read_cmdline_strings.py` | IAT 解析 / 开关字符串 |
| `memory/data/decomp/cdcheck_decomp*.txt` | CDCheck 反编译（4 份） |
| `memory/data/decomp/cdcheck_patch_asm.txt` | 0x52F7AF 汇编 + 命令行解析上下文 |
| `memory/data/decomp/woldata_xref.txt` / `woldata_read_fn.txt` | woldata.key 引用点 + 解密函数全文 |
| `memory/data/decomp/publickey_xref.txt` / `publickey_use.txt` | [PublicKey] 引用点 + WinMain 密钥初始化全文 |
