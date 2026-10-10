# 全程序重建图谱

本目录是 `gamemd.exe` 全量重建的横向索引，不是 MVP 功能清单。每个已识别函数都必须最终完成归类、还原和验证；排序只用于安排施工顺序。

## 当前快照

由现有 Ghidra 工程 `RA2` 对 `gamemd.exe` 运行 `export_full_atlas.py` 生成。统计数字均以同目录 `summary.txt` 为准，不代表语义已经完成。

- 已识别函数：8655
- Ghidra 直接调用/尾跳边：52465
- 未解析的间接调用点：12573
- 可执行区：4063232 字节
- 已归属函数体：2600053 字节
- 已定义但未归属函数的指令：981253 字节
- 可执行区内已定义数据：481926 字节
- 未定义可执行字节：0

上述四种字节分类相加恰好等于 `.text` 的 4063232 字节。Ghidra 将整个可执行区都定义成了指令或数据，但这不能等同于“边界和语义已经正确”；未归属指令与已定义数据仍必须继续区分函数遗漏、跳转表、内嵌数据、对齐填充等类别。

## 文件

| 文件 | 用途 |
|---|---|
| `functions.tsv` | 全部函数目录、范围、命名来源、调用统计 |
| `direct_edges.tsv` | Ghidra 已确认的直接调用与尾跳边 |
| `unresolved_calls.tsv` | 虚函数、函数指针、寄存器间接调用等待解析点 |
| `coverage.tsv` | 可执行内存块的函数归属和未归属覆盖 |
| `ranking.tsv` | 全部函数的依赖排序；没有函数被筛除 |
| `summary.txt` | 原始导出统计 |
| `ranking_summary.txt` | 直接扇入和传递依赖的观察视图 |

## 使用方式

1. 用 `export_full_atlas.py` 从 Ghidra 现有分析重新导出全量目录。
2. 用 `code/callgraph_rank.py` 计算依赖施工顺序。
3. 以 `functions.tsv` 为总账，为函数增加人工维护的重建状态和验证状态。
4. 对 `unresolved_calls.tsv` 逐批解析间接边，并与虚表、构造函数和对象布局交叉核对。
5. 对覆盖报告中的未归属区间建立地址级解释，直到代码区没有永久未知项。

`ranking.tsv` 中的 `transitive_dependents` 只表示图上的传递依赖数量，不代表函数重要性，也不代表可以跳过其他函数。`status=unclassified` 是待登记状态，不是“无需复刻”。

## 当前限制

- 该快照依赖 Ghidra 当前函数边界；未解析间接调用不会被伪造为唯一目标。
- 原始字节层的 `E8/E9 rel32` 扫描和绝对地址扫描应作为第二证据源补充，不能把线性反汇编结果直接当作完整调用图。
- 当前尚未做 SCC 压缩、虚表槽位展开、编译单元归属、反编译文本批量导出和动态调用覆盖；这些是后续全图闭合工作，不影响本批总账资产的使用。
- “行为逐位一致”和“重新生成的 EXE 机器码逐字节一致”必须分开记录；本图谱本身只记录静态结构证据。

## 第一批依赖前沿取证

第一批从依赖榜前列领取了六个函数。这里的“领取”只表示施工顺序；其他函数仍全部保留在总账。

| 地址 | 确认语义 | 状态 | 关键证据 |
|---|---|---|---|
| `0x7C8B3D` | C++ `delete` / `operator delete` 转发包装：把唯一栈参数传给 `0x7C93E8` | `crt_runtime_verified` | 11 字节完整汇编；大量析构/释放调用点 |
| `0x7C8E17` | Visual Studio `operator new(unsigned int)`，转调 `__nh_malloc(size, 1)` | `crt_runtime_verified` | Ghidra Library Function 单匹配 + 完整汇编 |
| `0x7C5F00` | MSVC x87 浮点转有符号 64 位整数辅助函数（`_ftol` 家族）；使用缓存控制字和 `fistp qword` | `crt_runtime_verified` | 完整汇编 + 随机地图浮点环境取证 |
| `0x4068E0` | 单字节 `RET` 空桩；调用点仍传入诊断格式串和参数 | `stub_verified` | 函数体逐指令取证 + RMG 阶段调用点 |
| `0x734E60` | 字符串表查找/缺失项创建路径，调用点携带源文件和行号；完整内部记录结构仍待命名 | `semantics_verified_partial` | 180 字节汇编/反编译 + 33 编译单元调试宏扫描 |
| `0x7C8D20` | 含 locale 分支的不区分大小写字节字符串比较 | `crt_runtime_semantics_verified` | 208 字节完整汇编：ASCII 大小写归一化 + locale 转换路径 |

原始证据在 `memory/data/callgraph/frontier_batch_001.txt`，机器可读状态在 `frontier_status.tsv`。

本批同时纠正了一个旧称呼：`0x734E60` 不是“打印调试日志”的函数。它利用编译器宏传入的源文件与行号定位字符串表标签，命中时返回宽字符串，未命中时构造 `MISSING: %hs` 记录；源文件/行号是诊断元数据。`0x4068E0` 才是本二进制中退化为单字节 `RET` 的诊断输出桩。

第二至第四批的机器可读状态也已合并到同一个 `frontier_status.tsv`。第四批确认了：

- `0x7C9FF0` 是路径组成器，按 drive、directory、basename、extension 拼接 Windows 路径，并补齐 `\\` 与 `.`。
- `0x005295F0` 和 `0x005276D0` 共用 INI 的 CRC/hash 缓存查找骨架，分别执行布尔值和整数值读取；这说明 INI 读取族应作为一组切片继续闭合，而不是把每个函数孤立处理。
- `0x0065CA80` 是 `RawFileClass` 构造函数，已确认对象初始字段和 vtable 写入。
- `0x007C978A` 是对 `0x7C970C` 返回值的布尔归一化包装，精确 API 名称仍待其调用者语义确认。

第四批原始汇编和反编译证据位于 `memory/data/callgraph/frontier_batch_004.txt`。当前排序总账仍包含全部 8655 个函数；已登记状态只改变少数已取证项，其余函数继续保持 `unclassified`，不代表跳过。

## 第八批 runtime 前沿取证

第八批继续沿句柄、CRT 错误状态和底层内存操作链推进，原始证据位于 `memory/data/callgraph/frontier_batch_008.txt`：

- `0x7D140B` 初始化线程局部 CRT error state，并在 TLS 分配期间保存/恢复 Win32 last-error。
- `0x7D161B` 将 Win32 错误码写入 CRT 的 DOS error、errno 两个字段；已登记为部分语义复现，错误码表仍需继续从相邻函数和运行时调用点核验。
- `0x7D95BF` 校验句柄索引及活动标志，失败时设置无效句柄错误并清零 DOS error。
- `0x7D4BF0` 是优化的前向 NUL 终止字符串复制；已加入 `string_copy_legacy`。
- `0x7D0A20` 是处理重叠区域的优化 `memmove`；已加入 `memmove_legacy`，以标准 C++ 库保留重叠复制语义。
- `0x7D777D` 是带全局引用计数和可选锁的线程安全包装器，底层 `0x7D77D6` 尚待取证，因此暂不伪造更高层实现。

本批新增的可独立验证 runtime 基元已接入 `code/rewrite/runtime`，并由 `test_runtime` 覆盖。TLS 错误状态模型记录为 C++ 等价模型，不能替代原版 ABI 级 TLS 布局；后续需要继续闭合 `0x7D140B`、`0x7D161B` 与句柄包装器之间的调用契约。

第九批继续沿该调用链取证：`0x7D77D6` 是单字符宽字符到窄字符输出函数，非转换模式直接接受小于 `0x100` 的字符，转换模式调用 `WideCharToMultiByte`；`0x7D2A74` 是句柄验证、加锁、底层操作、解锁的完整包装；`0x7D9D7F` 与 `0x7D777D` 共享 interlocked 引用计数及可选全局锁骨架。另重新确认 `0x7D161B`、`0x7D95BF`、`0x7D140B` 的证据，避免因重复取证制造新结论。`0x7D77D6` 已登记为部分 C++ 模型，其 Windows code-page 转换路径仍需平台层适配和独立对拍。

第十至十一批继续闭合句柄表生命周期：`0x7D93A1` 以每块 0x20 个、每槽 0x24 字节的布局扫描空槽，必要时分配 0x480 字节新块并初始化槽状态；`0x7D94C4` 把 native handle 写入尚未绑定的槽；`0x7D9540` 清除 active 槽的 native handle；`0x7D9601` 根据 open mode 和 `GetFileType` 合成槽 flags。C++ 模型新增 `HandleTable::bind`、`release_native_handle` 与 `create`，并用单元测试覆盖重复绑定、释放后失效、无效 native handle、句柄表满以及 open-mode/file-type flag 映射。进程标准句柄同步和原版分块扩容布局仍属于部分实现，未标记为完全复现。

第十二批新增 `0x7D930F`：这是 CRT 浮点分类辅助函数，直接按 IEEE-754 binary64 的 exponent、fraction 和 sign 位返回原版类别码，区分零、次正规数、普通数、无穷和 NaN。C++ `classify_double_legacy` 已通过正零、负零、正负普通数、无穷和 NaN 测试；该实现依赖 binary64 表示，未把平台浮点格式泛化为其他 ABI。

第十三批继续闭合浮点 runtime：`0x7D8DC8` 独立判断 IEEE-754 的正负无穷、NaN 编码，并区分 quiet/signaling NaN；C++ `classify_special_double_legacy` 已加入并覆盖普通数、正负无穷和 NaN。该分类器与 `0x7D930F` 分开登记，避免把特殊值分类与完整 double 分类混为同一个接口。

## 第十四批 runtime 浮点前沿

第十四批原始证据位于 `memory/data/callgraph/frontier_batch_014.txt`。`0x7D8EF5` 是 MSVC `__copysign`，仅按 binary64 位级语义保留第一个参数的 magnitude，并复制第二个参数的 sign bit；C++ `copysign_legacy` 已通过正负数、正负零、无穷和 NaN 测试，标记为完整行为模型。

同批还确认了两个相互配套但尚未实现的浮点辅助函数：`0x7D8D3B` 根据指数参数和源值的 sign/mantissa 字段重组 binary64，`0x7D8E22` 对非正规数执行左移规范化并通过输出指针返回调整后的指数。二者涉及 x87 `float10` 返回约定和调用者栈上 double 表示，当前先登记为部分语义确认，待按位测试后再接入 runtime，不能用普通 `std::scalbn` 代替而声称 ABI 等价。

## 第十五批 runtime 前沿

第十五批原始证据位于 `memory/data/callgraph/frontier_batch_015.txt`。`0x7D95BF` 是句柄表活动槽校验并返回 native handle 的入口，失败时写入 CRT invalid-handle errno（9）并清零 DOS error；C++ `HandleTable::validate_native_handle` 已覆盖有效槽和越界失败路径。`0x7D96A8`/`0x7D9707` 是句柄槽临界区的进入/离开包装，当前以 C++ 锁模型覆盖操作调用和释放。

同批 `0x7D75E0` 是 MSVC `_memset`，按 value 的低 8 位填充指定范围并返回原目的地址；`memset_legacy` 已接入低层 runtime 并覆盖低字节、零长度和返回值测试。`0x7D9D7F` 是 `byte_to_wide` 的线程安全引用计数包装，`0x7D7ED2` 则是 CRT 退出桩；二者已完成静态语义登记，前者仍需原版全局锁/引用计数 ABI，后者涉及进程终止行为，暂不在测试进程中直接执行。

## 第十六批 runtime/locale 前沿

第十六批原始证据位于 `memory/data/callgraph/frontier_batch_016.txt`。`0x7D7577` 是十六进制字符串解析器：数字字符保持原 ASCII 值，`A-F/a-f` 转为 10-15，然后按 `(accumulator - 3) * 16 + digit` 累积；该非标准行为已由 `parse_hex_legacy` 和测试固定。`0x7D9707` 的临界区退出按句柄索引定位每块 0x20 槽、每槽 0x24 字节的 `CRITICAL_SECTION`，当前已在第十五批登记。

`0x7D73DA` 检查语言标识是否不在内置的 10 个双字节表项中。`0x7D73F9` 负责把 locale 名称转换为数字标识，并在需要时将其与当前 locale 对比；`0x7D718A` 和 `0x7D729D` 初始化两组 locale 状态并调用 `EnumSystemLocalesA`。后三者依赖 Windows locale 回调、全局状态和系统环境，当前只登记静态语义，未伪造平台无关实现。

## 第十七批 runtime 字符串/流前沿

第十七批原始证据位于 `memory/data/callgraph/frontier_batch_017.txt`。`0x7D15A0` 是 MSVC 优化 `_strlen`，`0x7D75B0` 统计字符串开头连续的 ASCII 字母；对应的 `string_length_legacy` 与 `leading_alpha_length_legacy` 已接入 runtime 并通过 NUL 截断、字母/数字边界测试。

同批复验了 `0x7D7710`、`0x7D7638` 和 `0x7D7754` 的流缓冲初始化、句柄加锁分派及 `0x40` 标志查询语义。`0x7D9EF8` 是复杂的缓冲单字节写入：必要时初始化缓冲，处理 append seek，并在读写状态间切换；其结构字段与底层写入链已确认，但尚未建立足以声称等价的完整 stream ABI 模型。

## 第十八批 runtime/TLS/locale 前沿

第十八批原始证据位于 `memory/data/callgraph/frontier_batch_018.txt`。`0x7D168E` 和 `0x7D1697` 是当前线程 CRT error state 的两个字段地址包装器，分别对应 `+0x8` 与 `+0xC`；`errno_slot_legacy`、`dos_error_slot_legacy` 已实现并通过可写地址测试。`0x7D735A` 设置 locale 初始化标志并把 `GetUserDefaultLCID` 写入 current/default locale，`0x7D745B` 则检查 `GetVersionExA` 成功且 platform id 为 `VER_PLATFORM_WIN32_NT`。

`0x7D2169` 是缓冲输入流的单字节读取：剩余计数非负时推进游标，耗尽时调用 refill；`0x7D2183` 在句柄非 `-1` 时转发单字节写入 `0x7D9EF8`。这两个函数依赖尚未完整建模的 stream ABI，当前登记为部分语义确认，未在 C++ 层伪造不完整实现。

## 第十九批 runtime/句柄 I/O 前沿

第十九批原始证据位于 `memory/data/callgraph/frontier_batch_019.txt`。`0x7D2A74` 和 `0x7D0D55` 分别是关闭操作与三参数流操作的句柄校验/加锁/解锁包装器；`0x7D07DF` 是两参数流操作的同类包装器。三者复用句柄表活动位和 CRT 无效句柄错误状态。

`0x7D769D` 验证 native handle 后调用 `SetFilePointer`，成功时清除槽位的文本翻译状态，失败时保存系统错误并映射 CRT 错误。`0x7D0DBA` 是底层写入核心，包含 binary/text 分支、LF 到 CRLF 扩展、缓冲分块、部分写入和 Ctrl-Z 处理；`0x7D0844` 是对应的底层读取核心，包含 CRLF 归一化、跨块读取和 Ctrl-Z EOF 处理。`HandleTable::read` 已用可注入后端复现 binary/text 分支、同块 CRLF 归一化与 disk Ctrl-Z EOF；跨 `ReadFile` 边界的 CRLF 和真实 Win32 错误映射仍标为 partial。上述 I/O 函数依赖真实 Win32 handle 与原版 0x24 字节槽 ABI，当前仅登记为部分语义确认，避免用 POSIX 或抽象流替代后误称逐位复现。

## 第二十批 runtime/CRT 前沿

第二十批原始证据位于 `memory/data/callgraph/frontier_batch_020.txt`。`0x7D30D1` 是句柄校验、加锁和 `FlushFileBuffers` 包装器，已接入 `HandleTable::flush` 的 C++ 模型并覆盖成功与失败错误路径。

`0x7D0F45` 是 CRT 堆重分配入口：按 16 字节对齐请求，优先尝试原堆重分配，失败时分配新块并复制旧内容，最后支持 retry hook。`0x7D544A` 是文件打开入口，负责把 CRT open mode 映射为 `CreateFileA` 参数、分配句柄槽、识别 disk/character/pipe 类型，并处理文本模式 Ctrl-Z 初始化。`0x7D482C` 管理 CRT runtime mode 与控制台控制处理器，同时更新 locale 相关表状态。

`0x7D6D2A` 初始化 locale/code-page 元数据，`0x7D7374` 从 locale 名称取得 code page 并转换标准 locale 名称；这些函数都依赖 Windows 全局 locale 状态和 API 回调，当前只登记为部分语义确认，未伪造跨平台实现。

## 第二十一批 runtime/heap 前沿

第二十一批原始证据位于 `memory/data/callgraph/frontier_batch_021.txt`。`0x7D4B92` 在线性扫描 0x0C 字节记录数组时匹配 key，找不到返回空指针；该行为已由 `find_runtime_record_legacy` 复现并覆盖命中、未命中和空数组指针。`0x7D4C10` 经 Ghidra 确认不存在函数边界，已按 `NO_FUNCTION` 登记，不能据地址猜测语义。

`0x7DAC6C` 实现文件截断/扩展：通过 seek 计算差值，扩展时以 0x1000 字节零块循环写入，缩短时调用 `SetEndOfFile`，最后恢复文件位置。`0x7D06E8` 调用可选内存分配 retry callback 并将结果布尔化，已由 `invoke_allocation_retry_legacy` 复现并测试空回调、失败和成功路径。

`0x7CF7BD` 在 CRT heap segment 表中查找包含目标地址的一兆字节范围，`find_heap_segment_legacy` 已将该范围判定复现为可注入 segment-record 模型并覆盖边界；`0x7CFB13` 是基于 size-class bitmap、free list、boundary tag 和 segment growth 的堆块分配/拆分核心。后者依赖原版堆元数据布局，当前只登记为部分语义确认，不能用普通 `malloc` 冒充完整复现。

## 第二十三批 runtime/locale 前沿

第二十三批原始证据位于 `memory/data/callgraph/frontier_batch_023.txt`。`0x7C9B72` 的空白跳过、可选符号和十进制前缀累计已由 `parse_decimal_legacy` 建立 C++ 模型，但 locale/code-page 分类分支仍为 partial。`0x7C9CC2` 的 delimiter bitmap、连续分隔符跳过、原地 NUL 截断和 cursor 更新已由 `scan_token` 复现并测试。`0x7D468C` 已由回调模型复现负十进制值的 unsigned 标志转发；实际格式化引擎仍未在该函数中展开。

`0x7D6EA7` 已由 `binary_search_legacy` 复现排序数组二分查找和命中项 payload 指针更新，比较采用 ASCII 大小写折叠以匹配其调用的 CRT 不区分大小写比较函数。`0x7D6EFF` 负责 locale 全局状态初始化并调用 `EnumSystemLocalesA`，仍只完成静态语义登记；`0x7C9D14` 没有 Ghidra 函数边界，明确保持 `NO_FUNCTION`。上述实现/状态均已登记至 `frontier_status.tsv`，并通过完整 CTest。

## CRT 区策略收敛（2026-09-28）

第二十三批完成时，用户与本仓库共同收敛出一个**分区策略**：

- **主代码区**（`0x400000` 起，Westwood 手写）→ 行为级对拍复刻
- **CRT 区**（`0x7C-0x7D`，MSVC 6.0 静态链接）→ **行为断言**，不逐函数结构复刻
- **浮点语义层**（x87 中间精度 + `_control87` 全局舍入）→ 独立契约

CRT 断言清单落地在 `memory/data/callgraph/crt_behavior_contracts.md`，每条断言格式：
事实（登记地址+证据批次）/ 断言 / 状态（no_function / partial / 待验证 / 已验证(证据) / 须对拍）。

**战略影响**：这使之前起草的 `CrtHeap` 类降级——`0x7CFB13`（分配核心）与 `0x7CFFC8`（realloc 内部）、`0x7CF7E8`（free）、`0x7CFE1C`（段创建）、`0x7CFECD`（子段提交）不再追求结构化复刻，只保证 `0x7D0F45` 入口层暴露出来的四个行为（对齐/0 语义/失败回退/旧内容复制量）在现代 CRT 上等价即可。C++ 侧 `CrtHeap` 草稿已删除，节省的实现精力全部转投到行为断言验证和主代码对拍。

CRT 断言清单的第一轮已覆盖 `0x7D0F45`（realloc 入口）、`0x7C9430`（malloc）、`0x7C93E8`（free）、`0x7D30D1`（FlushFileBuffers 包装）、`0x7D06E8`（retry hook）、`0x7D544A`（`_open` 族）、`0x7D6D2A`/`0x7D7374`/`0x7D6EA7`/`0x7D468C`/`0x7D6EFF`/`0x7C9B72`/`0x7C9CC2`/`0x7C9D14`。`已验证` 的断言都给出具体测试文件与函数名；`partial` 的标出待回访的下游依赖。
