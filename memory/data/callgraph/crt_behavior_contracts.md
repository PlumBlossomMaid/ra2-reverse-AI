# CRT 行为断言清单

> **定位**：CRT 区（`0x7Cxxxx-0x7Dxxxx`＝MSVC 6.0 静态链接运行库，非 Westwood 手写）
> 不逐函数"结构复刻"，而是把"游戏逻辑依赖的 CRT 行为点"登记为**断言**——
> 现代 CRT 天然满足的标"已满足/待验证"，必须显式对齐的标"须对拍"。
> 本文档是"行为断言表"，不是"逐函数翻译表"。
> 状态语义见文末；凡"已满足"必须给出**依据**（编译器保证 / 证据地址 / C++ 标准条款）。

## 分层依据与目标

- 主代码区 `0x400000`＝Westwood 手写 → **行为级对拍复刻**（策略见 `docs/full-reimplementation/dependency-atlas.md`）
- CRT 区 `0x7C-0x7D`＝MSVC 6.0 静态 CRT → **行为断言**（本文档）
- 浮点语义层＝x87 中间精度 + `_control87` 全局舍入 → **独立契约**（见 `docs/random-map/fpu-and-random-idioms.md`）

## 断言格式

```
## 0x7Cxxxx  <别名/符号名>
- 事实（登记地址+证据批次）：……
- 断言：……
- 状态：已验证(证据地址) / 待验证 / 须对拍 / partial / NO_FUNCTION
- 备注：……
```

状态取值：
- `no_function`：Ghidra 无函数边界，不猜测语义
- `partial`：静态语义已确认，动态/ABI 层面待回访
- `待验证`：语义已读，未在 2026 环境验证
- `验证通过(证据)`：已在现代 CRT/测试环境验证一致
- `须对拍`：行为点依赖编译器/平台实现，必须与原版逐位对拍才能断言

---

## 0x7D0F45  CRT realloc 入口（heap 断言细化）
- 事实：见下方"realloc 入口（heap 断言细化）"节（batch 056 升级）——本节为历史锚点，状态已升级。
- 状态：**semantics_verified**（batch 056 升级，详见下方同名节；本锚点节保留是为了兼容旧引用点。）

## 0x7C9430  CRT 内部 malloc（`_malloc`）
- 事实：18B，排名 2780，扇入 48/37，调用者含 `FUN_00408610`,`FUN_00409c40`,`FUN_00759540`,`FUN_007be5d0`,`FUN_007cc6c3`（多个主代码调用点）。
- 断言：对齐/失败语义与现代 CRT malloc 一致。
- 状态：**semantics_verified**（batch 063 升级：18B 完整汇编已读（frontier_batch_020）——压 `[0xb782ac]` retry-hook 存在标志后转发 0x7C9442；`size<0xE0` 失败路径走 0x7D06E8 retry hook 循环；16B 对齐；失败→NULL。原"待验证"因 realloc 0x7D0F45 batch 056 五行为断言全闭合（含 hook 循环）理由消除；malloc 是 realloc NULL 分支的直接落点，行为一致。）

## 0x7C93E8  CRT 内部 free
- 事实：72B，排名 517，扇入 135/55，调用者含 `CCFileClass::Open`,`ColorScheme::~ColorScheme`,`FUN_00408610`,`FUN_00409c40`,`FUN_00433940`,`FUN_0047aac0`,`FUN_0047ab10`。
- 断言：free 语义与现代 CRT 一致；空指针安全。
- 状态：**semantics_verified**（batch 063 升级：72B 完整汇编已读（frontier_batch_020）——NULL 直返；否则 `0x7CD9F5(9)` 锁 heap → `0x7CF7BD` 找段 → 命中 `0x7CF7E8` 释放 + 解锁；未命中 → 解锁 + `HeapFree(heap,0,p)`。原"待验证"因下游 `0x7CF7BD`/`0x7CF7E8` batch 022 已登记且 realloc 0x7D0F45 batch 056 五行为断言全闭合，理由消除；free 是 realloc size==0 分支的直接落点，行为一致。）

## 0x7D30D1  FlushFileBuffers 句柄包装
- 事实：147B，排名已用；`DAT_00b78b80` 为句柄表上限，槽 flag bit0=active；`0x7D96A8`/`0x7D9707`＝槽临界区进出，`0x7D95BF`＝native handle 校验，`0x7D168E`/`0x7D1697`＝errno/dos-error 槽。失败映射：`errno=9`,`dos_error=GetLastError`。
- 断言：flush 失败时 errno=9 且 dos_error 保留系统错误码。
- 状态：已验证(测试 `HandleTable::flush`，`test_handles.cpp`)。

## 0x7D0F45  realloc 入口（heap 断言细化）
- 事实：312B；`param_1==NULL`→`0x7C9430`(malloc)；`param_2==0`→`0x7C93E8`(free)；否则尝试路径顺序：
  ① `0x7CF7BD` 找段 → 段内且 `size <= DAT_0087c584` → `0x7CFFC8`（原地/复制）→ 失败再 `0x7CFB13`（重新分配+复制）→ 复制大小 = `min(size, *(block-4)-1)`；
  ② 段外或超限 → `HeapReAlloc`；
  ③ 都失败 → `HeapAlloc(0, size+0xF & ~0xF)` + memmove + 释放旧块；
  ④ 兜底 → retry hook `0x7D06E8`，循环。锁守护：`0x7CD9F5`/`0x7CDA56`（region lock 9）。
- 断言：`realloc(p,size)`：NULL+p→malloc；p+0→free 返回 NULL；成功→16B 对齐；失败→全堆规约后走 retry hook；复制字节数 = min(size, 旧块可用)（旧块头在 user-4，语义为"由 heap block tag 取得的可复制长度"）。
- 状态：**semantics_verified**（batch 056 升级：312B 完整汇编 + 反编译二次全读，与 batch 020 结论一致无漂移——NULL→malloc、0→free、锁 9（`0x7CD9F5`/`0x7CDA56`）、`0x7CF7BD` 找段 → `size<=DAT_0087c584` 段内 `0x7CFFC8`（原地/复制）→ 失败 `0x7CFB13` → 复制 `min(size, *(block-4)-1)`、段外 `HeapReAlloc`、兜底 `HeapAlloc(0, size+0xF & ~0xF)`+memmove+free、retry hook `0x7D06E8` 循环。**五行为断言全闭合**：16B 对齐、0 语义、失败回退链、复制量 min(size, 旧块可用)、hook 循环。heap 内部函数（`0x7CF7BD`/`0x7CFFC8`/`0x7CFB13`/`0x7CF7E8`）按"行为断言"层处理不逐字节复刻。**batch 064 补充**：region lock 9 的锁原语 `0x7CD9F5`/`0x7CDA56` 已新登记（batch 064 节），本节的锁守护断言引用该节。）
- 备注：这是"游戏逻辑依赖的 heap 行为点"的核心入口——对齐、0 语义、失败回退、旧内容复制量四个行为必须在 2026 环境逐条断言。

## 0x7D06E8  retry hook 调用点
- 事实：见 `frontier_batch_021.txt`；`DAT_00b782c4` 为 hook 存在标志，`0x7D06E8` 把 hook 返回值布尔化。
- 断言：`realloc` 失败且 hook 存在时循环调用 hook，直到 hook 返回 0 才返回 NULL。
- 状态：已验证（`invoke_allocation_retry_legacy`，`test_lowlevel.cpp`）。

## 0x7CD9F5 / 0x7CDA56  region lock 入口（lock / unlock）
- 事实（batch 064，disasm 完整读 0x7CD9F5 ~ 64B + 0x7CDA56 ~ 18B）：CRT **按区域 key 索引的全局锁表**——锁槽数组起点 `[0x87C2A8]`，每槽一个 4B `CRITICAL_SECTION*` 指针，按 `param_1*4` 索引。
  - **`0x7CD9F5(key)`（Enter）**：`lea esi, [eax*4 + 0x87C2A8]`（key 转槽地址）；`[esi] != 0` → 已有 CS，直接 `push [esi]; call 0x7E11E8`（`EnterCriticalSection`）；`[esi] == 0`（首次）→ `_malloc(0x18)`（`0x7C9430`）分配 24 字节 CS，失败 → `GetLastError` + `FatalAppExitA`/`ExitProcess`；成功 → 若未初始化 `InitializeCriticalSection([esi])`（`0x7E11F4`）→ 写入 `[esi]` → `0x7CDA56(key)` 进锁；释放路径（`0x7CDA3C`）调 `_free` 释放 CS。**双检**：分配与初始化在临界区外完成，进锁后再检查一次 `[esi]` 是否被并发写入。
  - **`0x7CDA56(key)`（Leave）**：`push [eax*4 + 0x87C2A8]; call 0x7E11EC`（`LeaveCriticalSection`），18B 对称包装。
  - **key 用法**（已登记处互证）：`0x7D0F45` realloc 用 key=9（heap 锁）、`0x7D93A1` initterm 用 key=0x12（句柄表锁）、`0x7DD0F8`/`0x7DCFC4` locale 锁用 key=0x13（仅 `[0xB78BA0]!=0` 时）、`0x7D340C` LC 切换用 key=0x19、流锁 `0x7CB3F9`/`0x7CB44B` 用线性映射 `(ptr-0x87BF50)>>5 + 0x1C`。
- 断言：① 锁槽数组 `[0x87C2A8]` 按 4B/key 索引；② `Enter` 惰性分配 24 字节 CRITICAL_SECTION（`_malloc(0x18)`）+ 惰性 `InitializeCriticalSection`；③ `Leave` 对称调用 `LeaveCriticalSection`；④ `key` 是整数索引，多个 CRT 子系统共享同一张锁表（heap=9、句柄表=0x12、locale=0x13、LC 切换=0x19、流=(映射)）；⑤ 分配失败走 `FatalAppExitA`/`ExitProcess`（CRT 级致命错误）。
- 状态：**semantics_verified**（batch 064 新增登记：0x7CD9F5 ~64B + 0x7CDA56 18B 完整汇编；IAT 槽实锤 0x7E11E8=`KERNEL32!EnterCriticalSection`、0x7E11EC=`KERNEL32!LeaveCriticalSection`、0x7E11F4=`KERNEL32!InitializeCriticalSection`；分配失败路径 0x7E134C=`KERNEL32!FatalAppExitA`、0x7E1214=`KERNEL32!ExitProcess`、0x7E1200=`KERNEL32!GetLastError`。lock 9（heap）/0x12（句柄表）/0x13（locale）/0x19（LC 切换）/流锁映射的引用点已在 batch 046/048/054/056 各节互证。）
- 备注：**这是"已登记"的兑现**——contracts 多处节标注"0x7CD9F5/0x7CDA56 已登记 batch 021/020"但一直**没有本条节**（只有 TSV L116 引用 `0x7D06E8_REIMPL`），本批补齐。

## 0x7D95BF  native handle 校验（`_get_nhandle`）
- 事实（证据 `frontier_batch_048.txt`，66B 全读）：`(uint)param_1`。若 `param_1 < [0xB78B80]`（槽表上限）**且** `槽表[0x20组][0x24槽][(n>>5)][(n&0x1F)]` 的 `+4` 字节 bit0 已置（active）→ 返回该槽的 native handle 值（`*槽`）。否则 `0x7D168E()`（errno 槽）=9（EBADF）+ `0x7D1697()`（dos_error 槽）=0 + 返回 -1。
- 断言：① 越界或未 active → errno=9、dos_error=0、返回 -1；② 有效 → 返回槽内 native handle；③ 槽索引布局与 `0x7D96A8` 完全一致（0x20 组 × 0x24 槽）。
- 状态：**cpp_reimplementation_verified**（66B 全读；`HandleTable::validate_native_handle()` 已实现越界/未激活失败映射与有效句柄返回，`test_handles.cpp` 覆盖有效、越界及 release 后失效；runtime 与全量 CTest 均通过。此处验证的是行为模型，不宣称原版 FILE ABI 字节级复刻。）
- 备注：句柄表校验核心——`_flush/_read/_write/_lseek` 等所有句柄操作前的统一合法性检查。

## 0x7D94C4  设置文件句柄槽（`_setfilehandle`）
- 事实（证据 `frontier_batch_048.txt`，124B 全读）：`(uint, HANDLE)`。若 `param_1 < [0xB78B80]` 且对应槽值为 -1（未占用）：①若 `[0x87C2A4]==1`（stdin/stdout/stderr 特例开关）且 param_1 ∈ {0,1,2}，先 `SetStdHandle(-10/-11/-12, param_2)`（把 Windows 标准句柄也同步更新）；②写入槽值为 `param_2`，返回 0。其他（越界/已占用）→ `0x7D168E()`=9（EBADF）+ `0x7D1697()`=0 + 返回 -1。
- 断言：① 仅能设置**未占用**槽（-1）；② param_1 ∈ {0,1,2} 且开关 `[0x87C2A4]` 生效 → 同步更新 Windows 标准句柄（-10/11/12 分别为 STD_INPUT/OUTPUT/ERROR）；③ 越界/已占用 → errno=9、dos_error=0、返回 -1。
- 状态：**cpp_reimplementation_verified**（124B 全读；`HandleTable::bind()` 已实现未占用槽写入、已占用/无效句柄失败映射与原值保持，`test_handles.cpp` 覆盖成功、无效句柄和覆盖拒绝；runtime 与全量 CTest 均通过。标准句柄 `SetStdHandle` 同步未纳入当前跨平台行为模型，故不宣称完整 Windows ABI 复刻。）

## 0x7D93A1  初始化函数指针表（`_initterm`）
- 事实（证据 `frontier_batch_048.txt`，291B 全读）：CRT 静态初始化入口。`FUN_007CD9F5(0x12)`（锁 key 18）后遍历 `DAT_00B78A80` 指针表（每组 32 项、每槽 0x24 字节，共 32 项/组）：①对已存在的组，逐槽调 `0x7D96A8`（Enter + 惰性初始化 CS）→ `0x7D9707`（Leave）→ 若槽 bit0 未置则跳过；②空组 → `_malloc(0x480)` 分配新 0x480 组，初始化每槽 `[0]=0xFFFFFFFF、[1]=0、[2]=0、[5]=0xA`，然后调 `0x7D96A8`（组起始槽）做惰性 init；③返回找到的首个可用槽索引。`0x7CDA56(0x12)` 解锁退出。
- 断言：① 惰性分配 0x480 字节组（32 槽 × 0x24），槽字段布局与 `0x7D96A8` 一致；② 初始化槽内容：`[0]=0xFFFFFFFF`（未激活 handle）、`[1]=0`（flag）、`[2]=0`（CS 未初始化）、`[5]=0xA`（magic/marker）；③ 锁保护（key 18）；④ 返回首个可用槽索引（-1 = 全部已占）。
- 状态：**cpp_reimplementation_verified**（291B 全读；`HandleTable::ensure_group()` 提供按 32 槽惰性扩容并保持新槽 inactive/invalid 的跨平台行为模型，`create()` 在现有槽耗尽时自动扩容后分配新槽，`test_handles.cpp` 覆盖显式扩容与 create 自动扩容；runtime 与全量 CTest 均通过。未声称复刻原版全局组指针表、CRT 锁布局或 `_malloc` ABI。）
- 备注：**CRT 静态初始化入口**——启动期遍历 `.ctors` 段初始化全局对象；每次遇到空槽惰性扩容（每组 +32 槽）。

## 0x7D77D6  单字节 / 转换模式字符输出底层（`_putc` 底层，`0x7D777D` 无锁体）
- 事实（证据 `frontier_batch_054.txt`，105B 全读；`0x7D777D` 引用点闭合）：`(LPSTR 输出缓冲, WCHAR 宽字符) → LPSTR`。分两路：
  - **无 locale 表**（`[0xB782A0]==0`）：`(ushort)param_2 < 0x100` → `*param_1 = (CHAR)param_2` 返回 1；超 0xFF → EILSEQ；
  - **有 locale 表**：`WideCharToMultiByte([0xB782B0], 0x220, &param_2, 1, param_1, [0x87C79C], NULL, &used)`——失败或 used != 0（**转换产生多个字节**）→ EILSEQ 返 -1；成功且 used==0 → 返回字节数。
- 断言：① 等价 `_putc` 底层（宽字符 → ANSI 单字节）；② 无 locale 表快路径直接截断 ≤ 0xFF；③ **转换失败/多字节输出 → errno=0x2A（EILSEQ）+ 返回 -1**；④ 成功返回 1（单字节写）。
- 状态：**semantics_verified**（105B 全读；`0x7D777D` batch 043 已登记、`_REIMPL` `wide_char_to_byte` 已 verified。）
- 备注：**闭合 batch 043 `0x7D777D` 的底层引用**——宽转窄链 `0x7D777D → 0x7D77D6 → WideCharToMultiByte` 全链贯通；与 batch 054 `0x7D9DDC`（字节→宽）成反向对。

## 0x7DCDC0  无效参数异常对象构造（`std::invalid_argument` 构造器）
- 事实（证据 `frontier_batch_054.txt`，50B 全读；`0x7DCD9C` 下游）：`__thiscall (obj* this, undefined4 消息指针, int *pfn, char 析构标记)`。布局：`this[0] = &PTR_FUN_007fa498`（**vtable**）、`this[1] = param_1`（消息）、`this[2] = param_2`（`std::_Fancy_func`?/cb）、`this[3] = 0`；若 `param_2 != NULL && param_3 != 0` → `(**(pfn+1))(pfn)`（**调用 vtable+4 的析构/释放**）。`RET 0xC`（__thiscall 3 参弹栈）。
- 断言：① 是 `std::invalid_argument`/`std::invalid_parameter` 的**构造函数**（MSVC 6.0 布局：vtable + 消息 + 回调 + 标记）；② `param_3` 控制是否立即析构回调对象；③ 返回 `this`。
- 状态：**semantics_verified**（50B 全读；`0x7DCD9C` batch 053 已登记、`PTR_FUN_007fa498` vtable 是 CRT 标准异常布局。）
- 备注：**闭合 batch 053 `0x7DCD9C` 的构造调用点**——无效参数异常链 `0x7DC720 → 0x7DCD9C → 0x7DCDC0（构造）+ __CxxThrowException_8（抛出）` 全链贯通。

## 0x7CA564  有界宽格式化转发（`swprintf` 缓冲上限，batch 002 partial）
- 事实（证据 `frontier_batch_054.txt`，111B 全读；`0x7D21BE` 下游）：`(宽输出缓冲, 宽格式串, ...)` → 调 `0x7D21BE(&local_24, param_2, &stack_arg)`（宽引擎）→ 返回后**写两个 `L'\0'` 结尾**（`local_20` 从 `0x7fffffff` 递减，溢出走 `0x7CE18D(0, &local_24)`——**不受限缓冲**，本质是 `vsprintfW` 的薄封装（`swprintf` 的"安全"版本在此未用 0x7CA564 做缓冲上限检查）。
- 断言：① 等价 `vsprintfW`（宽格式化 + 双 `L'\0'` 结尾）；② `local_20=0x7fffffff` = 无界哨兵（不设缓冲上限）；③ 返回宽字符数。
- 状态：**semantics_verified**（111B 全读；`0x7D21BE` batch 050 已登记、`0x7CE18D` batch 052 已闭合。）
- 备注：**升级 batch 002 的 semantics_verified_partial → semantics_verified**（"identity unresolved"已消除——它是 `vsprintfW` 薄封装；`0x7CA564` 与 `0x7D21BE` 构成 0x7CA564 的完整下游，验证"两套引擎"结论的又一处。）

## 0x7D544A  文件打开入口（`_open` 族）
- 事实（证据 `frontier_batch_054.txt`，719B 全读）：`(LPCSTR 路径, uint mode, uint share, uint create)`。mode 解析：`0x80`→二进制、`0x40`→文本、`0x8000`→no-inherit 探测（`[0xB7857C]`）、`0x02`→追加、`0x08`→截断、`0x100`→`_O_TEXT` 兼容、`0x10`/`0x20`→`_O_RDWR`/`_O_WRONLY` 派生。访问模式映射：`mode&3`：0→`0x80000000`（GENERIC_READ）、1→`0x40000000`（GENERIC_WRITE）、2→`0xC0000000`（READ|WRITE）；share 映射：`0x10/0x20/0x30/0x40` → `0/1/2/3`（FILE_SHARE_*）；create 映射：`0x100/0x200/0x300/0x400/0x500/0x600/0x700` → `4/5/2/3/1/5/1`（CREATE_NEW/TRUNCATE/OPEN/OPEN_ALWAYS/CREATE_ALWAYS）。分配句柄槽 `0x7D93A1` → `CreateFileA` → `GetFileType` 判定 disk(1)/char(2)/pipe(3) 写标志位（`0x40` 宽流 / `0x8` 管道）→ `0x7D94C4` 写槽 → 文本模式 Ctrl-Z 检查（`0x7D769D` seek + `0x7D0844` 单字节读，遇 `0x1A` → `0x7DAC6C` 截断 + seek 回 0）→ 失败回滚 `0x7D2A74`（fclose 族）。无效 mode → errno=0x16（EINVAL）。
- 断言：① `_open` 语义：访问/共享/CREATE 参数按 mode 位映射（与 MSVC CRT 文档一致）；② 文本模式遇首字节 `0x1A`（Ctrl-Z）当 EOF 且**截断文件**；③ "追加"用 `FILE_END` 定位；④ 句柄槽 active 且 flag bit0 置位。
- 状态：**semantics_verified**（719B 全读；`0x7D93A1`/`0x7D94C4`/`0x7D95BF`（batch 048 已登记）、`0x7D96A8`/`0x7D9707`（batch 049 已登记）、`0x7D769D`（batch 049 已登记）、`0x7D0844`（batch 049 已登记）、`0x7D2A74`（batch 043 已登记）——**句柄槽族 + seek/read 原子操作全部闭合**。）
- 备注：**升级 batch 020 的 partial → semantics_verified**（原 partial 理由"入口已读、原子操作待闭合"已消除——所有下游原子操作 batch 048/049/043 全部登记，行为断言完整成立）。

## 0x7DA2B6  wide-only LCMapString 分发包装（`_wcslcas`）
- 事实（证据 `frontier_batch_054.txt`，491B 全读）：签名 `(LCID, flags, LPCWSTR, int, LPWSTR, size_t, UINT)`。启动探测 `LCMapStringW(0,0x100,L"",1,NULL,0)` → `DAT_00b78574=1`（Unicode）；失败再 `LCMapStringA(0,0x100,"",1,NULL,0)` → `=2`（ANSI fallback）。运行期：
  - `==1`：`LCMapStringW(lcid, flags, wide, len, dst, dstlen)` 直调
  - `==2`：`WideCharToMultiByte(cp, 0x220, wide, len, ...)` → `LCMapStringA` → 按 `flags & 0x400`（LCMAP_SORTKEY）：SORTKEY → `_strncpy` 拷贝；非 SORTKEY → `MultiByteToWideChar` 拷回宽字符
  - `param_4 > 0` 时先经 `0x7DA4BF`（宽字符长度前导计数）
- 断言：① 首调探测 Unicode/ANSI 并缓存 `[0xb78574]`；② ANSI 路径下 SORTKEY 用 strncpy，非 SORTKEY 用 WideCharToMultiByte；③ 返回值 = 映射后宽度（字符数，非字节数）；④ 失败 0。
- 状态：**semantics_verified**（batch 054 复核：491B 完整汇编 + 反编译再次读取，与 batch 026/048 结论一致无漂移——Unicode 直调路径 `LCMapStringW(lcid,flags,wide,len,dst,dstlen)`；ANSI 中转路径 `WideCharToMultiByte(cp,0x220)→LCMapStringA→SORTKEY?_strncpy:MultiByteToWideChar`；`[0xb78574]` 探测缓存幂等；`param_4>0` 先经 `0x7DA4BF` 前导计数校正。**下游依赖已全部登记**：`0x7DA4BF` batch 027、`0x7CA650` `_alloca` batch 032、`0x7CA489` wcscpy batch 045、`0x7C91D0` `_strncpy` batch 054。Windows API（`LCMapStringA/W`/`WideCharToMultiByte`/`MultiByteToWideChar`）按 CRT 断言层不验证——现代 Windows 行为一致。）
- 备注：**升级 batch 026/048 的 semantics_verified_partial → semantics_verified**（原 partial 理由"下游待登记"已消除——`0x7DA4BF`/`0x7CA650`/`0x7CA489` 全部已登记，batch 054 又补 `0x7C91D0` `_strncpy` 下游；locale 折叠链全链贯通。）

## 0x7D9540  native handle 清除（`_set_osfhnd` 清理分支）
- 事实（证据 `frontier_batch_011.txt`，127B 全读）：校验槽 active 且 native handle 非 `-1`；标准句柄开关生效时可将槽 0/1/2 同步映射到 `SetStdHandle(..., NULL)`；随后只把槽 native handle 写为 `-1`，**不清 active bit**；失败写 errno=9、dos_error=0、返回 `-1`。
- 调用者取证（同批 `0x7D2AD1`）：先调用 `0x7D9540` 清 native handle，再由调用者单独清 active bit；因此 native handle 生命周期与 active flag 是两层状态。
- 状态：**semantics_verified_partial**（`HandleTable::release_native_handle()` 已覆盖 native handle 清除、失败映射和当前抽象的 slot 失活，但将两层状态合并为一个跨平台操作；标准句柄同步与原版 CRT 槽布局尚未实现，不能宣称 ABI 逐字段等价。）

## 0x7D2A74  句柄槽校验 + 流关闭（`fclose` 族）
- 事实（证据 `frontier_batch_055.txt`，93B 全读）：`param_1`（槽索引）`< [0xb78b80]`（槽总数）且 `[0xb78a80 + (param_1>>5)*4] + (param_1&0x1f)*0x24 + 4` 的 bit0（active 位）置位 → `0x7D96A8(param_1)`（EnterCriticalSection 惰性初始化）→ `0x7D2AD1(param_1)`（实际关闭）→ `0x7D9707(param_1)`（LeaveCriticalSection）；返回关闭结果。否则失败路径：`0x7D168E()`（errno 槽）写 `9`（EBADF）、`0x7D1697()`（doserrno 槽）写 `0`、返回 -1。
- 断言：① active 位检查（槽索引位运算 + bit0）+ 锁槽 + 关闭 + 解锁完整交易；② 无效槽 errno=9（EBADF）/dos_error=0/返回 -1（与 `0x7D95BF` 校验族一致）；③ close 成功或失败后都清 active/native-handle 槽状态，后续可重新分配同一槽位；④ 与 batch 043 登记一致。
- 状态：**cpp_reimplementation_verified**（batch 055 汇编语义已闭合；`code/rewrite/runtime/handles.cpp` 实现 close 后清空 native handle，`test_handles.cpp` 覆盖失败失效与关闭后复用，runtime 与全量 CTest 均通过。）

## 0x7DAC6C  文本模式 Ctrl-Z 逻辑 EOF 截断（`_chsize` 族）
- 事实（证据 `frontier_batch_055.txt`，293B 全读）：`_alloca(0x1004)` 后两次 `0x7D769D`（seek + 位置查询）——首次取当前偏移（`param_1, 0, 1`），二次取文件尾（`param_1, 0, 2`）；任一失败返回 -1。`EDI = param_2 - 当前偏移`（目标长度差）：
  - `EDI < 0`（截短）：`0x7D769D(param_1, param_2, 0)`（seek 到目标）→ `0x7D95BF`（取句柄）→ `SetEndOfFile`（`[0x7E1334]`）；失败 errno=0xD（EACCES）+ GetLastError 存 doserrno。`ESI = (成功?0:-1)`。
  - `EDI > 0`（增长）：`_memset(0)` 清 0x1000 缓冲 → `0x7DC468(param_1, 0x8000)`（flush?）→ 循环 `0x7D0DBA(param_1, 缓冲, min(EDI,0x1000))`（写零块）直至差 ≤0；`0x7D0DBA` 返回 -1 且 errno==5（EIO）→ errno=0xD 重映射 → 返回 -1。
  - 收尾：`0x7D769D(param_1, 保存的偏移, 0)` 恢复位置。
- 断言：① 截短用 SetEndOfFile（seek+截断原子）；② 增长用 0x1000 零块循环写（EIO→EACCES 重映射）；③ 位置经三次 seek 原位恢复；④ 失败统一返回 -1。
- 状态：**semantics_verified**（batch 055 升级：293B 全读，与 batch 021 partial 结论一致无漂移；下游 `0x7D769D`/`0x7D95BF`/`0x7DC468`/`0x7D0DBA` 已登记。）

## 0x7D6D2A  locale/code-page 装载
- 事实（证据 `frontier_batch_055.txt`，381B 全读）：惰性初始化 `[0xb78564]` locale 函数指针——`0x7D745B`（NT 探测）成功 → `GetLocaleInfoA` exref（0x7E130C），失败 → `0x7D7491` 回退。`param_1`（locale 名）非空：`[0xb78554]=param_1`；`0x7D6EA7(&"american"表0x87D1B0, 0x40, &[0xb78554])` 二分查表（"american"→命中改写指针）；`[0xb78558]=param_1+0x40`；二次 `0x7D6EA7(&"america"表0x87D0F8, 0x16, &[0xb78558])`。四路分支：两名皆非空 → `0x7D6EFF`；仅主名 → `0x7D718A`；仅次名 → `0x7D729D`；皆空 → `0x7D735A`。随后 `0x7D7374(param_1+0x80)` 取 code page（cp）→ `IsValidCodePage(cp)` → `IsValidLocale([0xb78544], 1)`——任一失败返回 0。`param_2` 非空：写 3 个 u16 = LCID、sub-LCID（`[0xb78560]`）、cp。`param_3` 非空：`[0xb78564]([0xb78544], 0x1001, param_3, 0x40)`（LOCALE_SENGLANGUAGE）+ `[0xb78564]([0xb78560], 0x1002, param_3+0x40, 0x40)`（LOCALE_SABBREVLANGNAME）+ `0x7D468C(cp, param_3+0x80, 10)` 数字格式化。
- 断言：① NT 下 locale 函数指针 = GetLocaleInfoA（惰性单次绑定）；② "american"/"america" 特判经 `0x7D6EA7` 二分表；③ code page + LCID 双校验失败返回 0；④ LCID/LANG 名称/数字三槽填充幂等。
- 状态：**semantics_verified**（batch 055 升级：381B 全读，与 batch 020 登记一致无漂移；四路分支 + 三槽填充全闭合；下游 `0x7D6EA7`/`0x7D6EFF`/`0x7D718A`/`0x7D729D`/`0x7D735A`/`0x7D7374`/`0x7D468C` 全部已登记。Windows API `IsValidCodePage`/`IsValidLocale`/`GetLocaleInfoA` 按断言层不验证。）

## 0x7D7374  code page 转换辅助
- 事实（证据 `frontier_batch_055.txt`，102B 全读）：`param_1` 空 或 `== "american"`（0x7FA2D8）→ `uVar2=0x1004`；`== "america"`（0x7FA2D4）→ `0xB`；否则原样。经 `[0xb78564]([0xb78560], uVar2, local_c, 8)`（GetLocaleInfoA 取 code page 名）成功后 `0x7C9B72`（parse_decimal_legacy）解析数字串 → cp 数值（存 `[EBP-8]` 返回隐式或经解析器写回）。
- 断言：`"american"→0x1004→GetLocaleInfoA→十进制解析`、`"america"→0xB`；其他名 CP 转换后数字解析；失败返回 0。
- 状态：**semantics_verified**（batch 055 升级：102B 全读，与 batch 020 一致无漂移；`0x7C9B72` 已 model 为 `parse_decimal_legacy`。）

## 0x7D6EA7  二分查找 + 大小写折叠比较（locale 表）
- 事实：88B；调用者 `0x7D6D2A` 等；比较函数 `0x7C8D20`＝不区分大小写（ASCII 折叠 + locale 转换路径）。
- 断言：表按 double/key 排序；未命中不改 `*param_3`；命中写 `param_1+i*8+4`。
- 状态：**semantics_verified**（88B 全读；下游 `0x7C8D20` batch 001 已 verified、`0x7D6D2A` batch 055 已 verified；C++ 模型 `binary_search_legacy` 已 gtest 覆盖。）

## 0x7D468C  数字格式化转发（负数十进制 unsigned 标志）
- 事实：45B；`param_3==10 && param_1<0` → `uVar1=1`，否则 0；转发 `0x7D46B9`；返回 `param_2`。
- 断言：负数十进制 → unsigned 标志置位；其他基数/正数不置位；返回值恒为 `param_2`。
- 状态：已验证（`forward_numeric_format_legacy`，`test_lowlevel.cpp`）。

## 0x7D12E6  有限字符串长度（`_strnlen`）
- 状态：已验证（`forward_numeric_format_legacy`，`test_lowlevel.cpp`）。

## 0x7D6EFF  locale 名称长度/code-page 状态初始化
- 事实：135B；`_strlen(name)==3` → 标志置位；`0x7D75B0`（leading alpha，已 model 为 `leading_alpha_length_legacy`）；`EnumSystemLocalesA(FUN_007d6f86,1)`；末尾保留校验 `(flag & 0x100)&&(flag & 0x200)&&(flag & 0x7)`。
- 断言：名长 3 与 0x100/0x200/0x7 位联合条件成立时保留枚举 flag，否则清零。
- 状态：已验证（`initialize_locale_legacy`，`test_lowlevel.cpp`）。

## 0x7C9B72  十进制前缀解析（locale 数字串）
- 事实：139B；跳过空白（字符分类表 bit 8），可选 `+/-`（bit 4 数字循环 `accum = accum*10 + digit-0x30`），`-` 取负。
- 断言：前导空白、符号、十进制数字前缀按字符分类表解析，尾随非数字停止。
- 状态：**semantics_verified**（`parse_decimal_legacy` 已 model + `test_lowlevel.cpp` 两个 gtest 案例；下游 `0x7C9B72` 依赖的字符分类表 `[0x87C590]` 由 locale 装载写入、已由 0x7D1311 节登记。）

## 0x7C9CC2  strtok 族
- 事实：164B；delimiter bitmap（256B 局部表）+ 循环：跳过连续 delimiter、到非 delimiter 得 token、再遇 delimiter 写 `\0` 并推进、末尾返回 `param_1 != cursor ? param_1 : 0`。
- 断言：连续分隔符被折叠；token 以 NUL 截断；无 token 返回 NULL。
- 状态：**semantics_verified**（`scan_token` 已 model + `test_lowlevel.cpp` 覆盖；delimiter bitmap 256B 局部表 + NUL 截断 + 游标保存，strtok 语义等价。）

## 0x7C9D14  NO_FUNCTION
- 事实：无函数边界（Ghidra 导出仅前一函数尾行 `POP EBX`）。
- 断言：不猜测语义；按 `no_function` 登记。

## 0x7D1311  字符分类查询（`_isctype`）
- 事实（证据 `frontier_batch_024.txt` + batch 044 完整汇编，117B）：扇入 18（调用者全部为 `0x7C98xx-0x7C9Bxx` scanf 族）。`param_1+1 < 0x101` 时直接 `MOVZX EAX, word[ [0x87C590] + param_1*2 ]` 查 384 项字符分类表并 `& param_2` 返回；超界（含 EOF=-1）时按 lead-byte（`[0x87C590][(c>>8)*2+1]&0x80`）组装 1/2 字节转换缓冲，走 `0x7D9729(1, &conv, 1|2, &c, 0, 0, 1)`（多字节→分类），失败返回 0，成功返回转换后字符的类别 `& mask`。`[0x87C590]` = ctype 表基址（每字符 1 ushort 掩码位）。
- 断言：① `c ∈ [0,255]` 返回 `ctype[c] & mask`（无 null 检查，与 VC6 `_isctype` 一致）；② c 超出（含 EOF=-1）→ 走 `0x7D9729`，失败返回 0；③ 返回值恒为"类别位 & 掩码"；④ 超界路径的 lead-byte 判定复用 `[0x87C590]` 表高位（`&0x80` = `_LEADBYTE` 位）。
- 状态：**semantics_verified**（117B 全读；`0x7D9729` 已登记 batch 014/032。）
- 备注：游戏逻辑依赖 `_isctype` 做 `isspace`/`islower` 等分类（`parse_decimal_legacy` 的空白跳过依赖位 8）——分类表由 locale 初始化写入，规则加载期生效。

## 0x7DD0F8  宽字符不区分大小写比较（`_wcsicmp`）
- 事实（证据 `frontier_batch_024.txt` + batch 057 完整汇编 223B 全读）：扇入 20。非多字节 locale（`DAT_00b782a0==0`）：逐 16 位读 `ushort`，A-Z 折叠 `+0x20` 后比较，遇 `L'\0'` 或不等停止；返回 `(uint)uVar1 - (uint)uVar2`（**转小写后**的首个不同字符差值）。多字节 locale：`InterlockedIncrement(&0xB78BA4)` + `0x7CD9F5(0x13)`（locale 锁，仅 `[0xB78BA0]!=0` 时）下逐字符 `0x7DE07C` 折叠比较，再解锁（`0x7CDA56(0x13)` 或 `InterlockedDecrement`）。
- 断言：① 非多字节：ASCII A-Z 不区分大小写，返回首个不同字符（tolower 后）的差值；② 多字节：locale 锁内 `0x7DE07C` 折叠；③ 任一串遇 `L'\0'` 终止。
- 状态：**semantics_verified**（batch 057 升级：223B 完整汇编 + 反编译二次全读，与 batch 024 结论一致无漂移；多字节折叠链 `0x7DE07C` 已在 batch 056 verified——`_wcsicmp` 全链闭合。）

## 0x7DCFC4  字符串转大写（`_strupr`）
- 事实（证据 `frontier_batch_024.txt` + batch 057 完整汇编 308B 全读）：扇入 16。非多字节：原地把 `'a'-'z'` 写 `-0x20` 转大写，返回 `param_1`。多字节：locale 锁（`InterlockedIncrement(0xB78BA4)` + `0x7CD9F5(0x13)` 条件）内先 `0x7D10C2(DAT_00b782a0, 0x200, src, 0xffffffff, 0,0,0,1)` 求映射长度（0x200 = LCMAP_UPPERCASE），`_malloc` 临时缓冲，二次 `0x7D10C2` 写入，`0x7D4BF0` 拷回原串，解锁后 free 临时缓冲。
- 断言：① 单字节：仅 a-z 原地转大写，非字母不动，返回原址；② 多字节：LCMapString(UPPERCASE) 经临时缓冲映射后拷回；③ malloc 失败 → 跳过映射，原串返回。
- 状态：**semantics_verified**（batch 057 升级：308B 完整汇编 + 反编译二次全读，与 batch 024 结论一致无漂移；多字节 LCMap 链 `0x7D10C2` 已在 batch 056 verified——`_strupr` 全链闭合。）

## 0x7D5408  字符串堆复制（`_strdup`）
- 事实（证据 `frontier_batch_024.txt`）：43B，扇入 16（调用者 `CCFileClass::Open`、`FUN_0047ab10` 等主代码）。`_strlen(param_1)+1` → `_malloc` → `0x7D4BF0`（string copy）→ 返回新指针；NULL 输入或 malloc 失败 → 返回 NULL。
- 断言：① 非 NULL：分配 `strlen+1` 字节并拷贝含 NUL 终止符；② NULL 输入 → NULL；③ malloc 失败 → NULL。
- 状态：**semantics_verified**（43B 全读；无未读依赖）。

## 0x7D9729  字符串类型查询包装（`GetStringType` 分发）
- 事实（证据 `frontier_batch_014.txt`）：318B，扇入 3（`_isctype`/`0x7D36D5`/`0x7D6AA2`）。启动探测 `GetStringTypeW(1,"",1,&caps)`：成功→`DAT_00b78568=1`（Unicode 可用）；失败再试 `GetStringTypeA(0,1,...)`→`=2`（纯 ANSI）；都失败返回 0。运行期按 `[0xb78568]` 分派：
  - `==1`（Unicode 可用）：`MultiByteToWideChar(cp, fFlags|0x8?, src, n, 0, 0)` 求宽字符长度 → 栈上缓冲区 → `MultiByteToWideChar` → `GetStringTypeW(nType, wide, len, out)`
  - `==2`（仅 ANSI）：`GetStringTypeA(lcid, nType, src, n, out)` 直调
  - 其他 → 返回 0
- 断言：① 首次调用自动探测 Unicode 可用性，结果缓存在 `DAT_00b78568`；② Unicode 路径：先 `MultiByteToWideChar` 再 `GetStringTypeW`；③ ANSI 路径：直接 `GetStringTypeA`；④ `param_7=1` 把 `MB_ERR_INVALID_CHARS=8` 加入 flags（严格模式）；⑤ 任一失败 → 返回 0。
- 状态：**semantics_verified**（318B 全读；`MultiByteToWideChar`/`GetStringTypeA/W` 为 Windows API，行为断言层不验证）。

## 0x7DA0F1  GetStringType 宽字符分类（`_iswctype` 上游，与 0x7D9729 同构）
- 事实（证据 `frontier_batch_032.txt`，423B 全读）：cache `[0xB78570]` 幂等探测（同 0x7D9729 流程：`GetStringTypeW(1,L"",1,&caps)` 成功 → 1；失败再 `GetStringTypeA(0,1,"",1,&caps)` → 2；都失败 → 0）。**Unicode 路径**（cache==1）：直接 `GetStringTypeW(flags, ws, n, out)`（无中转）。**ANSI 路径**（cache==2）：`WideCharToMultiByte(cp, 0x220, ws, n, NULL, 0, NULL, NULL)` 测窄长 → `_alloca`（0x7CA650）栈缓冲 + `_memset`（0x7D75E0）清零 → `WideCharToMultiByte(cp, 0x220, ws, n, buf, size, NULL, NULL)` 实转 → **在 `buf[n]` 与 `buf[n-2]` 钉两个 0xFFFF 哨兵** → `GetStringTypeA(cp, flags, buf, size, out)`（把窄串类型结果直接写调用者的宽输出缓冲）→ **双哨兵校验**（`buf[n-2]==0xFFFF && buf[n]==0xFFFF` 未被覆盖）→ `0x7CA090(out, buf, n*2)` 把缓冲（现含类型结果）回拷到调用者输出。任一哨兵被覆盖（说明 `GetStringTypeA` 写出了多于 n 个字）→ 失败返回 0。
- 断言：① cache 幂等（同 0x7D9729）；② Unicode 路径直接 `GetStringTypeW`；③ ANSI 路径 `WideCharToMultiByte` 中转 + 双 0xFFFF 哨兵保护 + `0x7CA090` 回拷；④ 哨兵被覆盖 → 失败返回 0；⑤ `0x220` = `WC_COMPOSITECHECK|WC_DEFAULTCHAR` 标志位。
- 状态：**semantics_verified**（423B 全读；下游 `0x7CA090` 已登记 — 批次 031）。

## 0x7D0A20  重叠感知字节拷贝（`memmove` 双端变体，0x7CA090 同构）
- 事实（证据 `frontier_batch_032.txt`，664B 全读）：与 0x7CA090 **逐指令同构**——重叠判断 `(src < dst && dst < src+n)` → 反向（STD + MOVSD.REP 或逐 4B 后向块拷）；否则正向。对齐 `dst & 3` 分支到 0/1/2/3 字节余数跳转表；`n≥32` 用 REP MOVSD + 8 dword 展开。返回 `param_1`（dst）。CRT 在 0x7CA 与 0x7D0 两段分别放置了同一形态的 memcpy 实例（调用点就近）。
- 断言：① 行为与 0x7CA090 完全等价；② 重叠且 dst>src → 反向拷贝保序；③ 返回 dst。
- 状态：**semantics_verified**（664B 全读；与 0x7CA090 同构，行为与现代 CRT 等价——不逐字节结构复刻）。

## 0x7DE07C  单宽字符折叠（`_towlower`）
- 事实（证据 `frontier_batch_025.txt`）：117B，扇入 3（`_wcsicmp`/`0x7DDE80`/`0x7DE002`）。`(ushort)param_1 == 0xFFFF` 原样返回；非多字节（`DAT_00b782a0==0`）：`A-Z`（`0x40 < c < 0x5B`）→ `+0x20` 转小写；多字节：`c < 0x100` 时先 `0x7D3920(c, 1)`（判定该字符是否有多字节折叠入口，返回 0 → 原样返回），否则走 `0x7DA2B6(locale, 0x100, &c, 1, &dst, 1, 0)` 转换，成功用 `dst` 覆盖结果。
- 断言：① `0xFFFF` 恒不变；② 非多字节：仅 `A-Z` 折叠，其余原样；③ 多字节：`c<0x100` 且无折叠表条目 → 原样；④ 有折叠表条目或 `c≥0x100` → `0x7DA2B6` 转换，失败保底返回原字符。
- 状态：**semantics_verified**（batch 056 升级：117B 完整汇编 + 反编译二次全读，与 batch 025 结论一致无漂移——非多字节路径 `0x40 < c < 0x5B → c+0x20`；多字节路径 `0x7D3920(c,1)` 判定 `_UPPER` 位（返回 0 原样返回）→ `0x7DA2B6([0xB782A0], 0x100, &c, 1, &dst, 1, 0)` 转换，失败保底原字符。**下游全部闭合**：`0x7D3920` batch 044 verified、`0x7DA2B6` batch 048/054 verified。）

## 0x7D10C2  LCMapString 分发包装
- 事实（证据 `frontier_batch_025.txt`）：511B，扇入 4。启动探测 `LCMapStringW(0,0x100,L"",1,NULL,0)` → 缓存 `DAT_00b782cc=1`；失败再 `LCMapStringA(0,0x100,"",1,NULL,0)` → `=2`；都失败返回 0。运行期按缓存分派：
  - `==2`（ANSI）：`LCMapStringA(lcid, flags, src, len, dst, dstlen)` 直调
  - `==1`（Unicode）：`param_7==0 → [0xb782b0]`（默认 code page）；`MultiByteToWideChar(cp, (param_8!=0)?8:0|1, src, len, NULL, 0)` 求宽长 → 栈缓冲 → 二次 `MultiByteToWideChar` → `LCMapStringW(lcid, flags, wide, len, NULL, 0)` 求映射长 → 按 `flags & 0x400`（LCMAP_SORTKEY）分支：
    - 非 SORTKEY：`LCMapStringW` 实映射 → `WideCharToMultiByte(cp, 0x220, ...)`（`WC_COMPOSITECHECK|WC_DEFAULTCHAR`）写回 ANSI
    - SORTKEY：`param_6==0` → 返回映射长度；`param_6 < len` → 0；否则 `LCMapStringW` 直写
  - `param_4 > 0` 时先经 `0x7D12E6`（前导字节计数校正）
- 断言：① 首次调用探测 Unicode/ANSI 能力并缓存 `[0xb782cc]`；② 返回值 = 映射后长度（非 0）；③ SORTKEY 模式缓冲过小 → 0；④ flags 传透，`param_8` 严格标志 = `MB_ERR_INVALID_CHARS`；⑤ `param_4>0` 时先经 `0x7D12E6` 前导长度校正。
- 状态：**semantics_verified**（batch 056 升级：511B 完整汇编 + 反编译二次全读，与 batch 025 结论一致无漂移——异常保护 `FS:[0]` + `__SEH_prolog`；探测 `LCMapStringW(0,0x100,"",1,NULL,0)` 成功缓存 1、失败再 `LCMapStringA` 缓存 2、都失败返回 0；ANSI 直调路径、Unicode 中转路径（`MultiByteToWideChar` 求宽长→栈缓冲→二次转换→`LCMapStringW` 求映射长→`flags&0x400` SORTKEY 分支→`WideCharToMultiByte(0x220)` 写回）全闭合。**下游全部闭合**：`0x7D12E6` batch 027 verified、`0x7D3920`/`0x7DA2B6`/`0x7DA0F1` 全部 verified。）

## 0x7D3920  宽字符分类掩码（`_iswctype`）
- 事实（证据 `frontier_batch_026.txt` + batch 044 完整汇编，82B）：扇入 7。`(ushort)param_1 == 0xFFFF` → 0；`c < 0x100` → 查 256 项宽分类表 `[0x87C594]`（每项 ushort 掩码位）；`c ≥ 0x100` → `0x7DA0F1(1, &c, 1, &local, 0, 0)`（`GetStringType` 宽分类分发）失败返回 0；成功返回 `(uint)table[c] & param_2`。
- 断言：① `0xFFFF` → 0；② `c<0x100` 查宽分类表并 `& mask`；③ `c≥0x100` 走 `0x7DA0F1` 转换，失败 0。
- 状态：**semantics_verified**（82B 全读；`0x7DA0F1` 已登记 batch 032。）

## 0x7D39F6  单宽字符转大写（多字节安全 `_towupper`）
- 事实（证据 `frontier_batch_044.txt` + batch 056 完整汇编，119B 全读）：`(ushort)param_1==0xFFFF` → 原样返回。非多字节（`[0xB782A0]==0`）：仅 `'a'-'z'`（`0x60 < c < 0x7B`）→ `+0xFFE0` 转大写，其余原样。多字节：`c<0x100` 时先 `0x7D3920(param_1, 2)`（`_LOWER` 位）判定是否有折叠入口，为 0 → 原样返回；否则或 `c≥0x100` → `0x7DA2B6([0xB782A0], 0x200, &c, 1, &dst, 1, 0)` 转换（`0x200` = `LCMAP_UPPERCASE`），成功用 `dst` 覆盖。
- 断言：① `0xFFFF` 恒不变；② 非多字节仅 ASCII `a-z` 折叠；③ 多字节 `c<0x100` 无 `_LOWER` 位 → 原样；④ 有折叠表条目或 `c≥0x100` → `0x7DA2B6`，失败保底原字符。
- 状态：**semantics_verified**（batch 056 升级：119B 完整汇编 + 反编译二次全读。**身份修正**——本函数是 `_towupper`（大写折叠），非此前登记的 `_towlower`：非多字节路径 `0x60 < c < 0x7B → +0xFFE0`（`'a'+'FFE0'=='A'`）明确是大写；`0x7DA2B6(0x200)` = `LCMAP_UPPERCASE`；`0x7D3920(c,2)` 判 `_LOWER` 位。真正的 `_towlower` 族是 `0x7DE07C`/`0x7D39F6` 的孪生——batch 025/044 登记中把两者混名，本批据汇编实锤区分。）
- 备注：**闭合批次 025 `0x7DE07C` 与 batch 044 `_towlower` 链的对应大写兄弟**——`0x7DE07C`（`_towlower`，`0x100` LCMAP_LOWERCASE）与 `0x7D39F6`（`_towupper`，`0x200` LCMAP_UPPERCASE）共享同一"locale 锁内 `0x7DA2B6` 折叠"骨架，仅 flag 与判定位不同。

## 0x7DA4BF  宽字符串前导长度（`_wcsnlen`-like）
- 事实（证据 `frontier_batch_027.txt`）：48B。逐 16 位扫 `param_1`，最多 `param_2` 项，遇 `L'\0'` 提前停；返回 `(ptr-src)>>1`（**字符数**）当遇 NUL，否则返回 `param_2` 原值；`param_2==0` → 检测 `*p==0` → 返回 0。
- 断言：① 返回 `min(wcslen(s), n)`（字符数，非字节）；② `n==0` → 0。
- 状态：**semantics_verified**（48B 全读）。

## 0x7DA0F1  GetStringType 宽字符分类（`_iswctype` 上游，与 0x7D9729 同构）
- 事实（证据 `frontier_batch_032.txt`，423B 全读；027 已登记入口语义）：cache `[0xB78570]` 幂等探测（同 0x7D9729：`GetStringTypeW(1,L"",1,&caps)` 成功 → 1；失败再 `GetStringTypeA(0,1,"",1,&caps)` → 2；都失败 → 0）。**Unicode 路径**（cache==1）：直接 `GetStringTypeW(flags, ws, n, out)`（无中转）。**ANSI 路径**（cache==2）：`WideCharToMultiByte(cp, 0x220, ws, n, NULL, 0, NULL, NULL)` 测窄长 → `_alloca` 栈缓冲 + `_memset` 清零 → `WideCharToMultiByte(cp, 0x220, ws, n, buf, size, NULL, NULL)` 实转 → **在 `buf[n]` 与 `buf[n-2]` 钉两个 0xFFFF 哨兵** → `GetStringTypeA(cp, flags, buf, size, out)`（把窄串类型结果直接写调用者的宽输出缓冲）→ **双哨兵校验**（`buf[n-2]==0xFFFF && buf[n]==0xFFFF` 未被覆盖）→ `0x7CA090(out, buf, n*2)` 把缓冲（现含类型结果）回拷到调用者输出。任一哨兵被覆盖（说明 `GetStringTypeA` 写出了多于 n 个字）→ 失败返回 0。
- 断言：① cache 幂等（同 0x7D9729）；② Unicode 路径直接 `GetStringTypeW`；③ ANSI 路径 `WideCharToMultiByte` 中转 + 双 0xFFFF 哨兵保护 + `0x7CA090` 回拷；④ 哨兵被覆盖 → 失败返回 0；⑤ `0x220` = `WC_COMPOSITECHECK|WC_DEFAULTCHAR` 标志位。
- 状态：**semantics_verified**（423B 全读；下游 `0x7CA090` 已登记 — 批次 031）。

## 0x7DDCD6  加锁宽字符拷贝（`_wcsncpy_l` 变体）
- 事实（证据 `frontier_batch_048.txt`，317B 全读）：`(ushort*)param_1`。非多字节（`[0xB782A0]==0`）：原地把 `a-z` 写 `+0x20` 转大写，返回 `param_1`。多字节路径分两支：①`[0xB782A0]` 有值但 `DAT_00b78ba0`（`_setlocale` 标记）非零 → 走 `0x7DA2B6` 中转路径——`0x7DA2B6(0x200)` 求映射长度 → `_malloc(iVar2*2)` → 二次 `0x7DA2B6` 写入临时缓冲 → `0x7CA489`（wcscpy）拷回原串 → free 临时缓冲；②`DAT_00b78ba0==0`（未设 locale）→ 走 ASCII 直路径原地大写（与单字节路径等价）。所有多字节路径都在 `InterlockedIncrement(0xB78BA4)` + `0x7CD9F5(0x13)`（locale 锁）内执行、退出前 `0x7CDA56(0x13)` 解锁；`DAT_00b78ba0!=0` 时解锁前额外置位并调用错误处理。
- 断言：① 非多字节 = ASCII a-z 原地大写；② 多字节 + locale 已设 = `0x7DA2B6(0x200)` 中转 + `_malloc` 临时缓冲 + `0x7CA489` 拷回 + free；③ 多字节 + locale 未设 = ASCII 直路径（等价单字节）；④ 全程 locale 锁保护。
- 状态：**semantics_verified**（317B 全读；下游 `0x7DA2B6` batch 048、`0x7CA489` wcscpy batch 045、`0x7C93E8` free、`0x7CD9F5`/`0x7CDA56` 锁全部登记。）
- 备注：**闭合批次 024 `0x7DD0F8` `_wcsicmp` 与 027 `0x7DDE80` `_wcsnicmp` 的"多字节折叠路径"引用**——三者共享同一"locale 锁内 `0x7DE07C`/`0x7DA2B6` 折叠"骨架。

## 0x7DDE80  限长宽字符不区分大小写比较（`_wcsnicmp`）
- 事实（证据 `frontier_batch_048.txt`，248B 全读（batch 027 首读 + 048 补读）：签名 `(ushort*, ushort*, int)`。`param_3==0` → 直接返回 0（恒等）。**非多字节**（`[0xB782A0]==0`）：逐 16 位比较前各把 `A-Z` 折叠 `+0x20`（`uVar1 = (uVar1<0x5b && 0x40<uVar1) ? uVar1+0x20 : uVar1`），最多 `param_3` 项，遇 NUL 或不等退出；返回 `(uint)uVar1 - (uint)uVar2`（折叠后首差字符差值）。**多字节**：locale 锁（`InterlockedIncrement(0xB78BA4)` + `0x7CD9F5(0x13)`）内逐 16 位字符经 `0x7DE07C` 折叠比较，`param_3` 耗尽 / `uVar1==0` / 不等退出；锁退出在 return 前（`0x7CDA56(0x13)` 或 `InterlockedDecrement`）。
- 断言：① `n==0` → 0；② 非多字节：折叠后首差字符差值（与 `_wcsicmp` 同语义、有 n 上限）；③ 多字节：locale 锁内 `0x7DE07C` 折叠比较；④ 返回折叠后首差字符差值（`uVar1 - uVar2`）。
- 状态：**semantics_verified**（248B 全读；下游 `0x7DE07C` batch 025 已登记、`0x7CD9F5`/`0x7CDA56` 锁已登记。）

## 0x7DE002  加锁的宽字符小写折叠（`_towlower_l` 加锁变体）
- 事实（证据 `frontier_batch_048.txt`，122B 全读）：`(ushort)param_1 == 0xFFFF` → 返回 0xFFFF（直通）。非多字节（`[0xB782A0]==0`）：`A-Z`（`0x41≤c≤0x5A`）→ `+0x20`，返回折叠后。多字节：`InterlockedIncrement(0xB78BA4)` + `0x7CD9F5(0x13)`（locale 锁）内调 `0x7DE07C(param_1)` 折叠，退出前解锁（`0x7CDA56(0x13)` 或 `InterlockedDecrement`）。
- 断言：① 0xFFFF 直通；② 非多字节 A-Z 折叠；③ 多字节在 locale 锁内折叠（与 0x7DE07C 一致）。
- 状态：**semantics_verified**（122B 全读；`0x7DE07C` batch 025、锁已登记。）
- 备注：**闭合批次 027 引用**——之前 partial 仅因未复核；与 `0x7D39F6`（batch 044）语义等价，差异仅在**加锁**（`_towlower_l` 变体）。

## 0x7DA2B6  wide-only LCMapString 分发包装（`_wcslcas`）
- 事实（证据 `frontier_batch_026.txt`）：491B，签名 `(LCID, flags, LPCWSTR, int, LPWSTR, size_t, UINT)`。启动探测 `LCMapStringW(0,0x100,L"",1,NULL,0)` → `DAT_00b78574=1`（Unicode）；失败再 `LCMapStringA(0,0x100,"",1,NULL,0)` → `=2`（ANSI fallback）。运行期：
  - `==1`：`LCMapStringW(lcid, flags, wide, len, dst, dstlen)` 直调
  - `==2`：`WideCharToMultiByte(cp, 0x220, wide, len, ...)` → `LCMapStringA` → 按 `flags & 0x400`（LCMAP_SORTKEY）：SORTKEY → `_strncpy` 拷贝；非 SORTKEY → `MultiByteToWideChar` 拷回宽字符
  - `param_4 > 0` 时先经 `0x7DA4BF`（宽字符长度前导计数）
- 断言：① 首调探测 Unicode/ANSI 并缓存 `[0xb78574]`；② ANSI 路径下 SORTKEY 用 strncpy，非 SORTKEY 用 WideCharToMultiByte；③ 返回值 = 映射后宽度（字符数，非字节数）；④ 失败 0。
- 状态：**semantics_verified**（batch 048 复核：491B 完整汇编 + 反编译再次读取，行为确认无漂移——Unicode 直调路径 `LCMapStringW(lcid,flags,wide,len,dst,dstlen)`；ANSI 中转路径 `WideCharToMultiByte(cp,0x220)→LCMapStringA→SORTKEY?_strncpy:MultiByteToWideChar`；`[0xb78574]` 探测缓存幂等；`param_4>0` 先经 `0x7DA4BF` 前导计数校正。**下游依赖已全部登记**：`0x7DA4BF` batch 027、`0x7CA650` `_alloca` batch 032、`0x7CA489` wcscpy batch 045、`0x7C91D0` `_strncpy` batch 054。**与 batch 054 同名节结论完全一致**——本重复节仅作历史锚点保留，最新结论见 batch 054 节。）

## 0x7D12E6  有限字符串长度（`_strnlen`）
- 事实（证据 `frontier_batch_026.txt`）：43B，签名 `(char*, int)`。`param_2 != 0` 时 `for (i=0; i<len; i++) if (*p==0) break; p++;` 然后 `*p==0 ? p-src : len`。**这就是 `min(strlen(src), len)` 实现**。
- 断言：① 返回 `min(strlen(s), n)`；② `n == 0` → 返回 0（因不进入循环且检测 `*p == 0` → 计算偏移 0）。
- 状态：**semantics_verified**（43B 全读，纯字符串长度）。

## 0x7CA090  重叠感知字节拷贝（`memmove` 双端变体）
- 事实（证据 `frontier_batch_031.txt`，664B 全读；028 已登记行为）：`(src < dst && dst < src+n)` → **反向**（STD + MOVSD.REP 或逐 4B 后向块拷）；否则正向。对齐 `dst & 3` 分支到 0/1/2/3 字节余数跳转表；`n≥32` 用 REP MOVSD + 8 dword 展开。返回 `param_1`（dst）。**宽表复制分支**（0x7DA0F1 下游的 `WideCharToMultiByte` 回拷路径）即此反向拷贝路径——`dst > src` 重叠时后向复制保序。
- 断言：① 重叠且 dst>src → 反向拷贝保序；② 返回 dst；③ 对齐分支覆盖 0..3 余数；④ 反向路径同时服务 0x7DA0F1 的宽表回拷（批次 031 闭合下游）。
- 状态：**semantics_verified**（664B 全读，标准 memmove 的 MSVC 生成形态，行为与现代 CRT 等价——不逐字节结构复刻）。

## 0x7D75E0  `_memset`
- 事实（证据 `frontier_batch_028.txt`）：88B，Ghidra 库匹配 "Visual Studio 1998 _memset"。`size==0` → 返回 `_Dst`；`_Val & 0xff` 展开成 4 字节（`AL<<8|AL<<16`），`_Dst&3` 前缀逐字节 → REP STOSD → 尾部逐字节。
- 断言：① 返回 `_Dst`；② 低字节填充 4 字节模式；③ 已登记 `memset_legacy`（`frontier_batch_015.txt`，`test_lowlevel.cpp`）。
- 状态：**semantics_verified**（batch 061 词汇对齐：Ghidra 库函数匹配 + `memset_legacy` gtest 覆盖，与 TSV 一致；旧词汇「验证通过」规范化为 semantics_verified 以消除漂移扫描误报。）

## 0x7D9872  宽字符数字串解析（`_wtoi`/`strtod` 前端状态机）
- 事实（证据 `frontier_batch_028.txt`）：1185B，扇入 5。跳过空白（空格/HT/LF/CR）→ 状态机（`iVar10` 为状态）：0=初态、1=前导 0/符号、2=符号已读、3=十进制数字收集、4=数字后检查、5=十进制终止、6=指数 'E'/'e'、7=指数数字、8=指数前导 0、9=指数累加、0xb=指数符号、0xa=终止。数字按位存 `local_60[25]`（低字节 = 每位数字），`local_8`=位数（cap 0x19，溢出 → `local_c` 计数）。指数累加 `iVar4 = iVar4*10 + d - 0x30`，cap 0x1451。收尾：`0x7DB140(local_60, len, &local_44)` 把数字串拼成浮点尾数；`FUN_007dc0f6(&local_44, exp+param_5-param_6, param_4)` 应用十进制指数；上溢（>0x1451）→ `local_30=1` → 结果 `{0x7fff, 0x80000000}`（= 无穷/溢出标志）；下溢 → `local_34=1` → 结果 0。返回状态（`local_18`：1=下溢、2=上溢、4=正常）。
- 断言：① 空白/符号/前导 0/十进制/指数完整状态机；② 数字位数 cap 25、溢出单独计数；③ 指数 cap 0x1451 触发上溢标志；④ 结果写入 `param_1` 的 4 个字段（`[0]`=低位尾数、`[1]`=高位、`[3]`=指数字节、`[5]`=符号标志）；⑤ 返回 `local_18` 状态码（1/2/4）。
- 状态：**semantics_verified**（batch 061 闭环：1185B 全读；下游 0x7DB140（数字串→浮点尾数）batch 061 已 verified、0x7DC0F6（十进制指数应用）batch 061 已 verified——原 partial 理由"下游未读"消除；整条 strtod 前端状态机 5 断言闭合，与 0x7D151E/0x7D7CC1/0x7D7D1C/0x7D7CEE 四个下游入口完全对齐。）

## 0x7DB140  数字串 → 浮点尾数构造（`_str2flt` 尾数部分）
- 事实（证据 `frontier_batch_029.txt`）：199B。把 `param_2` 个数字字节逐位经 `0x7DB0E5`（×10 进位）+ `0x7DB087`（+digit）累加进 3 个 dword（`param_3[0..2]`）；收尾归一化：`[2]==0` 循环左移 16 位（`[2]=[1]>>16; [1]=([0]>>16)|([1]<<16); [0]<<=16`，每轮指数 `local_8 -= 0x10000`）；`[2]&0x8000==0` 循环 `0x7DB0E5`（×2）直到最高位进 0x8000（每轮指数 `-0x10000`）；最终指数 `0x404E + 修正` 写 `param_3[5]`（ushort）。结果 = 3-dword 尾数 + 16 位指数。
- 断言：① 逐位 ×10+digit 累加；② 归一化到 `[2]&0x8000` 置位；③ 指数从 0x404E（= 16398）开始按移位修正。
- 状态：**semantics_verified**（batch 061 闭环：199B 全读；下游 0x7DB0E5（×2 移位）batch 030、0x7DB087（___add_12）batch 030 均已 verified——原 partial 理由"下游未读"消除；尾数构造 + 归一化 + 指数修正三断言闭合。）

## 0x7DC0F6  十进制指数应用（查 10 的幂表乘法）
- 事实（证据 `frontier_batch_029.txt`）：124B。`param_2`（十进制指数）为正/负选表基址 `0x87D6C0`/`0x87D820`；每轮 `idx >> 3`（除以 8）取 `0x54` 字节间隔的段，`idx & 7` 选段内 `×0xC` 间隔的条目（`>0x7FFF` 则复制 + 指数 -1）；调 `0x7DBED6`（3-dword 乘法）乘到尾数。`param_3==0` 时结果写 0。
- 断言：① 10 的幂表按 `2^3` 分档（每档 8 条、每条 0xC 字节）；② 负指数用负表；③ 最终尾数 ×= 10^|exp|。
- 状态：**semantics_verified**（batch 061 闭环：124B 全读；下游 0x7DBED6（3-dword 乘法）batch 030 已 verified——原 partial 理由消除；10 的幂表分档 + 负表切换 + 乘法应用三断言闭合。）

## 0x7D6AA2  ctype 表装载（`_setmbcp` 核心）
- 事实（证据 `frontier_batch_048.txt`，581B 全读（batch 029 首读 + 048 复核）：`DAT_00b782a0==0`（单字节 locale）→ 直接把 `[0x87C590]`/`[0x87C594]` 指向静态 `DAT_0087c59a` 并 free 旧表、返回 0。多字节路径：4 次 `_malloc`（0x202×2 宽表 + 0x101 字节表 + 0x202 宽分类表）→ `GetCPInfo(cp, &local_28)`（`MaxCharSize<3` 校验）→ 字节表填 0..255、`MaxCharSize>1` 时按 `LeadByte[]` 范围把首字节位置 0（lead byte 标记）→ `0x7D9729(1, bytes, 0x100, wide+1, ...)` 生成窄→宽映射 → `0x7DA0F1(1, wide, 0x100, wcls+1, ...)` 生成宽分类 → 同样按 lead byte 把宽表对应位置 0x8000 → 写全局 `[0x87C590]`=宽映射+1、`[0x87C594]`=宽分类+1 → free 旧表、换新 `[0xB78518]`/`[0xB7851C]`。失败 → free 全部并返回 1。
- 断言：① 单字节 → 指向静态表 + free 旧表；② 多字节 → `GetCPInfo` 驱动、lead byte 位置 0/0x8000；③ `[0x87C590]`=窄→宽映射、`[0x87C594]`=宽字符分类（与 0x7D1311/0x7D3920 的查表一致）；④ 失败路径统一 free + 返回 1。
- 状态：**semantics_verified**（581B 全读；下游 `0x7D9729` batch 014、`0x7DA0F1` batch 032、`0x7C93E8` free、`0x7DAF28` GetCPInfo 包装均已登记，无新依赖。）

## 0x7DB0E5  3-dword 左移 1 位（12 字节数 ×2）
- 事实（证据 `frontier_batch_030.txt`）：46B。`[0]<<1; [1]=[1]<<1|[0]>>31; [2]=[2]<<1|[1]>>31`——进位逐级传播的 96 位左移。
- 断言：① 等价于 12 字节定点数 ×2；② 最高位（`[2]>>31`）移出丢弃（溢出由调用方指数调整吸收）。
- 状态：**semantics_verified**（46B 全读，纯移位）。备注：`strtod` 12 字节浮点链基元。

## 0x7DB087  `___add_12` 3-dword 带进位加法
- 事实（证据 `frontier_batch_030.txt`）：94B，Ghidra 库匹配 "Visual Studio 2003 Release"。三段 `0x7DB066(a,b,&dst)`（带进位加原语）串行：`[0]+[0]` → 进位 `[1]+1` → `[1]+[1]` → 进位 `[2]+1` → `[2]+[2]`。
- 断言：① 等价于 12 字节数加法；② 进位跨段传播（低位溢出 → 高段 +1）。
- 状态：**semantics_verified**（94B 全读；下游 `0x7DB066` 已读 — 批次 031）。

## 0x7DB066  带进位加原语（12 字节算术基元）
- 事实（证据 `frontier_batch_031.txt`）：33B。`sum = a + b`；进位 = `(sum < a) || (sum < b)`（比较检测无符号回绕）；`*dst = sum`；返回进位标志。
- 断言：① 等价于 32 位无符号加法 + 回绕检测；② 返回 0/1 进位；③ 被 0x7DB087/0x7DBED6 用作多段累加基元。
- 状态：**semantics_verified**（33B 全读，纯算术）。备注：`strtod` 12 字节浮点链基元。

## 0x7DBED6  12 字节浮点乘法（`_ld12mul` 风格）
- 事实（证据 `frontier_batch_030.txt`）：544B。指数相加（掩 0x7FFF）+ 符号 XOR；下溢（`exp<0x3FC0`）→ 结果 0；上溢/NaN（`exp≥0x7FFF` 或 `exp≥0xBFFE`）→ 特殊值 `0x7FFF8000`（±INF 位模式）；尾数 5×5 部分积循环（16 位 × 16 位经 `0x7DB066` 进位累加）→ 归一化移位 + 指数修正 → 舍入（`0x7DB113` 右移 + 粘滞位）。
- 断言：① 等价于 12 字节（3-dword 尾数 + 16 位指数）浮点乘法；② 下溢/上溢特殊值位模式固定（`0` / `0x7FFF8000|sign`）；③ 舍入依赖粘滞位。
- 状态：**semantics_verified**（544B 全读；下游 `0x7DB066`/`0x7DB113`/`0x7DB0E5` 全读 — 批次 031）。
- **备注（浮点语义层关联）**：`0x7D9872 → 0x7DB140/0x7DC0F6 → 0x7DBED6/0x7DB087` 是原版 **strtod 的十进制→12 字节浮点转换链**——rules.ini 数值 → double 位模式的必经路径。按三层策略属于"须对拍"行为点（lockstep 联机需 strtod 位模式一致）；CLI 侧暂以现代 CRT strtod 为准，后续做 ULP 对拍决定是否需重写此链。

## 0x7DB113  3-dword 右移 1 位（12 字节数 ÷2）
- 事实（证据 `frontier_batch_031.txt`）：45B。`[2]>>1; [1]=[1]>>1|[2]<<31; [0]=[0]>>1|[1]<<31`——进位逐级传播的 96 位右移（0x7DB0E5 的对称逆）。
- 断言：① 等价于 12 字节定点数 ÷2；② 最低位移出丢弃（由舍入逻辑收集为粘滞位）。
- 状态：**semantics_verified**（45B 全读，纯移位）。备注：`strtod` 12 字节浮点链舍入基元。

## 0x7D9707  临界区槽释放（`LeaveCriticalSection` 槽索引）
- 事实（证据 `frontier_batch_049.txt`，34B 全读）：`index>>5` 选槽组指针 `[0xB78A80]`，`index&0x1F` → `×9×4`（0x24 间隔）槽内 +0xC 偏移；`LeaveCriticalSection(槽)`。
- 断言：① 槽索引布局与 0x7D96A8 完全一致（0x20 组 × 0x24 槽）；② 与 0x7D96A8 的 Enter/Init 成对使用。
- 状态：**semantics_verified**（34B 全读；与 0x7D96A8 同布局）。

## 0x7D96A8  临界区槽获取（`EnterCriticalSection` + 惰性初始化）
- 事实（证据 `frontier_batch_049.txt`，95B 全读）：槽 = `[0xB78A80][idx>>5] + (idx&0x1F)*0x24`（0x20 组 × 0x24 字节槽）；`槽+8` 是初始化标志（0 = 未初始化）→ 锁 `0x7CD9F5`（0x11 锁）内 `InitializeCriticalSection(槽+0xC)` + 标志置 1（双检，防竞态）；再 `EnterCriticalSection(槽+0xC)` 获取锁。
- 断言：① 惰性初始化（首次进入前 `InitializeCriticalSection`）；② 双检 + 锁内初始化防竞态；③ 返回时已持有槽锁（与 0x7D9707 的 Leave 成对）。
- 状态：**semantics_verified**（95B 全读；`0x7CD9F5`/`0x7CDA56` 锁原语已登记 — batch 020）。
- 备注：**qsort 槽锁族**——句柄表（0x7D544A）与 qsort（0x7C8B48）均用此槽锁模式（双检+惰性初始化），形成两个独立调用链，共享同一槽管理器。

## 0x7D168E  `_lconvfromf` 前置包装（per-thread CRT 状态 +8 偏移）
- 事实（证据 `frontier_batch_034.txt`）：9B。`FUN_007d140b() + 0x8`（per-thread CRT 状态结构 +8 偏移）；调用者 = 7 个高扇入 CRT 错误路径。
- 断言：① 返回 per-thread CRT 状态结构 +8 指针；② 与 0x7D1697（+0xC）同族，偏移对应不同字段。
- 状态：**semantics_verified**（9B 全读；`0x7D140B` 已登记 — batch 008）。

## 0x7D482C  CRT locale / 控制台运行模式切换
- 事实（证据 `frontier_batch_020.txt`）：386B。`param_2 ∈ {3,4}`（LC_CTYPE=3 / LC_NUMERIC=4）恒失败返回 -1 并置 errno=0x16。`param_1 ∈ {0x02,0x15,0x16,0x0F}`（LC_TIME / LC_COLLATE / LC_MONETARY / LC_MESSAGES）→ 首次调用 `SetConsoleCtrlHandler(&0x7D49AE,1)`（`DAT_00b782ec` 幂等），然后写入 4 个 `DAT_00b782{dc,e0,e4,e8}` 槽并返回旧值。`param_1 ∈ {0x08,0x04,0x0B}`（LC_ALL / LC_TIME 别名 / LC_COLLATE 别名）→ `FUN_007d140b` 取 locale 对象，若分类表指针 `+0x50 == &DAT_0087cb20`（哨兵）就 `_malloc(DAT_0087cba0)` 复制分类表；`0x7D4B92` 按 key 查表并把 `+8` 槽写入 `param_2`，返回首个不匹配的旧值。全局锁守护 `0x7CD9F5` / `0x7CDA56`。
- 断言：①`param_2 ∈ {3,4}` 恒返回 -1 + `errno=0x16`；② `SetConsoleCtrlHandler` 只在首次调用（`DAT_00b782ec==0` 时）且失败映射到 `dos_error=GetLastError` 后返回 -1；③ 分类表首次使用时从 `DAT_0087cb20` 深拷贝到 heap（不共享），后续调用直接修改副本；④ 设置成功的返回值是**旧值**（不是新值）。
- 状态：**semantics_verified**（batch 057 升级：386B 完整汇编 + 反编译二次全读，与 batch 020 结论一致无漂移——`param_2∈{3,4}` 恒 -1/errno=0x16；`param_1∈{2,0x15,0x16,0xF}` SetConsoleCtrlHandler 幂等 + 四槽写回旧值；`param_1∈{8,4,0xB}` locale 对象分类表哨兵→深拷贝 + `0x7D4B92` 查表写 `+8` 槽。**下游全部闭合**：`0x7D4B92` 记录扫描 batch 021 verified、`0x7D140B` locale 对象查询 batch 008、`0x7D0A20` memmove batch 031、`_malloc` batch 020。）
- 备注：游戏逻辑几乎不触发 locale 分类路径；断言只保留"哨兵→拷贝"与"返回旧值"两条，避免误伤默认 CRT 行为。

## Heap 内部 —— 入口行为层断言

> **策略**：`0x7CF7BD` / `0x7CFB13` / `0x7CFFC8` / `0x7CF7E8` / `0x7CFE1C` / `0x7CFECD`
> 六个函数是 MSVC 6.0 静态 CRT 的**内存分配器内核**——按"边界标签 + 尺寸类位图 +
> 空闲链"实现。它们不是游戏逻辑，现代 CRT 天然满足其**对外行为**（对齐 / 失败回退 /
> 释放合并 / 段成长），**不逐字节结构复刻**。以下条目只登记"入口层面的可验证断言"，
> 断言由 2026 CRT + 现代 OS 隐式满足即可，除非某条对拍游戏逻辑必需。

### 0x7CF7BD  段查找（segment containment）
- 事实：`frontier_batch_021.txt`。遍历段表 `[0x87c584..0xb78b8c]`，返回包含 `param_1 - 4` 的段记录（段 `+0xC` 为 address、`+0x14` 为下一段哨兵）。
- 断言：给任意用户指针 `p`，返回包含 `[p-4, p-4+0x100000)` 的唯一段记录指针；无匹配 → 返回哨兵（NULL/末端）。
- 状态：**semantics_verified_partial**（`find_heap_segment_legacy` 已 model；仅段查找行为，非字节结构复刻。**batch 063 策略注记**：heap 内核按"行为断言层；不做结构复刻"——边界标签+尺寸类位图布局断言由 `0x7D0F45` 入口五行为断言闭合（batch 056）覆盖，本条只登记段查找子行为。）

### 0x7CFB13  分配/切块主循环
- 事实：`frontier_batch_022.txt`。`aligned=(size+0x17)&~0xF`；尺寸类 = `min(aligned/16-1, 0x3F)`（低 5 位 = <32、高 5 位 = ≥32）；按位图在段表内找空闲类（先"当前段"再全局再"新段"三级），必要时调 `0x7CFE1C` 造段、`0x7CFECD` 提交子段；找到后走 `0x7CFFC8` 的原地/切分路径；找不到则返回 NULL。段记录 `+0`/`+4` 是低/高段位图。
- 断言：① 对齐 16B；② 尺寸类按 16B 分档（`<32` 存 `+0x44`、`≥32` 存 `+0xC4`）；③ 三级段选择顺序（当前 → 全局 → 新段）；④ 段表 `DAT_00b78b8c` 每次分配后被更新为命中段。
- 状态：**semantics_verified_partial**（`size==0` 语义与失败回退已按 0x7D0F45 入口验证；`_cfb13` 内部逐字节结构不复刻。**batch 063 策略注记**：heap 内核按"行为断言层；不做结构复刻"——边界标签+尺寸类位图布局断言由 0x7D0F45 入口五行为断言闭合覆盖。）

### 0x7CFFC8  原地切分（coalesce-or-grow）
- 事实：`frontier_batch_022.txt`。读用户块 `+0xC` 的段基址算索引；若"块尾 + 块长 < 段顶" 且"后邻空闲链为空" 则从后邻切分，把新尾链写回；否则回退到 `0x7CFB13` 重新分配。
- 断言：① 原地扩展仅在"后邻无空闲"且"切分后剩余 ≥ 1 空档"时成功；② 失败必然触发重新分配；③ 边界标签（用户 `user-4` 与后邻 `next-8`）同步更新。
- 状态：**semantics_verified_partial**（行为断言层；不做结构复刻。**batch 063 策略注记**：heap 内核按"行为断言层；不做结构复刻"——边界标签+尺寸类位图布局断言由 0x7D0F45 入口五行为断言闭合覆盖。）

### 0x7CF7E8  释放（合并 + 段回收）
- 事实：`frontier_batch_022.txt`（811B）。读用户块 `user-4` 边界标签；若"前邻空闲"（`bit 0==0`）则与前邻合并；若"后邻空闲"则与后邻合并；合并后若尺寸类跨越槽位则重挂到对应 `+0x44/+0xC4` 空闲链。段内**所有**槽全部空闲（`+0x100` 处 `DAT_00b78b90` 指向本段）时调 `VirtualFree(addr+32, 0x8000, MEM_RELEASE=0x4000)` 归还整段，并清对应位图位。
- 断言：① 前后空闲邻居必须双向合并（`piVar9[1]=piVar3[..]`）；② 尺寸类跨越槽位 → 重挂到正确的 `+0x44` / `+0xC4` 双端链表；③ 段内**所有**槽全空 → `VirtualFree(..., MEM_RELEASE)`（不只是 COMMIT 释放）；④ 归还段的段表 `+0x100` 哨兵位被清。
- 状态：**semantics_verified_partial**（合并算法行为断言；不做结构复刻。**batch 063 策略注记**：heap 内核按"行为断言层；不做结构复刻"——边界标签+尺寸类位图布局断言由 0x7D0F45 入口五行为断言闭合覆盖。）

### 0x7CFE1C  段创建（1MiB 预留）
- 事实：`frontier_batch_022.txt`。段记录数组 `[0xb78b84..0xb78b94)` 满容量时先 `HeapReAlloc(heap,0,old,(count*5+0x50)*4)` 扩容 `+16` 条；再 `HeapAlloc(heap,HEAP_ZERO_MEM=8,0x41C4)` 分配段元数据；再 `VirtualAlloc(NULL,0x100000,RESERVE=0x2000,PAGE=4)` 预留 1MiB；段记录 `+0x00=flags=0`、`+0x04=free_count=0`、`+0x08=bitmap=0xFFFFFFFF`、`+0x0C=address`、`+0x10=metadata`；段元数据 `+0x00=0xFFFFFFFF`（初始全部空闲）；成功 `count++` 并返回记录，失败（reserve 失败）释放 metadata 并返回 NULL。
- 断言：① 段记录尺寸 0x14 字节（5 dword），数组扩容步长 `+16` 条、新容量 `(count+0x50)*4` 字节；② metadata 固定 0x41C4 字节、`HEAP_ZERO_MEM`；③ 地址预留 1MiB / `MEM_RESERVE` + `PAGE_NOACCESS=4`；④ 段元数据首 dword 初始化为 0xFFFFFFFF（"全部 64 槽空闲"）；⑤ reserve 失败必须回滚 metadata 分配。
- 状态：**semantics_verified**（`create_heap_segment_legacy` 已 model + 两个 gtest 案例覆盖扩容路径与 reserve 失败回滚路径；证据 `test_lowlevel.cpp:93-141`；`HeapAlloc`/`HeapReAlloc`/`VirtualAlloc` Windows API 按 CRT 断言层不验证——现代 Windows 行为一致。）

### 0x7CFECD  子段提交（32K commit + 初始双端链）
- 事实：`frontier_batch_022.txt`（251B）。基于段 `+0x08`（free_count）的比特数算段级偏移 `idx*0x204+0x144`；把 `idx` 个双端链槽 `[+8,+0x1F8]` 初始化为自环；再 `VirtualAlloc(seg_addr+idx*32K, 32K, COMMIT=0x1000, PAGE=4)` 提交子段；提交成功后遍历 32K 内每 0x400 字节打"初始空档 + 边界标签 0xff0 + 自环指针"；段 `+0x043`（已用字节计数）+1；首次提交置段 `flags|1`；段 `+0x08` 位图清对应位。提交失败返回 -1。
- 断言：① 每次提交固定 32K；② 提交成功后 32K 内被切成 8 个 0x400 边界的空档（`+0=+4=next-8=自环`、`+0xFE8=+0xFE0=0xff0`）；③ 段级已用槽计数严格 +1；④ 首次提交触发 `flags|1`；⑤ 失败返回 -1 且不递增段计数。
- 状态：**semantics_verified_partial**（行为断言层；不做结构复刻。**batch 063 策略注记**：heap 内核按"行为断言层；不做结构复刻"——边界标签+尺寸类位图布局断言由 0x7D0F45 入口五行为断言闭合覆盖。）

## 0x7D4C00  优化前向字符串拷贝（`strcpy` 词级变体）
- 事实（证据 `frontier_batch_035.txt`，224B 全读）：两阶段——**长度**：对齐 `src&3` 逐字节 + 4 字 NUL 检测（`0x7EFEFEFF` 魔术 + `0x81010100` 掩码）找 NUL；**拷贝**：对齐 `dst&3` 逐字节 + 4 字块拷（边拷边检 NUL，命中终止）。返回 `param_1`（dst）。调用者 `_init_namebuf` 等 6 个 CRT 函数。
- 断言：① 等价于 NUL 终止的 `strcpy`；② 4 字块检 NUL（word-at-a-time）；③ 返回 dst。
- 状态：**semantics_verified**（224B 全读；与 `0x7D4BF0` string_copy_legacy 同族）。

## 0x7D0703  `fgetc` 单字节读取（流状态机）
- 事实（证据 `frontier_batch_035.txt`，220B 全读）：读 `FILE+0xC`（stream 标志）；`&0x83==0 || &0x40!=0` → 返回 -1（非法流）；`&0x2`（已读 EOF/error 位）→ 置 `|0x20` 并返回 -1；否则 `|0x1`（占用）→ 若未初始化 buffer（`&0x10C==0`）→ `0x7D7710`（初始化流状态，已登记 batch 006），已初始化 → 用 `FILE+0x8`（保存的指针）；`0x7D07DF(FILE+0x18, FILE+0x8, FILE+0x10)` 读回调 → `FILE+0x4`=读入字节；EOF/错误（0 或 -1）→ `|0x10`、`FILE+0x4=0`、返回 -1；成功 → 若 `FILE+0x10==-1`（未缓冲）用 `0x87C928` 槽，否则按 `idx>>5`+`0x24` 槽布局查 `槽+0x4 & 0x82==0x82` → `|0x2000`（该文件句柄的文本标志位）；`FILE+0x18==0x200 && &0x8 && !(&0x400)` → 缓冲上限从 0x200 升到 0x1000；最终 `*FILE` 指向读入字节、`FILE+0x4` 自减、返回该字节。
- 断言：① 非法/错误流恒返回 -1；② `0x7D07DF` 是底层读回调；③ 文本标志 `0x2000` 按句柄槽查表；④ 未缓冲（`0x10==-1`）用静态槽 `0x87C928`。
- 状态：**semantics_verified**（batch 062 闭环：220B 全读；下游 `0x7D07DF` batch 056 已 verified（101B 全读；与 0x7D0D55 同构模板的两参/三参实例）——原 partial 理由"底层读回调未读"消除；六行为断言全闭合。）

## 0x7D2F41  `exception::~exception`（析构）
- 事实（证据 `frontier_batch_035.txt`，22B 全读）：Ghidra 库匹配 "Visual Studio 2003/2005 Release"。虚表指针置 `&PTR_FUN_007f990c`；`this+8` 非 0 → `0x7C8B3D(this+4)`（`_free` 释放消息字符串）；否则直接返回。
- 断言：① 析构只释放 `this+4` 的消息字符串（若 `this+8` 标志非 0）；② 虚表指针先复位为 `0x7F990C`（防双重析构）。
- 状态：**semantics_verified**（22B 全读；`0x7C8B3D` = `_free` 已登记）。

## 0x7D9EF8  单字节缓冲流写（`putc` 内核）
- 事实：`frontier_batch_017.txt`，110B 全读。输入为字节值与 FILE-like 流槽：`param_1 == -1` 直接返回 -1；流状态要求 `flags&1`，或 `(flags&0x80) && !(flags&2)`；缓冲未初始化时调用 `0x7D7710`。当当前指针等于缓冲基址且计数为 0 时先前移一字节；当前指针等于缓冲尾且计数非 0 时失败。普通模式回退一字节写入；`flags&0x40` 模式回退后只比较已有字节，失配恢复指针并返回 -1。成功递增计数，清 `flags&0x10`、置 `flags&1`，返回输入低字节。
- 断言：① 无效值、不可写/EOF 状态返回 -1；② 空缓冲通过初始化和指针前移后写入；③ 普通模式写一个低字节；④ `0x40` 模式只校验、不覆盖，失配不增加计数；⑤ 成功状态为计数 +1、清 bit4、置 bit0。
- 状态：**semantics_verified_partial**（110B 汇编/反编译已读；C++ `buffered_put_byte_legacy` + 3 项单测已通过，但 FILE 指针布局与原版 CRT ABI 仍未做进程内动态对拍。）

## 0x7D2169  单字节缓冲流读（`getc` 内核）
- 事实：`frontier_batch_018.txt`，26B 全读。输入为 `byte**` 当前指针与相邻的有符号剩余计数：先执行 `remaining--`；结果仍非负时读取 `*current` 的无符号字节、`current++` 并返回该字节；结果为负时将原始二元状态整体转发给 `0x7D0703` refill/read 分发并直接返回其结果。
- 断言：① 每次命中缓冲先消耗一个剩余计数；② 未耗尽路径只读当前字节并前移指针；③ 耗尽路径不自行读取或修复指针，完全交给下游 refill；④ 下游返回值原样透传。
- 状态：**semantics_verified_partial**（26B 汇编/反编译已读；C++ `buffered_get_byte_legacy` + available/exhausted 两项单测已通过，但与原版 `FILE`/句柄槽布局的动态 ABI 对拍仍未完成。）

## 0x7D151E  `_wtof` 宽字符字符串 → double（解析器下游）
- 事实（证据 `frontier_batch_036.txt`，127B 全读）：`0x7D9872` 解析器 → 12 字节浮点 + 状态码；状态码 `&0x4`（正常）→ `0x7D7C1B`（12B→8B double 转换）；其他状态 → `EDI=0x200`（`ERANGE` 标志）；状态码 `&0x2` 或 `0x7D7C1B` 返回 1 → `EDI|=0x80`（下溢）；`&0x1` 或返回 2 → `EDI|=0x100`（上溢）；写 `param_1+0` = 状态码、`param_1+4` = 已消费字符数、`param_1+0x10` = double。
- 断言：① 状态码位组合 = `0x80`（下溢）| `0x100`（上溢）| `0x200`（解析器正常但需转换）；② `param_1+4` = `消费长度`（指针差）；③ `param_1+0x10` = double。
- 状态：**semantics_verified**（batch 061 闭环：127B 全读；下游 0x7D7C1B 12B→8B 转换 batch 038 已 verified——原 partial 理由"0x7D7C1B 未读"消除；状态码位组合 + 消费长度 + double 输出三断言闭合。）

## 0x7D7CC1  宽字符字符串 → 8 字节 float（解析器下游）
- 事实（证据 `frontier_batch_036.txt`，45B 全读）：`0x7D9872` 解析器 → 12 字节 + 状态码 → `0x7D7C1B` 12B→8B float 转换（覆盖 `param_1` 指针）。与 0x7D151E 同族但无状态码输出。
- 断言：① 等价于 `_wcstod` 无状态码版本；② 结果通过指针 `param_1` 返回。
- 状态：**semantics_verified**（batch 061 闭环：45B 全读；下游 0x7D7C1B batch 038 已 verified——原 partial 理由消除。）

## 0x7D7D1C  宽字符字符串 → 8 字节 float（解析器下游，alternate 变体）
- 事实（证据 `frontier_batch_036.txt`，45B 全读）：与 0x7D7CC1 同构但用 `0x7D7C31`（12B→8B 的 alternate 转换，可能处理不同舍入/精度模式）而非 `0x7D7C1B`。
- 断言：① 等价于 0x7D7CC1 但用 alternate 转换；② 结果通过指针 `param_1` 返回。
- 状态：**semantics_verified**（batch 061 闭环：45B 全读；下游 0x7D7C31 batch 038 已 verified——原 partial 理由消除。）

## 0x7D45A1  十进制两位数格式化守卫（`-1` 直通）
- 事实（证据 `frontier_batch_037.txt`，25B 全读）：`(short)param_1 == -1` → 直接返回（不写入）；否则 `0x7DA638(param_1, param_2)` 转发。
- 断言：① `param_1 == 0xFFFF`（句柄/哨兵）时无操作；② 否则转发到 `0x7DA638`。
- 状态：**semantics_verified**（25B 全读；`0x7DA638` 已读 batch 042，下游闭合）。

## 0x7D78B5  尾数 sticky 位检测（12B→8B 舍入辅助）
- 事实（证据 `frontier_batch_043.txt`，73B 全读）：入参 = 12B 尾数指针 + 位计数 `param_2`。语义：① 掩码位 `0x1F - (param_2%0x20)` 以下（更低权重的低位）非 0 → 返回 0（**存在 sticky 位，舍入需向上**）；② 否则自 `param_2/0x20 + 1` 起检查后续 dword，任一非 0 → 返回 0；③ 全部为 0 → 返回 1（**sticky 全零**）。即"检查从位 `param_2` 以下直到尾数最低位是否全零"。
- 断言：① sticky 检测（round bit 以下低位全零 → 1）；② 供 `0x7D7954` 决定是否进位；③ 纯位逻辑、无浮点。
- 状态：**semantics_verified**（73B 全读；纯位运算。）

## 0x7D78FE  尾数进位加法（12B→8B 舍入辅助）
- 事实（证据 `frontier_batch_043.txt`，86B 全读）：入参 = 12B 尾数指针 + 位计数 `param_2`。语义：在 `param_2/0x20` dword 的掩码位 `0x1F - (param_2%0x20)` 上加 1（`0x7DB066` carry-add）；若产生进位（返回非 0）则向低一个 dword 加 1（`0x7DB066(*dword, 1, dword)`），并沿低位 dword 链继续进位直到进位为 0 或到达 0。
- 断言：① 从指定 bit 加 1 的**多精度进位链**；② 进位跨越 dword 边界向上传播；③ 纯位逻辑、无浮点。
- 状态：**semantics_verified**（86B 全读；`0x7DB066` 已登记 batch 031。）

## 0x7D777D  加锁宽字符→字节转换包装（与 0x7D9D7F 同构）
- 事实（证据 `frontier_batch_043.txt`，89B 全读）：与 `0x7D9D7F` **逐字节同构**——同一份"引用计数 + 条件锁"模板：`InterlockedIncrement(&[0xB78BA4])`；`[0xB78BA0]!=0` → 递减 + `0x7CD9F5(0x13)` 锁内执行；否则无锁执行 `0x7D77D6(param_1, param_2)`；结束后解锁/递减。
- 断言：① 与 `0x7D9D7F` 同构（宽→字节方向）；② 底层 `0x7D77D6`（单字节/转换模式字符输出）；③ 同一锁 key `0x13` 与同一引用计数槽。
- 状态：**semantics_verified**（89B 全读；`0x7D77D6` 已登记 batch 009 及 `_REIMPL` `wide_char_to_byte`。）

## 0x7D8E22  double 归一化 + 指数提取（`frexp` 族，含 x87 重建）
- 事实（证据 `frontier_batch_043.txt`，193B 全读）：输入 64 位 double + 输出指数 `param_3`：
  ① `value == 0.0`（比较 `[0x7E2800]`）→ `*param_3=0`、返回 0.0；
  ② 指数为 0 且尾数非 0（**非规格化数**）→ 循环左移尾数直到最高位（bit `0x10`）置位（`iVar4` 从 `-0x3FD` 递减），清隐式位（`&0xFFEF`），按符号补符号位（`|0x80` 到符号字节），经 `0x7D8D3B`（x87 80 位重建）回载为 double → 返回；
  ③ 常规数 → 经 `0x7D8D3B` 重建后返回，`*param_3 = (exp16 & 0x7FF) - 0x3FE`。
- 断言：① 等价于 `frexp`（返回 `value = mantissa * 2^exp`，mantissa ∈ [0.5, 1)）；② 非规格化数归一化路径（含符号处理）；③ 依赖 `0x7D8D3B`（x87 80 位重建，batch 014/037 已登记）。
- 状态：**semantics_verified**（batch 043 原 partial 记为"[0x7E2800] 常量未 dump"；已由 batch 052 dump 确认为 0.0，见下方同名节升级）

## 0x7D58C8  两位数十进制输出（`%02d` 基元）
- 事实（证据 `frontier_batch_037.txt`，40B 全读）：`param_2/10` → 十位 `+0x30` 写 `param_1[0]`；`param_2%10` → 个位 `+0x30` 写 `param_1[1]`；返回 `param_1+2`。`param_2` 负值经 `CDQ`/`IDIV` 截断。
- 断言：① 等价于 `snprintf(buf, 3, "%02d", v)` 的核心行为；② 返回写后指针。
- 状态：**semantics_verified**（40B 全读，纯算术）。

## 0x7D8D3B  x87 80 位重建（`_ftol` 逆操作：指数 + 符号组装）
- 事实（证据 `frontier_batch_037.txt`，41B 全读）：输入 64 位 double + 16 位指数；`新指数 = param_2 + 0x3FE`；`高位 = 新指数 << 4 | (param_1._6_2_ & 0x800F)`；`FLD/FSTP` 经 x87 栈把重组的 80 位模式回载为 `float10`（long double）返回。**已登记为 `semantics_verified_partial`（batch 014）**——本批次闭合为完整汇编验证：重组位模式 `(exp+0x3FE)<<4 | (sign+低 4 位尾数)` 是 x87 80 位格式的符号+指数+尾数高位拼接。
- 断言：① 输出 = 80 位 x87 扩展精度（15 位指数 + 64 位尾数）；② 指数偏移 0x3FE 保持数值不变；③ 尾数高 4 位从源 double 的符号+尾数高位继承。
- 状态：**semantics_verified**（41B 全读；与 batch 014 登记一致）。

## 0x7D7C1B  12B→8B float 转换（表 0x87D3C0 变体）
- 事实（证据 `frontier_batch_038.txt`，22B 全读）：薄转发 `0x7D7AAF(param_1, param_2, &DAT_0087d3c0)`——第三个参数是**转换表指针** `0x87D3C0`；真正的 12 字节→8 字节转换在 `0x7D7AAF`（batch 041 已闭合）。
- 断言：① 等价于 12 字节浮点 → 8 字节浮点转换（表驱动舍入）；② 表 `0x87D3C0` 决定舍入/精度模式。
- 状态：**semantics_verified**（22B 全读；下游 `0x7D7AAF` 已读，batch 041）。

## 0x7D7C31  12B→8B float 转换（表 0x87D3D8 变体）
- 事实（证据 `frontier_batch_038.txt`，22B 全读）：与 0x7D7C1B 同构，表指针改为 `&DAT_0087d3d8`（0x87D3C0 + 0x18，相邻第二张表）。
- 断言：① 与 0x7D7C1B 同构，仅表不同；② 表 `0x87D3D8` 可能对应不同舍入模式（向零/最近）。
- 状态：**semantics_verified**（22B 全读；下游 `0x7D7AAF` 已读，batch 041）。

## 0x7D7CEE  宽字符字符串 → 8 字节 float（解析器下游，第三变体）
- 事实（证据 `frontier_batch_038.txt`，46B 全读）：`0x7D9872(local_10, &param_2, param_2, 1, 0, 0, 0)`（param_4=1，区别于 0x7D7CC1/0x7D7D1C 的 param_4=0——`0x7D9872` 的 param_4 是"数字串前端后允许转换"标志）→ `0x7D7C47(local_10, param_1)`（第三张表变体，batch 041 已闭合）。
- 断言：① param_4=1 是解析器"正常路径"标志（0x7D7CC1/0x7D7D1C 的 param_4=0 走 12B→8B 表驱动转换，0x7D7CEE 走 0x7D7C47 自归一化）；② 转换经 `0x7D7C47`。
- 状态：**semantics_verified**（batch 061 闭环：46B 全读；param_4=1 语义已修正为"数字串前端后允许转换"标志（与 0x7D7CC1/0x7D7D1C 的 param_4=0 区分解析器路径）；0x7D7C47 batch 041 已 verified。）

## 0x7D7AAF  12B→8B 浮点转换核心（表驱动，覆盖 0x7D7AFF）
- 事实（证据 `frontier_batch_041.txt`，364B 全读；实际函数入口 `0x7D7AAF`，`0x7D7AFF` 位于函数体中段）：
  接受 12 字节扩展精度结构（offsets：`+0`=低字尾数高16位、`+6`=中32位尾数、`+A`=指数与符号位）与**转换表** `param_3`（表字段：`[+0]`=最小指数上界、`[+4]`=最大指数上界、`[+8]`=正常化偏移（`0x80000000` 掩码相关）、`[+0xC]`=指数偏移常量、`[+0x10]`=输出位宽（`0x40`=double/`0x20`=float）、`[+0x14]`=表基址指针）。
  分支语义：① `exp16 == 0x3FFF`（x87 非数）→ 检测尾数是否为全 0（0x7D7A07）→ 若全 0 则返回特殊标记，否则零化后走零输出路径；② 常规路径：`0x7D79E0` 打包 → `0x7D7954(尾数, 表偏移)` 做尾数归一化 → 按 `exp16 - 0x3FFF` 与表 `[+0]/[+4]/[+8]` 比对决定舍入方向 → 上溢/下溢/正常三种分支写尾数；③ 出口 `mantissa<<=(0x1F - exp_adj)` + `sign<<0x1F` + 原低位拼接 → 按表位宽写 `param_2`（double 写 2 个 dword、float 写 1 个 dword）。
  返回码：`0`=正常/NaN/零路径（`0x7D7BDB`）、`1`=上溢路径（`0x7D7BC4`）、`2`=下溢路径（`0x7D7B91` 出口）。
- 断言：① 表参数决定 double/float 位宽与舍入方向；② 12B→8B 转换依赖表 `[0x87D3C0]/[0x87D3D8]` 两张具体表的常量（未读表字节）；③ NaN（exp=0x3FFF）特殊路径需保证不产生 0/0=NaN 之外的语义；④ 返回码 0/1/2 与调用方（`0x7D7C1B`/`0x7D7C31` 薄转发、`0x7D7CEE` 第三变体）配合决定 ERANGE 状态。
- 状态：**semantics_verified**（batch 061 闭环：364B 全读；舍入辅助 0x7D7954/0x7D78B5/0x7D78FE 全部 verified；表常量 `[0x87D3C0..0x87D3F8)` 属 data 层（两张表 0xC×6 字段），按"行为断言"已捕获——`lockstep 做 ULP 对拍时再 dump 表字节`，不阻塞语义登记。）
- 备注：**strtod 12B→8B 链最末段已闭合**——`0x7D9872`（十进制解析）→ `0x7DB140/0x7DC0F6`（数字→12B）→ `0x7DBED6/0x7DB087`（12B 算术）→ `0x7D7AAF`（12B→8B 舍入）。按三层策略为"须对拍"行为点（lockstep 需 strtod 位模式一致）；CLI 侧仍用现代 CRT strtod，建议后续做 ULP 对拍决定是否需重写整链。

## 0x7D7C47  12B→8B float 转换（第三张表变体，直接调 0x7D7954）
- 事实（证据 `frontier_batch_041.txt`，122B 全读）：与 `0x7D7AAF` 同族但**不经过表参数**——直接以 `0x40`（double 位宽标志）调 `0x7D7954` 做尾数归一化；归一化返回非 0 时置 `mantissa=0x80000000` 且 `exp16 += 1`（上溢补偿）；`exp16==0x7FFF` 时置返回码 1（下溢）；写出：`out[0]=low32 & 0x7FFFFFFF | sign<<31`、`out[4]=mid32`、`out[8]=exp16`（12B 原样回写 + 尾数重打包）。
  与 `0x7D7AAF` 的差异：无表驱动、无分支舍入、直接归一化 → 更像"12B 自归一化 + 双字打包"的辅助步骤，而非完整 double 舍入。
- 断言：① 无表参数、单一路径；② 归一化返回非 0 触发 `mantissa=0x80000000, exp16++`（上溢补偿语义）；③ 输出结构 = 原 12B 结构 + 尾数重排；④ 调用方 `0x7D7CEE`（宽字符→8B 第三变体）依赖此步。
- 状态：**semantics_verified**（batch 061 闭环：122B 全读；尾数归一化核心 0x7D7954 batch 042 已 verified——原 partial 理由"0x7D7954 内部未读"消除；返回码语义与 0x7D7AAF 不同已在事实中注明。）

## 0x7D7A22  尾数左移 + 低位清零（12B→8B 转换辅助）
- 事实（证据 `frontier_batch_041.txt`，141B 全读；`0x7D7A47` 位于函数体中段）：`param_2 / 0x20` = dword 移位计数、`param_2 % 0x20` = 字内移位量。循环 3 次对 `param_1[0..2]` 做左移（每个 dword 保留移位溢出的低位到下一次循环）；然后对 `param_1[3..4]`（2 个 dword）按计数写 0 或回搬尾部 dword（当 `count < 2` 时保留尾部 `count` 个 dword，其余清 0）。**总长度 5 dword = 20 字节**（即 12 字节扩展尾数的 128 位扩展表示）。
- 断言：① 左移 `param_2` 位、按 dword 循环处理低位溢出；② 尾 2 dword 清零（12B 扩展→更高精度）；③ 与 `0x7D7AAF` 的 `[0x87D3C0]` 表偏移配合（表字段 `[+3]`/`[+4]` 作为移位常量传入）。
- 状态：**semantics_verified**（141B 全读；纯位运算 + dword 循环，无外部依赖。）
- 备注：`0x7D7AAF` 内 `[+3]`/`[+4]` 表字段即此移位量，本函数是 12B→8B 舍入的"尾数重对齐"步骤。

## 0x7D7954  尾数舍入（12B→8B 链归一化核心）
- 事实（证据 `frontier_batch_042.txt`，140B 全读）：入参 = 12B 尾数指针 + 位计数 `param_2`。语义：① 定位到 `param_2/0x20` dword、掩码位 `0x1F - (param_2%0x20)`；② 若该位为 1（**round bit 置位**）：先 `0x7D78B5(ptr, param_2+1)` 检查更高位（sticky/guard），为 0 时再 `0x7D78FE(ptr, param_2-1)` 做进位（返回进位标志到 `local_8`）；③ 无论是否进位，清除该 round bit；④ `param_2/0x20+1 < 3` 时把后续 dword 清零（截断低位）。
- 断言：① **round-to-nearest 语义**（round bit 置位 + 无 sticky → 向上进位；否则截断）；② 该函数是 `0x7D7AAF`（表驱动转换）与 `0x7D7C47`（多精度打包）共用的**舍入原语**；③ `0x7D78B5`/`0x7D78FE` 为相邻位检查/进位辅助（batch 043 已闭合）。
- 状态：**semantics_verified**（140B 全读；`0x7D78B5`/`0x7D78FE` 已读 batch 043；保留 round-to-nearest 行为断言。）
- 备注：**闭合 strtod 链最后一个未知行为点**——`0x7D7AAF → 0x7D7954` 的舍入决定 double/float 的最低有效位模式。retained 隐式规则：round-to-nearest 是 IEEE 754 默认，但全局 `_control87` 设了 chop（向零截断），**该舍入不读 FPCW**（纯位逻辑），即双经此链转换恒为 round-to-nearest，不受全局 chop 影响——lockstep 时须按此断言对拍。

## 0x7D79E0  12B 尾数拷贝（3 dword 打包）
- 事实（证据 `frontier_batch_042.txt`，27B 全读）：循环 3 次把 `param_2[0..2]` 拷到 `param_1`（前向 4B/步）。纯 `memcpy(3 dword)`。
- 断言：12B 尾数整体拷贝（无字节序/移位干预）。
- 状态：**semantics_verified**（27B 全读，纯拷贝）。

## 0x7D79FB  12B 尾数清零
- 事实（证据 `frontier_batch_042.txt`，12B 全读）：3 个 `STOSD` 把 12 字节置 0。
- 断言：12B 尾数整体清零（下溢/零路径用）。
- 状态：**semantics_verified**（12B 全读，纯清零）。

## 0x7D7A07  12B 尾数全零检测
- 事实（证据 `frontier_batch_042.txt`，27B 全读）：依次检查 3 个 dword 是否全 0；全 0 → 返回 1，任一非 0 → 返回 0。
- 断言：返回 1 ⇔ 12B 尾数全零（`0x7D7AAF` 的 NaN/零分支用它区分 x87 非数与零值）。
- 状态：**semantics_verified**（27B 全读，纯比较）。

## 0x7D9FCA  底层宽字符流写（非缓冲路径，`_putwc` 底层）
- 事实（证据 `frontier_batch_053.txt`，295B 全读；`0x7D2FC8` 显式调用点闭合）：`(unsigned short 宽字符, 流槽)→uint`。与 `0x7CE18D`（窄字符）**逐字节同构**——同一份"缓冲/错误状态位 + 底层 `0x7D0D55` 写 + `0x7D7638` 定位刷新"模板，但计数步进为 2 字节：
  - 状态位检查 `槽+0xC & 0x82 | 0x40` 置位即返回 0xFFFF（0x82 = `_IOMYBUF|_IOAPPEND|_IOMODE_READ`?；`&0x40` = `_IOWIDE` 已走本函数不该再进）；
  - 缓冲路径：`[槽] - [槽+8]` 有空间 → 直写 `*(short*)[槽]` 并把 `[槽+4] -= 2`（剩余缓冲）；否则先 `[0x7D0D55(句柄, buf, n)]` flush 已写部分（`[槽+0x18]` 计数），再按句柄槽 `[槽+4]&0x20` 决定是否 `0x7D7638(句柄, 0, 2)`（**定位刷新 2 字节**）；
  - 非缓冲路径：`[槽+0xC]&0x108` 位清零 → 直接 `0x7D0D55(句柄, &字符, 2)` 写；否则走缓冲路径；
  - 失败置 `槽+0xC |= 0x20`（`_IOERROR`）返回 0xFFFF；成功返回 `param_1 & 0xFFFF`（宽字符原值）。
- 断言：① 与 `0x7CE18D` 同构（差集仅"步进 2 字节 / `0x7D7638` 第三参 2"）；② 宽字符写入走窄缓冲时**按小端 2 字节序**写（`MOV WORD [EAX],BX`）；③ 定位刷新调 `0x7D7638(句柄, 0, 2)` 更新文件位置；④ 失败置错误位返回 0xFFFF。
- 状态：**semantics_verified**（295B 全读；`0x7D0D55`/`0x7D7638`/`0x7D7754`/`0x7D7710`/`0x7D95BF`/`0x7D96A8`/`0x7D9707`/`0x7D93A1` 全部已登记——**宽/窄写原子链完整闭合**。）
- 备注：**升级 batch 052 `0x7D2FC8` 的调用点闭合**——宽引擎写路径 0x7D21BE → 0x7D290B → 0x7D2FC8 → 0x7D9FCA（宽流底层）+ 0x7CE18D（非宽流转窄），全链贯通。

## 0x7DCD9C  无效参数异常抛出（`__RTTI 0x80F0D0`）
- 事实（证据 `frontier_batch_053.txt`，36B 全读；`0x7DC720` 下游）：`(param_1, param_2)` → 先调 `0x7DCDC0(param_1, param_2, 0)`（**参数解析/校验/格式化**），**随后无条件 `__CxxThrowException_8(local_14, &DAT_0080f0d0)`**——Ghidra 标注 `Warning: Subroutine does not return`（异常抛出点，函数不返回）。
- 断言：① `0x80F0D0` = `__RTTI::type_info_name` for `std::invalid_argument`/`invalid_parameter`——**C++ 标准异常身份**（MSVC 6.0 布局）；② 先跑 `0x7DCDC0` 生成异常对象 `local_14[16]`（16B payload），再抛；③ **调用链 0x7DC720（单参转发）→ 0x7DCD9C（抛异常）→ 0x7DCDC0（构造）→ 0x7D2F64（`__CxxThrowException_8`）**——**闭合批次 052 `0x7DC720` "API 身份未定" 备注**：它是"无效参数→抛异常"链的入口，`0x7Dcd9c` 是"两参数版本"。
- 状态：**semantics_verified**（36B 全读；`0x7DCDC0`/`0x7D2F64` 是 MSVC 6.0 CRT 标准异常基础设施，行为断言成立。）
- 备注：`0x7D2F64` = `__CxxThrowException_8`（16B 异常对象 + `type_info*`），`__RTTI 0x80F0D0` 是 `std::invalid_argument` 的 type_info 静态对象（Ghidra 已按 RTTI 结构识别）——**语义确定**，无需再追踪。

## 0x7DA638  流写字符（字节/宽字符双路径）
- 事实（证据 `frontier_batch_042.txt`，237B 全读）：与 `0x7DA638` 为流写入口（非 `0x7D45A1` 下游格式化链——该地址是独立写路径）：
  入口校验：`param_1==0xFFFF` 直接返回 `0xFFFF`（无效）；流槽 `param_2[3]` 状态位：`&1`（`_IOBUFFERED`）或（`&0x80` 且非 `&2`）。
  缓冲检查：`param_2[2]==NULL` → `0x7D7710`（流状态初始化）；`[param_2] == [param_2+8]`（缓冲满）且 `[param_2+4]==0`（无未写计数）→ 回绕/重置。
  字节路径（`&0x40` 未置位即非宽流）：按 `param_2[4]`（COOKIE）+`0x87C928` 定位文件槽表 `[0xB78A80]` 项（`param_2[4]>>5` + `(param_2[4]&0x1F)*0x24`）→ `[槽+4]&0x80`（写激活位）→ `0x7D777D(&param_1, param_1)`（底层写）→ 失败 errno=0x2A + 返回 `0xFFFF`；成功：写 2 字节（先高字节再低字节，`[param_2]` 指针递减——**字节流以小端序写 2 字节表示宽字符**）+ `[param_2+4] += 写字节数`；清 `0x10` 位（`_IOERROR`?）置 `0x1` 位；返回 `param_1 & 0xFFFF`。
  宽字符路径（`&0x40` 置位）：满时回绕；`[param_2+4] += 2`；`[param_2] -= 2`；`**param_2 = param_1`（**写入 1 个 wide char**）→ 返回该 wide char。
- 断言：① 流写双路径（宽流 `_IOWIDE`(0x40) 与窄流）；② 窄流把宽字符拆 2 字节**倒序**写（先高字节后低字节 = 小端）；③ 底层写失败 errno=0x2A（`EILSEQ` = "Illegal byte sequence"，Windows MSVC errno 表标准值）且返回 `0xFFFF`；④ 空缓冲/未初始化触发 `0x7D7710`。
- 状态：**semantics_verified**（237B 全读；`0x7D777D` 底层写、`0x7D7710` 已登记；errno 0x2A = `EILSEQ` 语义确定。）
- 备注：**升级 batch 042 的 semantics_verified_partial → semantics_verified**（errno 语义消除——`0x2A` 是 MSVC errno 表标准 `EILSEQ` 值，与 batch 052 `0x7D2FC8`/batch 053 `0x7D9FCA` 转换失败路径一致）。与 `0x7D9DDC`（`MultiByteToWideChar` 包装）共同构成宽字符 I/O 的写端。

## 0x7D9DDC  单字节 → 宽字符转换底层（`MultiByteToWideChar` 包装）
- 事实（证据 `frontier_batch_042.txt`，201B 全读）：`0x7D9D7F` 的底层（无锁）。语义：`param_2==NULL || param_3==0` → 返回 0；`*param_2==0` → 写 `L'\0'` 返回 0；`[0xB782A0]==0`（无 locale 表）→ 单字节直接提升 `*param_1=*param_2` 返回 1；否则查 `[0x87C590][c*2+1]&0x80`（lead-byte 位）：
  - 非 lead → `MultiByteToWideChar([0xB782B0], 9, str, 1, dst, dst!=NULL)` 成功返回 1；
  - lead → `[0x87C79C]` 为字符字节数（>1）：`param_3 >= 字节数` 则转换 `[0x87C79C]` 字节（成功返回字节数）；否则检查 `param_2[1]!=0` 且 `[0x87C79C] <= param_3` → 返回 `[0x87C79C]`（**无目标缓冲时返回所需字节数**语义）。
  失败路径：`0x7D168E()` = errno 槽写 `0x2A`，返回 `0xFFFFFFFF`。
- 断言：① lead-byte 感知（DBCS 双字节字符）；② 无目标缓冲时返回所需字节数（`MultiByteToWideChar` 的 `cbMultiByte` 语义）；③ 失败 errno=0x2A + 返回 -1；④ `[0xB782A0]` 无 locale 时退化单字节提升。
- 状态：**semantics_verified**（201B 全读；`0x7D168E` errno 槽已登记；`[0x87C590]` 表备用 `[0x87C59C]` 未读 — 低优先级。）

## 0x7DAE9F  MessageBoxA 惰性包装（user32 动态加载）
- 事实（证据 `frontier_batch_042.txt`，137B 全读）：`[0xB78580]==NULL` → `LoadLibraryA("user32.dll")` + `GetProcAddress` 取 `MessageBoxA`（→`[0xB78580]`）、`GetActiveWindow`（→`[0xB78584]`）、`GetLastActivePopup`（→`[0xB78588]`）；任一失败返回 0。后续调用：`hWnd = GetLastActivePopup(GetActiveWindow())`（任一步为 NULL 则 hWnd=NULL）→ `MessageBoxA(hWnd, param_3, param_2, param_1)`（返回按钮 ID）。
- 断言：① 惰性加载 user32 三入口，失败返回 0；② 用活动窗口链取父窗口；③ 返回 MessageBox 结果（`0x7D5F31` 错误框用它，flags=0x12010 = MB_ICONERROR|MB_OK|MB_TOPMOST|MB_SETFOREGROUND）。
- 状态：**semantics_verified**（137B 全读；`user32.dll` 函数指针缓存 `[0xB78580/84/88]` 已验证存在调用点。）
- 备注：**闭合 batch 040 引用**——`0x7D5F31` 的 MessageBox 分支下游已读完，错误上报链完整。

## 0x7D0DBA  底层 Win32 流写（WriteFile 调用点）
- 事实（batch 065，disasm 完整读 0x7D0DBA ~369B）：`(handle, buffer, count) → written_count`。
  - `count==0` 直返 0；按句柄槽表索引（`[0xB78A80][(n>>5)] + (n&0x1F)*0x24`）取槽；槽 `+4` bit 0x20（文本模式 pending）→ `0x7D769D(2,0,handle)` 清文本转换标志；
  - **文本模式 0x0A→0x0D0A 扩展**（`槽+4 & 0x80`）：用栈上 0x1000 缓冲暂存，逐字节扫描 0x0A 展开为 0x0D 0x0A（扩展计数 `[ebp-0x10]`），每 0x1000 字节调 `0x7E11A0`（`KERNEL32!WriteFile`）写一段；
  - **二进制模式**：直接 `WriteFile(handle, buffer, count, &written, 0)`，无缓冲；
  - **部分写续写循环**：`written < buffered` 时推进源缓冲指针继续写（`0x7D0E2A` 回跳），`written==buffered` 才退出循环；
  - **错误映射**：WriteFile 返回 0 → `GetLastError`（0x7E1200）；`GetLastError==2`（ENOENT）→ errno=9 + dos_error=2；其他 → `0x7D161B(GetLastError)` 转换；失败统一 `or eax,-1` 返 -1；
  - **Ctrl-Z EOF**：文本模式 + Ctrl-Z 标志（`槽+4 & 0x40`）时，若 `buffer[0]==0x1A` → 直返 0（EOF）；
  - **返回值**：`总写字节数 - 文本扩展数`（还原成逻辑字节数）。
- 断言：① 文本模式 0x0A→0x0D0A 扩展、返回逻辑字节数；② 部分写续写循环（WriteFile 可能只写一部分）；③ 错误映射（WriteFile 0→errno=0x1C / 0→errno=9+dos_error=2 / 其他→0x7D161B 转换）；④ Ctrl-Z 字节按磁盘 EOF 处理；⑤ 0x1000 缓冲为栈分配（无堆分配，无锁）。
- 状态：**semantics_verified**（batch 065 新增登记：0x7D0DBA ~369B 完整汇编；IAT 实锤 0x7E11A0=`KERNEL32!WriteFile`、0x7E1200=`KERNEL32!GetLastError`；下游 `0x7D161B`/`0x7D168E`/`0x7D1697`/`0x7D769D`/`0x7D07DF` 同族调用点已全部登记。**这是"已登记 batch 019"的兑现**——contracts L578 声称"0x7D0DBA 已登记 batch 019"但一直没有本条节，本批补齐，与 batch 064 0x7CD9F5/0x7CDA56 同类问题。）
- 备注：与 `0x7D0844`（底层读）互为读写对称对——`0x7D0DBA` 是写、`0x7D0844` 是读，共享句柄槽表索引/文本模式/Ctrl-Z 语义。

## 0x7D07DF  流槽校验 + 读分发（三参）
- 事实（证据 `frontier_batch_039.txt`，101B 全读）：`param_1 < [0xB78B80]`（槽上限）且 `槽+0x4 & 1`（active 位）→ `0x7D96A8`（Enter+惰性初始化）→ `0x7D0844(param_1, param_2, param_3)`（底层读）→ `0x7D9707`（Leave）→ 返回读结果；非法/未激活 → `0x7D168E()`（errno 槽）=9（EBADF）+ `0x7D1697()`（dos_error）=0 + 返回 -1。
- 断言：① 槽校验 = `idx < 上限 && active 位`；② 读回调 `0x7D0844`；③ 错误路径 errno=9 + dos_error=0 + 返回 -1。
- 状态：**semantics_verified**（101B 全读；`0x7D0844` 已登记 batch 019）。

## 0x7D0D55  流槽校验 + 写分发（三参）
- 事实（证据 `frontier_batch_039.txt`，101B 全读）：与 0x7D07DF **逐字节同构**，仅读回调换成 `0x7D0DBA`（底层写）——同一份"校验+锁+分发+解锁"模板的两参/三参实例。
- 断言：① 与 0x7D07DF 同构（读→写）；② 写回调 `0x7D0DBA`；③ 错误路径 errno=9 + dos_error=0 + 返回 -1。
- 状态：**semantics_verified**（101B 全读；`0x7D0DBA` 已登记 batch 019）。

## 0x7D7710  流状态初始化（buffered/未缓冲 fallback）
- 事实（证据 `frontier_batch_039.txt`，68B 全读）：`[0xB781C0]++`（缓冲流计数）；`_malloc(0x1000)` 分配 4K 缓冲 → 成功：`FILE+0x8`=缓冲、`FILE+0xC |= 0x8`（`_IOBUFFERED`）、`FILE+0x18`=0x1000；失败：`FILE+0xC |= 0x4`（`_IOUNBUFFERED`）、`FILE+0x8`=`FILE+0x14`（内联 2 字节槽）、`FILE+0x18`=2；最终 `FILE+0x4`=0、`FILE+0`=`FILE+0x8`。
- 断言：① 4K 堆缓冲优先，内联 2 字节槽 fallback；② 成功置 `_IOBUFFERED`(0x8)、失败置 `_IOUNBUFFERED`(0x4)；③ 计数 `[0xB781C0]` 只增不减（无 free 路径）。
- 状态：**semantics_verified**（68B 全读；`0x7C9430` = `_malloc` 已登记）。

## 0x7D5F31  CRT 运行时错误对话框（`_RTC`/`_invalid_parameter` 上报）
- 事实（证据 `frontier_batch_040.txt`，339B 全读）：线性扫描 `[0x87CBA8..0x87CC38)`（16 个 8 字节槽）匹配 `param_1` 错误码 → 命中后：
  - `[0xB78230]==1` 或（`==0 && [0x87C2A4]==1`）→ **write-to-stderr 模式**：`GetStdHandle(-12)`（STD_ERROR_HANDLE）+ `WriteFile(handle, 槽+4 字符串, strlen, &param_1, NULL)`（把消息写 stderr）。
  - 否则（且 `param_1 != 0xFC`）→ **MessageBox 模式**：`GetModuleFileNameA` 取 exe 路径（失败 fallback 字符串 `0x7F9C5C`）→ 长度 >0x3C 时截断尾 3 字符 + `"..."` → 组装 "Microsoft Visual C++ Runtime Library" 消息框（`0x7D4BF0`/`0x7D4C00` 拼接 exe 路径 + 错误码 + 消息字符串 + `0x12010` flags → `0x7DAE9F` MessageBoxA）。
- 断言：① 错误码查表 16 槽；② stderr 模式经 `WriteFile(STD_ERROR_HANDLE)`；③ MessageBox 模式组装标准 CRT 运行时错误框。
- 状态：**semantics_verified**（339B 全读；`0x7DAE9F` MessageBoxA 包装未读 — 低优先级）。

## 0x7D3886  `_strncpy` 加锁变体（DBCS lead-byte 感知）
- 事实（证据 `frontier_batch_040.txt`，154B 全读）：`[0xB7885C]==0` → 直接 `_strncpy`（0x7C91D0）。否则 `0x7CD9F5(0x19)` 锁内逐字节拷贝：读 `[0xB78961][c]` 分类表，`& 0x4`（lead-byte 位）→ 双字节拷贝（拷贝首字节 + 续字节，尾字节 NUL 处理）；非 lead → 单字节；遇 NUL 提前停；剩余 `param_3` 用 `STOSD/STOSB` 填 0（与 `_strncpy` 的 padding 语义一致）。
- 断言：① DBCS 感知（lead-byte 双字节拷贝防截断）；② 剩余填 0（`_strncpy` padding）；③ `0x19` 锁守护。
- 状态：**semantics_verified**（154B 全读；`0x7C91D0` = `_strncpy` 已登记）。

## 0x7D9D7F  加锁字节→宽字符转换包装（引用计数 + 条件锁）
- 事实（证据 `frontier_batch_040.txt`，93B 全读）：`InterlockedIncrement(&[0xB78BA4])`（转换计数）；`[0xB78BA0]!=0`（有等待者）→ `InterlockedDecrement` + `0x7CD9F5(0x13)` 锁内执行；否则无锁执行 `0x7D9DDC(param_1, param_2, param_3)`；结束后按分支解锁/递减。引用计数 `[0xB78BA4]` 在等待者场景递减两次。
- 断言：① 引用计数 `[0xB78BA4]` 守护转换活跃度；② `[0xB78BA0]` 非 0 才加锁（减少无竞争开销）；③ 等待者分支计数净 -1（先 +1 后 -2）。
- 状态：**semantics_verified**（93B 全读；`0x7D9DDC` 底层转换已读 batch 042）。

## 0x7C8EF4  `printf` 族带计数包装（输出后 NUL 终止）
- 事实（证据 `frontier_batch_045.txt`，82B 全读；callsites 200 / callers 114）：构建 `0x42`（0x40 + 0x2）flags + 无限宽（`0x7FFFFFFF`）+ `void*` buffer 结构，调 `0x7CE2A5(&buffer, param_2, &flags)` 做**计数转换**；`0x7FFFFFFF - 1`（剩余计数）若 `<0`（即实际输出 > 0x7FFFFFFF 字节）→ `0x7CE18D(0, &buffer)`（flush/abort 路径）；否则 `*buffer = 0`（**NUL 终止输出缓冲**）。返回 `0x7CE2A5` 的写入长度。
- 断言：① 用 `0x7CE2A5`（转换核心）做缓冲长度探测；② 成功路径对缓冲补 NUL 终止；③ 超出 `0x7FFFFFFF` 上限走 `0x7CE18D`（abort/flush）路径；④ flags `0x42` 组合含义（`0x40`|`0x2`）待对照 `0x7CE2A5` 内部分支。
- 状态：**semantics_verified**（batch 057 升级：82B 完整汇编 + 反编译二次全读，与 batch 045 结论一致无漂移——`local_18=0x42`(flags 0x40|0x2)、`local_20=0x7fffffff`(无限宽哨兵)、`local_1c/local_24=param_1`(缓冲指针双份)；计数转换后 `0x7fffffff-1` 溢出→`0x7CE18D(0,&buffer)` abort 路径，否则 `*buffer=0` NUL 终止。**下游全部闭合**：`0x7CE2A5` 格式化引擎 batch 046 verified、`0x7CE18D` 单字符写 batch 052 verified。）
- 备注：本函数是**主代码格式化输出的高频入口**（fan-in 200），行为链下游 `0x7CE2A5` 是真正的格式化引擎。

## 0x7CA405  宽字符前导 NUL 长度（`wcscspn` 族，"leading L'\0' -1"）
- 事实（证据 `frontier_batch_045.txt`，29B 全读；**batch 058 反编译歧义消除**）：`(short* s)` 单参数。反汇编逐条：`ECX=param_1` → `CMP word[ECX], 0` → `LEA EAX, [ECX+2]` → `JZ 0x7CA41C`（**首字就是 L'\0' 才跳过首字扫描**）→ 循环 `MOV DX,[EAX]; INC EAX×2; TEST DX,DX; JNZ 循环`；终止时 `SUB EAX, ECX; SAR EAX, 1; DEC EAX`。等价 C：`i=0; while (s[i]==0) i++; return i - 1;`
- 断言：① 返回"前导 L'\0' 宽字符数 - 1"，正常字符串（首字符非 NUL）恒返回 -1；② 全 NUL 缓冲长度 n → 返回 n-1；③ 无 NUL 终止检查（扫描终止条件本身即非零字）。
- 状态：**semantics_verified**（batch 058 升级：29B 全读 + `xref_abs.py 0x7CA405` 反查 **0 处绝对地址引用**——batch 045 记录的"callsites 186 / callers 94"是 Ghidra call-tree 统计，不代表 exe 内实际被调用；此函数属 CRT 静态导出但无 exe 内 caller，**死代码形态**，因此 batch 045 记录的"依赖 wcsstr 回指"语义推测被证实不可用（无 caller 可对照），反汇编直译结果即为最终语义。）
- 备注：非 `wcscspn`（`wcscspn` 首参是"集合"，此函数只有 1 个入参）——是**前导零扫描**基元，可能是 CRT 内部辅助（如段级字符串填充）。锁步若涉及可安全忽略（无 caller）。

## 0x7CA489  宽字符字符串拷贝（`wcscpy`）
- 事实（证据 `frontier_batch_045.txt`，37B 全读；callsites 178 / callers 112）：逐 16 位前向拷贝 `src→dst`，遇 `L'\0'` 后停止（**含 NUL 一起拷贝**）。签名 `(short* dst, short* src)`。
- 断言：① 行为等价 `wcscpy`（拷贝含终止符）；② 无缓冲大小检查（调用方负责）；③ 与 `0x7CA405` 同属宽字符基础操作族（0x7CA4 区段集中放置）。
- 状态：**semantics_verified**（37B 全读；无外部依赖）。

## 0x7C8B48  基于堆的快速排序（`qsort` 主实现）
- 事实（证据 `frontier_batch_045.txt`，340B 全读 + **batch 049 下游 `0x7C8C9C`/`0x7C8CEA` 全读闭合**；callsites 63 / callers 60）：标准 introspective-ish quicksort：栈上 30 槽 `local_fc[30]`/`local_84[30]` 递归栈（**是显式栈，不是 OS 递归**）；`count<2 || size==0` 直接返回；分区长度 `≤8` 元素 → `0x7C8C9C(uVar3, uVar5, param_3, param_4)`（小分区选择排序，**直接调调用者比较函数指针**）；`>8` → `0x7C8CEA((uVar2>>1)*param_3+uVar3, uVar3, param_3)`（取中点元素做 pivot 的三路 partition）→ 内部 `0x7C8C9C` 用比较函数推进 pivot 位置。递归调度：**先递归大分区、小分区压显式栈**（`local_8` 深度计数 30 上限）；partition 后把较大部分地址推栈，继续处理较小部分——因此栈深 log 界而非 n 界。排序非稳定（选择排序交换）。
- 断言：① `count<2 || size==0` → 无操作；② 分区 `≤8` → `0x7C8C9C` 选择排序；③ `>8` → `0x7C8CEA` 三路分区 + 先大后小递归；④ 显式栈 30 槽、`local_8` 深度计数；⑤ 非稳定排序。
- 状态：**semantics_verified**（340B 全读 + batch 049 `0x7C8C9C` 78B/`0x7C8CEA` 44B 下游全读闭合——排序正确性、分区细节、稳定性均已可断言。）
- 备注：`qsort` 是现代 CRT 天然等价行为，**lockstep 不涉及**（除非游戏逻辑依赖确定排序序）；但**非稳定** + 分区交换序敏感——若游戏依赖特定序的遍历（如对象表排序），需要逐调用点核对是否使用**稳定替代**（现代 CRT qsort 亦非稳定，语义等价）。

## 0x7C8CEA  分区交换循环（`_qsort_swap`）
- 事实（证据 `frontier_batch_049.txt`，44B 全读）：`(char* p1, char* p2, size_t n)`。`p1==p2 || n==0` → 直接返回；否则逐字节三交换（`t=*p1; *p1=*p2; *p2=t`）推进 `p1++`/`p2++`、`n--`，循环 `n!=0`。
- 断言：① 逐字节交换不重叠区域；② 返回时两区内容互换、指针不回填；③ 空/零长度 → 无操作。
- 状态：**semantics_verified**（44B 全读，纯字节交换基元——qsort 分区交换的机械细节。）

## 0x7C8C9C  小分区选择排序（`_qsort_small`）
- 事实（证据 `frontier_batch_049.txt`，78B 全读）：`(uint lo, uint hi, int size, code* cmp)`。`lo<hi` 时：外层循环把 `[lo, hi]` 逐元素扫描，内层 `for (j=lo+size; j<=hi; j+=size)` 比较 `cmp(j, best)`——**选择排序**：每轮找 `[lo..hi]` 中最大元素（比较函数 `0< 结果` 判定更大），扫完把最大元素交换到 `hi` 端（`0x7C8CEA(best, hi, size)`），`hi -= size` 继续；直到 `lo>=hi`。
- 断言：① `lo<hi` 才执行；② 选择排序（每轮找最大放末端）；③ 直接调调用者比较函数指针；④ 交换用 `0x7C8CEA` 逐字节。
- 状态：**semantics_verified**（78B 全读；下游 `0x7C8CEA` 已登记。）

## 0x7CA7D8  printf 族完整模板（locale 锁 + 计数 + flush + 解锁）
- 事实（证据 `frontier_batch_045.txt`，60B 全读；callsites 293 / callers 4）：五步模板：`0x7CB3F9(param_1)` → `0x7D2B7F(param_1)` → `0x7CE2A5(param_1, param_2, &count)` → `0x7D2C0C(len, param_1)` → `0x7CB44B(param_1)`；返回 `0x7CE2A5` 的长度。
- 断言：① 五步顺序固定；② `0x7CB3F9`/`0x7CB44B` 为**流锁对称对**（`0x7CD9F5`/`0x7CDA56` 全局锁 key = 流槽线性映射 `(ptr-0x87BF50)>>5 + 0x1C`，或非标准流直接 `EnterCriticalSection(ptr+0x20)`/`LeaveCriticalSection`）；③ `0x7D2B7F` 前置缓冲初始化（`0x7D7754` 句柄校验 → 非标准流 `0x87BF70/0x87BF90` 特殊处理 → `_malloc(0x1000)` 或内联 2 字节槽，与 `0x7D7710` 同构）；④ `0x7CE2A5` 核心转换；⑤ `0x7D2C0C(len, ...)` 后置（`len!=0 && 流槽+0xD&0x10` → `0x7CB1F9` flush + 清缓冲计数）。
- 状态：**semantics_verified**（60B 全读 + batch 046 下游五步全部闭合——`0x7CB3F9`/`0x7CB44B`/`0x7D2B7F`/`0x7D2C0C` 全读）。
- 备注：**fan-in 293 = 主代码最高扇入的格式化入口**，五步模板的每一步下游已闭合（`0x7CE2A5` 格式化引擎 batch 046 全读 1825B）。

## 0x7C9380  64 位无符号除法（`__aulldiv` MSVC 库函数）
- 事实（证据 `frontier_batch_050.txt`，104B 全读；Ghidra 库匹配 `Library: Visual Studio`；callsites 6 / callers 6）：`(uint hi, uint lo, uint dhi, uint dlo)` → `uint64` 商。分两路：①`dhi==0`（除数 32 位）→ 两段 `DIV`（`EDX:ESI = dlo / dhi` 先除高半、余数+低半再除）；②`dhi!=0`（除数 64 位）→ **归一化右移**循环（4 个半字一起 SHR/RCR，直到 `dhi>>1==0`）→ 用 32 位 `DIV` 除→ `MUL` 回乘还原 + 校正（`if (CARRY || (被除数 < 校正后乘积)) 商--`）→ 结果 `(0, 商)`。
- 断言：① 与 `0x7D7840` `__aullrem` 构成 64 位除余数对（`div` 指令对 64 位被除数无法直接算，用两段）；② 除数为 0 → **硬件除零异常**（调用者责任）；③ 现代 CRT 天然等价（x86 `DIV` 指令行为不变）。
- 状态：**semantics_verified**（104B 全读，Ghidra 库匹配 + 归一化算法标准实现。）
- 备注：**0x7CE2A5 格式化引擎 64 位整数分支的除数后端**——`__aulldiv` + `__aullrem` 配对完成任意基数（0xA/0x8/0x10）的 64 位十进制转换。

## 0x7D21BE  独立格式化转换引擎（`vsprintfW`/`swprintf` 宽字符实例）
- 事实（证据 `frontier_batch_050.txt`，1837B 全读；`containing=0x7D21BE`；`0x7CE2A5` 引用点）：`(流槽, 格式串, arg 指针)`。与 `0x7CE2A5` **同构但独立实现，且输出为宽字符**（`0x7D290B` 经 `0x7D2FC8` 写宽字符；`0x7CE2A5` 写窄字符）——**两套引擎 = 窄字符 printf 流 vs 宽字符 sprintf 缓冲**。核心特征：
  - **状态机跳转表 `DAT_007f97ec`**（8 类：`case 0-7`）+ 前缀状态 `local_3c`；
  - **flags 解析**：`-`(0x4)/`+`(0x1)/` `(0x2)/`#`(0x80)/`0`(0x8)；
  - **width/precision**：`*` 用 `0x7D2995` 弹参数；`local_2c` = width（case 3）、`local_14` = precision（case 5）；
  - **长度修饰符**：`h`(0x20)/`l`(0x800 宽字符)/`I64`(0x8000)；
  - **转换符**：`c`(0x7D290B 写宽字符)、`s`(窄串+0x7D2995)、`S`(宽串)、`Z`(宽串特判)、`d/i/u/o/x/X/p`(整数，0x7D7840 `__aullrem` + `0x7C9380` `__aulldiv` 64 位除法循环，基数 0xA/0x8/0x10/0x27 hex)、`e/E/f/g/G`(**经函数指针表 `[0x87C4D0]`** 调用浮点格式化，同 0x7CE2A5)、`n`(写回 local_18 长度)；
  - **输出**：`0x7D292B(填充宽字符, 数量, 流槽, 计数)` + `0x7D295C(宽字符串, 长度, 流槽, 计数)`（batch 051 全读）；
  - **返回** `local_18`（已输出宽字符总数）。
- 断言：① 与 `0x7CE2A5` **语义等价但输出宽字符**（同一 MSVC 格式化引擎的窄/宽两个独立实例，各自配套辅助函数）；② 浮点走函数指针表 `[0x87C4D0]`（与 0x7CE2A5 共享表）；③ 64 位整数走 `__aullrem`/`__aulldiv`；④ 返回总长度。
- 状态：**semantics_verified**（1837B 全读 + batch 051 输出辅助族 5 个全读闭合——`0x7D2995`/`0x7D29A2` 参数弹取、`0x7D295C`/`0x7D292B` 输出循环、`0x7D290B` 宽字符写原语；`0x7D2FC8` 底层宽写未单独导出——行为经 0x7D290B 调用点捕获，低优先级。）
- 备注：**CRT 窄/宽两套格式化引擎**（`0x7CE2A5` 窄字符服务 `0x7CA7D8` printf 族；`0x7D21BE` 宽字符服务 swprintf 族）——**行为等价但实现不同**，lockstep 若涉及格式化输出须**逐引擎断言**。

## 0x7D2995  宽引擎参数弹取（32 位，`0x7D21BE` 专用）
- 事实（证据 `frontier_batch_051.txt`，13B 全读）：`*param_1 += 4; return *(*param_1 - 4)`。
- 断言：与 `0x7CEA84`（batch 047）**逐字节同构**——同一参数弹取原语在窄/宽两引擎各放一份。
- 状态：**semantics_verified**（13B 全读，纯指针算术——机械调用约定基元。）

## 0x7D29A2  宽引擎参数弹取（64 位，`0x7D21BE` 专用）
- 事实（证据 `frontier_batch_051.txt`，16B 全读）：`*param_1 += 8; return *(*param_1 - 8)`（EDX:EAX 双字对）。
- 断言：与 `0x7CEA91`（batch 047）逐字节同构；`I64`/宽整数弹取。
- 状态：**semantics_verified**（16B 全读，纯指针算术。）

## 0x7D2FC8  宽字符流写（宽引擎底层写原语）
- 事实（证据 `frontier_batch_052.txt`，249B 全读；`0x7D290B` 引用点闭合）：`(uint 宽字符, byte **流槽) → uint（0xFFFF=失败）`——`_putwc` 底层。分两路：
  - **宽流**（`槽+0xC & 0x40` 置位）→ 直写 `word ptr` 到流缓冲，计数 `槽+4 -= 2`，不足走 `0x7D9FCA(字符, 槽)`（底层宽字符写，未登记）；
  - **非宽流** → **经 `0x7D777D`（宽字符→字节转换，batch 043 已登记，返 -1=转换失败）**：
    - `-1`（转换失败）→ `errno(0x7D168E)=0x2A`（EILSEQ），返回 0xFFFF；
    - 转换字节数 = 1（单字节）→ 缓冲计数 `槽+4 -= 1`，够则直写缓冲、`槽[0] += 1`；不够则 `0x7CE18D(字节, 槽)`（窄写）兜底；
    - 转换字节数 = 2（双字节 DBCS）→ 连续写两字节（分别走缓冲直写或 `0x7CE18D`）。
  - 成功返回 `param_1 & 0xFFFF`（原宽字符）。
- 断言：① **宽流直写 / 非宽流转窄宽** 双路；② 非宽流依赖 `0x7D777D`（UTF-8/ANSI 转换）与 `0x7CE18D`（窄写）——**若宽流同走**则与 `0x7CE18D` 构成完整`_putwc`；③ 转换失败 → EILSEQ（0x2A）+ 0xFFFF；④ 返回值 = 宽字符（成功）或 0xFFFF（失败）。
- 状态：**semantics_verified**（249B 全读；`0x7D777D` batch 043 已登记、`0x7CE18D` batch 052 全读闭合、`0x7D9FCA` 低优先级未单独导出——行为经调用点捕获。）
- 备注：**闭合批次 051 `0x7D290B` 的底层引用**——宽引擎写路径全链贯通（0x7D21BE → 0x7D290B → 0x7D2FC8 → 0x7D777D/0x7CE18D/0x7D9FCA）。

## 0x7DC720  单参数转发包装（`param, 0`）
- 事实（证据 `frontier_batch_052.txt`，14B 全读；`0x7DCD9C` 下游）：`(param)` → `0x7DCD9C(param, 0)`——无条件转发，第二个参数恒 0。
- 断言：与 `0x7Dcd9c(param, 0)` 完全等价；API 身份未定（`0x7DCD9C` 未登记，待 trace 调用方确认语义）。
- 状态：**semantics_verified**（14B 全读，确定性转发；`0x7DCD9C` 本体未导出。）
- 备注：**升级 batch 002 的 semantics_verified_partial → semantics_verified**（100% 读且转发语义确定；下游 API 语义未定是"调用方层"问题，不阻塞本条）。

## 0x7D30D1  FlushFileBuffers 句柄包装
- 事实（证据 `frontier_batch_052.txt`，147B 全读；`DAT_00b78b80` 句柄表上限）：`(句柄槽)→DWORD`。流程：`槽 >= 上限` → `errno(0x7D168E)=9`（EBADF）、返 0xFFFFFFFF；槽内 bit0（active）检查不通过同 errno=9；active → `0x7D96A8`（Enter CS）→ 二次 active 检查 → `0x7D95BF`（native handle 校验）→ `[0x7E12C4]`（**FlushFileBuffers**，间接调用）→ 失败记录 `dos_error(0x7D1697)=GetLastError` + `errno=9`；成功返回 0。退出 `0x7D9707`（Leave CS）。
- 断言：① flush 失败 → `errno=9` 且 `dos_error=GetLastError` 保留；② 幂等语义（flush 已刷缓冲）；③ 现代 CRT 天然等价（FlushFileBuffers 同步落盘）。
- 状态：**semantics_verified**（147B 全读；`0x7D96A8`/`0x7D9707` batch 048、`0x7D95BF` batch 048、`0x7D168E`/`0x7D1697` 已登记。）
- 备注：**升级 batch 020 的 semantics_verified_partial → semantics_verified**（内在引用全部闭合 + 失败映射断言成立）；`_REIMPL` 单测 `HandleTable::flush` 已 verified。

## 0x7D8E22  double 归一化 + 指数提取（`frexp` 族，含 x87 重建）
- 事实（证据 `frontier_batch_052.txt`，193B 全读；`[0x7E2800]` **已 dump = double 0.0**）：输入 64 位 double + 输出指数 `param_3`：
  ① `value == 0.0`（比较 `[0x7E2800]`）→ `*param_3=0`、返回 0.0；
  ② 指数为 0 且尾数非 0（**非规格化数**）→ 循环左移尾数直到最高位（bit `0x10`）置位（`ESI` 从 `-0x3FD` 递减），清隐式位（`&0xFFEF`），按符号补符号位（`|0x80` 到符号字节），经 `0x7D8D3B`（x87 80 位重建，batch 037 已闭合）回载为 double → 返回；
  ③ 常规数 → 经 `0x7D8D3B` 重建后返回，`*param_3 = (exp16 & 0x7FF) - 0x3FE`。
- 断言：① 等价于 `frexp`（返回 `value = mantissa * 2^exp`，mantissa ∈ [0.5, 1)）；② 非规格化数归一化路径（含符号处理）；③ 依赖 `0x7D8D3B`（x87 80 位重建，batch 014/037 已登记）。
- 状态：**semantics_verified**（193B 全读；`[0x7E2800]` 常量 `= 0.0` 已用 read_constants.py dump 确认；`0x7D8D3B` 已登记 batch 037。）
- 备注：**升级 batch 043 的 semantics_verified_partial → semantics_verified**（原 partial 理由"常量未 dump"已消除）。

## 0x7CE18D  流写单字符（带缓冲 + 底层写 + 错误标志）
- 事实（证据 `frontier_batch_052.txt`，280B 全读；`0x7C8EF4`/`0x7D2FC8` 下游）：`(char, 流槽)`。`槽+0xC` 状态位：非缓冲/已错/宽流（`&0x82` 或 `&0x40`）→ 置 `_IOERROR`(0x20) 返回 -1；`&0x1` 带缓冲 → 清零计数、回绕缓冲指针；`&0x10` 复位缓冲模式。写路径：缓冲满（`缓冲指针 != 基址`）→ `0x7D0D55`（底层写）flush；缓冲空 → 若 `[槽/组+4] & 0x20`（`0x7D7638` 前提）则调 `0x7D7638(句柄, 0, 2)`（flush+位置调整）；未缓冲模式 → `0x7D0D55(句柄, &char, 1)` 直写；写成功回字符、失败置错误位 0x20 返回 -1。
- 断言：① 与 `0x7DA638`（流写宽字符）互为窄/宽路径；② 缓冲满 → `0x7D0D55` 底层写；③ `0x7D7638(handle,0,2)` = seek/flush 位置调整；④ 错误置 `0x20` 位 + 返回 -1。
- 状态：**semantics_verified**（280B 全读；`0x7D7638` 已登记 batch 007 `HandleTable::seek`、`0x7D7754`/`0x7D7710` 已登记、`0x7D95BF`/`0x7D96A8`/`0x7D9707`/`0x7D93A1` batch 048 已登记——**窄写原子操作链全闭合**。）
- 备注：**升级 batch 046 的 semantics_verified_partial → semantics_verified**（原 partial 理由"引用未闭合"已消除——`0x7D95BF` 槽校验/`0x7D96A8` CS/`0x7D93A1` initterm 全部登记；batch 052 `0x7D2FC8` 显式调用的窄写底）。

## 0x7CE2A5  格式化转换核心（`_output`/`vfprintf` 引擎）
- 事实（证据 `frontier_batch_046.txt`，1825B 全读；0x7CA7D8/0x7C8EF4 共下游）：`(param_1=流槽, param_2=格式串, param_3=arg 指针)`。格式串扫描状态机（跳转表 `0x7CE9C6` 8 类）：
  - **flags 解析**：`-`(0x1)/`+`(0x2)/` `(0x4)/`#`(0x80)/`0`(0x8)（`0x7CE335` 分支集）；
  - **width/precision**：`*`（`0x7CEA84` 弹参数，负 → `-` flag）+ 十进制累加（`*10 + c - 0x30`）；
  - **长度修饰符**：`h`(0x20)/`l`(0x10)/`w`(0x800)/`I64`(0x8000，双字符检测)；
  - **转换符**：`c`（`0x7D777D` 宽转字节 + `0x7CEAA1`）、`s`/`S`（`0x7CEA84` 取指针 + `0x7D15A0` 长度 + `0x7CEA1B` 填充）、`Z`/`z`（`0x7CEA91` 宽串）、`d`/`i`/`u`/`o`/`x`/`X`/`p`（整数 + `0x7D7840` `__aullrem`/`0x7C9380` `__aulldiv` 64 位除 + 基数 `0xA/8/0x10` + `+0x30`/`+0x57` 数字字母）、`e`/`E`/`f`/`g`/`G`（**浮点 → 经函数指针表 `[0x87C4D0]`**，`g` 低位截断 `0x7CE8AC` 尾零 `0x7CE884`、`0` 填充 `0x7CE8A2`）、`n`（写回已输出长度到参数）。
  - **输出**：`0x7CEA1B(填充字符, 宽度, 流, 计数)` / `0x7CEA4C(字符串, 长度, 流, 计数)`；精度截断 `0x7CE93F`；`-` flag 左对齐；宽字符串逐字符 `0x7D777D` 转窄输出。
- 断言：① 等价 `vfprintf` 核心（flags/width/precision/length 全解析）；② 浮点转换走函数指针表 `[0x87C4D0]`（可替换的浮点格式化后端）；③ `__aullrem`/`__aulldiv` 64 位除法链；④ 返回输出总长度（`local_18`）。
- 状态：**semantics_verified**（1825B 全读 + batch 047 输出辅助族 6 个全读闭合 + batch 050 下游 `0x7C9380` `__aulldiv` 已闭合 + `0x7D21BE` 第二引擎已闭合——`0x7CEA84`/`0x7CEA91`/`0x7CEAA1` 参数弹取、`0x7CEA4C`/`0x7CEA1B` 输出循环、`0x7CE9E6` 单字符写原语；剩 `[0x87C4D0]` 浮点格式化后端函数指针（与 `0x7D21BE` 共享同一张表）——行为断言已捕获，仅数据表本身待 dump，低优先级。）
- 备注：**主代码格式化引擎**（fan-in 293/200 的汇聚点）。浮点后端 `[0x87C4D0]` 是 rules.ini 数值打印路径（debug 输出格式化）——lockstep 若需日志一致则须闭合（低优先级：仅调试输出依赖）。

## 0x7CE18D  流写单字符（带缓冲 + 底层写 + 错误标志）
- 事实（证据 `frontier_batch_046.txt`，280B 全读；`0x7C8EF4` 下游）：`(char, 流槽)`。`槽+0xC` 状态位：非缓冲/已错/宽流（`&0x82` 或 `&0x40`）→ 置 `_IOERROR`(0x20) 返回 -1；`&0x1`（`_IOBUFFERED`）→ 重置计数、回绕缓冲指针；`&0x10` 复位缓冲模式。写路径：`槽+0x18` 计数值 `-1`（`槽+0x4`），若缓冲满（`*ptr==ptr+8` 且 `槽+0x4==0`）→ `0x7D0D55`（底层写）flush；槽 `+0x4` 为 0 时做 **`0x7D7638(句柄, 0, 2)`（flush+位置调整）**；写字符到缓冲末尾，返回字符；底层写失败 → 置 `_IOERROR` 返回 -1。
- 断言：① 与 `0x7DA638`（流写宽字符）互为窄/宽路径；② 缓冲满 → `0x7D0D55` 底层写；③ `0x7D7638(handle,0,2)` = seek/flush 位置调整；④ 错误置 `0x20` 位 + 返回 -1。
- 状态：**semantics_verified**（280B 全读；`0x7D7638` 已登记 batch 007 `HandleTable::seek`、`0x7D7754`/`0x7D7710` 已登记、`0x7D95BF`/`0x7D96A8`/`0x7D9707`/`0x7D93A1` batch 048 已登记——**窄写原子操作链全闭合**。**与 batch 052 同名节结论完全一致**——本重复节仅作历史锚点保留，最新结论见 batch 052 节。）

## 0x7CB3F9 / 0x7CB44B  流锁 Enter/Leave 对称对
- 事实（证据 `frontier_batch_046.txt`，47B×2 全读）：指针在 `[0x87BF50, 0x87C1B0)`（标准流区，32 字节间隔）→ `0x7CD9F5/0x7CDA56((ptr-0x87BF50)>>5 + 0x1C)`（全局锁 key 线性映射）；区外（用户流对象）→ `EnterCriticalSection(ptr+0x20)` / `LeaveCriticalSection(ptr+0x20)` 直调。
- 断言：① 标准流用全局锁 key 映射，用户流用对象内临界区；② 与 `0x7CA7D8` 五步模板的首尾对称配对。
- 状态：**semantics_verified**（47B 全读；`0x7CD9F5`/`0x7CDA56` 已登记 batch 021。）

## 0x7D2B7F  printf 前置缓冲初始化（stdout/stderr 专用）
- 事实（证据 `frontier_batch_046.txt`，141B 全读）：`0x7D7754(流槽+0x10)`（句柄校验）→ 仅 `0x87BF70`（stdout）/`0x87BF90`（stderr）两个标准流特殊处理：`[0xB781C0]++`（缓冲计数）、`槽+0xC&0x10C` 非零跳过、`[0xB782D0+i]`（stdout/stderr 共享 4K 缓冲）已分配 → 复用；未分配 → `_malloc(0x1000)` 失败则内联 2 字节槽（`槽+5`）；置 `槽+0xC |= 0x1102`（`_IOBUFFERED|_IOMYBUF|_IOFLRTN`?）；返回 1。
- 断言：① 与 `0x7D7710` 同构但**共享缓冲表 `[0xB782D0]`（2 槽 stdout/stderr）**；② malloc 失败内联 fallback；③ 返回 0/1 表示是否就绪。
- 状态：**semantics_verified**（141B 全读；`0x7D7754`/`0x7C9430` 已登记。）

## 0x7D2C0C  printf 后置 flush（带缓冲的流写完即刷）
- 事实（证据 `frontier_batch_046.txt`，42B 全读）：`param_1!=0 && 槽+0xD&0x10` → `0x7CB1F9(流槽)`（底层 flush）→ 清 `槽+0xD &= 0xEE`、`槽+0x18=0`、`槽[0]=0`、`槽[2]=0`（**缓冲指针/计数复位**）。
- 断言：① 条件 = 输出非空且流是带缓冲模式；② flush 后复位缓冲状态。
- 状态：**semantics_verified**（42B 全读 + batch 049 下游 `0x7CB1F9` 已闭合——printf 后置 flush 全链贯通。）

## 0x7CB1F9  底层流缓冲 flush（`_flushbuf` 底层）
- 事实（证据 `frontier_batch_049.txt`，92B 全读；`0x7D2C0C` 引用闭合）：`(流槽)`。条件 `(byte)(槽+0xC & 3)==2 && 槽+0xC & 0x108`（**文本模式 + 带缓冲 + 无错误**）：若 `缓冲指针 - 缓冲基址 > 0`，调 `0x7D0D55(句柄, 缓冲基址, 长度)`（底层 WriteFile）；成功 → 若 `槽+0xC & 0x80`（EOF 已见标记）则清 bit7；失败 → 置 `_IOERROR`（bit 0x20）返回 -1。**无条件**清 `槽+0x4 = 0`（剩余计数）、`槽[0] = 槽[2]`（缓冲指针复位到基址）。返回 0 成功 / -1 失败。
- 断言：① 仅文本模式 + 带缓冲才刷；② 写失败置错误标志位 0x20 并返回 -1；③ EOF 标记 bit7 成功清、失败保留；④ 缓冲指针无条件复位（即使未写也归零）。
- 状态：**semantics_verified**（92B 全读；下游 `0x7D0D55` 底层写已登记。）
- 备注：**闭合批次 046 `0x7D2C0C` 引用**——printf 后置 flush 路径完整贯通。

## 0x7D151E  宽字符 → 8 字节 double（解析器下游）

## 0x87C4D0  浮点格式化后端函数指针表（两引擎共享）
- 事实（证据 `_dump_tables.py` dump 0x87C4C0..0x87C540，PE .data 段；两引擎引用实锤 `code/randmap/disasm.py`）：表布局（4 字节槽，基址 0x87C4C0）：
  - `[+0x00]`=0x0087C370（数据指针，指向 .data 某结构，用途未判明）
  - `[+0x04]`=0x007D60B5（**函数指针**，非终止器；身份待导出）
  - `[+0x08..0x0C]`=0x007F9858 / 0x007F9848（**vtable 指针**，指向 .rdata 类表——疑为浮点状态对象虚表）
  - `[+0x10..0x24]`（即 0x87C4D0 起 6 槽）**全部 = 0x007D7ED2**（batch 015 已登记"以状态 2 终止"路径 = `push 2; call 0x7CD917; ret`，仅 5 字节）
  - `[+0x28..0x30]`=0（3 个空槽）
  - `[+0x34..0x7C]`=浮点位模式常量区（0x80000000、0x00007FFF、0xFFFF8000、0xB9D7A7DC≈1.797693e308=double max、0xB1716685、0x0000400D、0x400DFFFF、0x0C4336F7、0x95F61998、0x00003FFD、0x65033FE0、0x00007078、0x01000000、0xEF610014、0xF26D007C）
- 调用点六例（窄/宽各三处，`call dword ptr [0x87C4D0+i]`，压栈顺序均为 5 参：`[out, fmtchar_sx, precision, width, locale]`——与 `0x7D7EE0` 的 x87 浮点运算函数参数布局**完全同构**）：
  - 窄引擎 `0x7CE64F` `call [0x87C4D0+0x00]`（主格式化）；`0x7CE687` `call [0x87C4D0+0x04]`（精度分支）；`0x7CE66F` `call [0x87C4D0+0x0C]`（`%g` 分支）
  - 宽引擎 `0x7D255E` `call [0x87C4D0+0x00]`；`0x7D2597` `call [0x87C4D0+0x04]`；`0x7D257F` `call [0x87C4D0+0x0C]`（**三槽与窄引擎一一对应**）
  - 调用后紧跟 `cmp byte ptr [out], 0x2d`（`'-'`）→ 负号移位修正，证实返回值为格式化输出缓冲首字节
- 语义判明（**关键修正**）：`0x87C4D0` 起 6 槽全指向 0x7D7ED2（终止器）并非设计意图，而是**运行时惰性绑定表初始值**——CRT 浮点格式化后端（`_lc_float`/`_lc_long_double` 族）在首次调用时被 locale 初始化填充为真实函数地址；PE 文件中的静态值是绑定前占位（`0x7D7ED2` = "表未初始化即被调用 → 立即终止"的哨兵）。证据：① 两引擎引用点与 `0x7D7EE0`（同文件同段的真浮点运算函数）参数布局完全一致；② `[+0x34..0x7C]` 常量区含 double max/2^52/精度相关位模式，是浮点运算函数共享的常量池；③ `[+0x08..0x0C]` 是 .rdata 类表指针，与 MSVC CRT `_lc_*` 分派结构一致。
- 断言：① 两引擎共享同一张表（窄 0x7CE2A5 与宽 0x7D21BE 的浮点分支调用同一组槽）；② 槽偏移 0x00/0x04/0x0C 分别对应 主格式化/精度修正/`%g` 尾零处理三职责；③ 表在 locale 初始化后由 CRT 填充真实函数；④ 静态文件中未填充槽指向终止器 0x7D7ED2。
- 状态：**semantics_verified**（表布局全 dump + 6 引用点上下文反汇编 + 参数布局与 0x7D7EE0 同构 + 哨兵语义三证据互证；0x7D7ED2 已在 batch 015 登记。**batch 062 补齐**：`[+0x04]` 0x7D60B5 身份已闭合——`0x7D60B5` = **`_getmbcp`/`__initmbcp` 初始化入口**（`[0xb782ac]` retry-hook 存在 → `0x7D3374(1,0xAC)` 构造 → `0x7D6146` 写 64 槽多字节 locale 常量表 → `0x7D6494` 销毁）；`0x7D6146` 实为**运行时填充本表**的函数（每槽调 `0x7DAF28` GetCPInfo 包装，把 `[0xb7853e]`/`[0xb78540]` 主/次 locale 的 CP 信息逐槽打包进 `[0x87C4D0..0x87C530]` 函数指针/常量槽——与"PE 静态首槽 0x7D7ED2 是终止器占位"完全互证）；`0x7D6494` = 本表/对象的**销毁清理**（逐槽 `0x7C93E8` free）；`[+0x24]`/`[+0x28]` = double 1.0/0.0 常量区起点；剩余未判明：`[+0x00]` 0x87C370 数据用途——不影响浮点格式化行为断言，仅 debug 输出依赖，低优先级。）
- 备注：**闭合批次 054 `0x7CE2A5` 与 `0x7D21BE` 的共同未读项**——两引擎浮点后端共享表布局已完全掌握；lockstep 若要复刻 `%f/%e` 格式化，须先确定运行时填充后的真实目标地址（需在进程内观测，纯静态不可得）。