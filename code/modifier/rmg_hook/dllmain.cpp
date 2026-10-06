// rmg_hook.dll —— gamemd.exe 进程内 RMG 参数注入 / cells dump 钩子。
//
// 机制（全部来自 2026-10-06 取证）：
//   · 0x598960（RMG 入口）inline hook：把共享内存里的 16 个参数写进
//     MapSeedClass(0xABDFD8)+0x38..+0x74，游戏按参数生成
//   · 0x5994B0（rmg_done）inline hook：进程内直接把 512x512 cells
//     按 9B/格（ex,iso,sub,lv,ramp,od,ov）dump 到作业指定的文件
//   · 控制面：命名共享内存 Local\RMGHookJob（RmgJob 结构）+
//     命名事件 Local\RMGHookUnload（置位即卸载并恢复原始字节）
//
// 与调试器方案的本质区别：DLL 常驻游戏进程，工具（rmgctl）死活与游戏无关；
// 卸载时恢复被覆盖的原始指令字节，游戏无感知。
#include <windows.h>
#include <cstdio>
#include <cstdint>

#define RMG_ENTRY      0x598960
#define RMG_DONE       0x5994B0
#define RAND_NEXT      0x65C780
#define RAND_RANGED    0x65C7E0
#define TECHNO_CTOR    0x6F2B40
#define TECH_COUNT     0x5A966A
#define CLEAR_CLASSES  0x6851F0
#define SEC_OBJ        0xA8B230
#define MAPSEED        0xABDFD8
#define CELLTBL        0x87F924
#define JOB_NAME       "Local\\RMGHookJob2"
#define JOB_MAP_SIZE   0x10000
#define UNLOAD_EVT     "Local\\RMGHookUnload"
#define WM_RMG_RESET   (WM_APP + 0x42)

#pragma pack(push, 1)
struct RmgJob {
    volatile uint32_t magic;          // 'RMGJ'
    volatile uint32_t cmd;            // 0=旁路 1=下次生成注入参数
    volatile uint32_t params[16];     // +0x38..+0x74
    volatile uint32_t status;         // 1=已装钩 2=已注入 3=已 dump
    volatile uint32_t inject_count;
    volatile uint32_t dump_count;
    char dump_path[260];
    // 副随机流消耗取证：bit31=钩子号（0=next 1=ranged），低 31 位=调用者返回地址；
    // 同时记录当时主 RNG (0xABE890) 的 idx1/idx2，用来定位发生在生成哪个阶段
    volatile uint32_t rand_log[512];
    volatile uint32_t rand_log_obj[512];
    volatile uint32_t rand_log_main1[512];
    volatile uint32_t rand_log_main2[512];
    volatile uint32_t rand_log_n;
    // 固定副随机流：enable!=0 时，RMG 入口把 [0xA8B230]+0x218 整个
    // Randomizer（0x3F4 字节）覆写为 rand_state——生成变体随之确定
    volatile uint32_t rand_state_enable;
    uint8_t rand_state[0x3F4];
    // 干净模式：非 0 时，RMG 入口把全图格子的占用者指针(+0xE4)和
    // overlay(+0x44) 清零——消除上一局残留对生成的影响，等价于
    // 进程启动后的首次生成
    volatile uint32_t pristine;
    // dump 格式：9 = 9B/格（ex,iso,sub,lv,ramp,od,ov16）；
    // 12 = 12B/格（追加 obj占用/+0xEC/+0x4C），供回放模式装载
    volatile uint32_t dump_format;
    // 官方场景重置计数：rmgctl 发 WM_RMG_RESET 消息，游戏 WndProc 在
    // 消息边界调 Scenario_ClearClasses(0x6851F0) 后 +1（回执）。
    // 调用后格子表全 NULL + 对象列表/House/Waypoint/Logic 全清 =
    // 开机首局状态，游戏进程永不重启
    volatile uint32_t reset_scenario;
};
#pragma pack(pop)

static RmgJob* g_job = nullptr;
static HANDLE g_map = nullptr;
static HANDLE g_unload_evt = nullptr;
static HWND g_game_wnd = nullptr;
static WNDPROC g_old_wndproc = nullptr;

// 主窗口 WndProc 子类：在消息边界（主线程、非回调嵌套）执行
// Scenario_ClearClasses——官方场景重置。在 BN_CLICK 回调内直调会崩
// （c000041d 回调内致命异常），消息边界是安全调用点。
static LRESULT CALLBACK RmgWndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_RMG_RESET) {
        reinterpret_cast<void (*)()>(CLEAR_CLASSES)();
        if (g_job != nullptr) ++g_job->reset_scenario;  // 重置计数（回执）
        return 0;
    }
    return CallWindowProcA(g_old_wndproc, w, msg, wp, lp);
}

static void SubclassGameWindow() {
    DWORD pid = GetCurrentProcessId();
    struct Ctx { DWORD pid; HWND hwnd; } ctx{pid, nullptr};
    EnumWindows([](HWND w, LPARAM lp) -> BOOL {
        Ctx* c = reinterpret_cast<Ctx*>(lp);
        DWORD wp = 0;
        GetWindowThreadProcessId(w, &wp);
        if (wp == c->pid && IsWindowVisible(w) &&
            GetWindowTextLengthA(w) > 0) {
            c->hwnd = w;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    if (ctx.hwnd != nullptr) {
        g_game_wnd = ctx.hwnd;
        g_old_wndproc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(
            ctx.hwnd, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(&RmgWndProc)));
    }
}
static uint8_t g_orig_entry[6];
static uint8_t g_orig_done[5];
static uint8_t g_orig_rand_next[5];
static uint8_t g_orig_rand_ranged[6];
static uint8_t g_orig_techno[6];
static uint8_t g_orig_techcount[5];
static bool g_hooked = false;

// ── cells dump（在 rmg_done 钩子里进程内执行）──
static void DumpCells() {
    if (g_job->dump_path[0] == 0) return;
    uint32_t* tbl = *reinterpret_cast<uint32_t**>(CELLTBL);
    if (tbl == nullptr) return;
    FILE* f = nullptr;
    if (fopen_s(&f, g_job->dump_path, "wb") != 0 || f == nullptr) return;
    bool ext = g_job->dump_format == 12;
    for (uint32_t i = 0; i < 0x40000; ++i) {
        uint8_t rec[12] = {0};
        const uint8_t* cell = reinterpret_cast<const uint8_t*>(tbl[i]);
        if (cell != nullptr) {
            rec[0] = 1;
            rec[1] = cell[0x38];
            rec[2] = cell[0x39];
            rec[3] = cell[0x11A];
            rec[4] = cell[0x11B];
            rec[5] = cell[0x11C];
            rec[6] = cell[0x11E];
            rec[7] = cell[0x44];
            rec[8] = cell[0x45];
            if (ext) {
                rec[9] = *reinterpret_cast<const uint32_t*>(cell + 0xE4) != 0;
                rec[10] = cell[0xEC];
                rec[11] = cell[0x4C];
            }
        }
        fwrite(rec, 1, ext ? 12 : 9, f);
    }
    fclose(f);
}

// ── 参数注入（在 RMG 入口钩子里进程内执行）──
static void InjectParams() {
    // 入口主 RNG 快照（id=3，总是记录，用于判定主 RNG 是否逐局重播种）
    uint32_t n0 = g_job->rand_log_n;
    if (n0 < 512) {
        g_job->rand_log[n0] = (3u << 29);
        g_job->rand_log_obj[n0] = 0;
        g_job->rand_log_main1[n0] =
            *reinterpret_cast<uint32_t*>(0xABE890 + 4);
        g_job->rand_log_main2[n0] =
            *reinterpret_cast<uint32_t*>(0xABE890 + 8);
        g_job->rand_log_n = n0 + 1;
    }
    // cmd!=1 时完全旁路——绝不把脏/零参数写进 MapSeedClass
    // （全零参数会让生成死循环卡死游戏）
    if (g_job->cmd != 1) return;
    volatile uint32_t* dst = reinterpret_cast<volatile uint32_t*>(MAPSEED + 0x38);
    for (int i = 0; i < 16; ++i) dst[i] = g_job->params[i];
    if (g_job->rand_state_enable) {
        uint32_t obj = *reinterpret_cast<uint32_t*>(SEC_OBJ);
        if (obj != 0) {
            memcpy(reinterpret_cast<void*>(obj + 0x218),
                   const_cast<uint8_t*>(g_job->rand_state), 0x3F4);
        }
    }
    if (g_job->pristine) {
        // 把格子表整个置 NULL（内存泄漏可忽略）——模拟开机未分配状态，
        // 让生成走「首次分配」路径。注意：清字段（+0xE4/overlay）会崩，
        // 因为对象还挂在全局列表里；置 NULL 后旧格子整体被遗弃。
        uint32_t* tbl = *reinterpret_cast<uint32_t**>(CELLTBL);
        if (tbl != nullptr) {
            memset(tbl, 0, 0x40000 * sizeof(uint32_t));
        }
    }
    ++g_job->inject_count;
    g_job->status = 2;
}

// DumpCells 的裸函数安全包装（声明前置，供 __asm 引用）
static void DumpCellsTramp();

// ── 跳板代码 ──
// 0x598960 原始前 6 字节: 81 EC 18 04 00 00  (sub esp, 0x418)
__declspec(naked) static void HookEntry() {
    __asm {
        pushad
        call InjectParams
        popad
        sub esp, 0x418
        push 0x598966
        ret
    }
}

// 0x5994B0 原始前 5 字节: 68 E4 BD 82 00  (push 0x82BDE4)
__declspec(naked) static void HookDone() {
    __asm {
        pushad
        call DumpCellsTramp
        popad
        push 0x82BDE4
        push 0x5994B5
        ret
    }
}

// DumpCells 本身可能被优化内联出问题，用独立包装保证裸函数里安全调用
static void DumpCellsTramp() {
    DumpCells();
    ++g_job->dump_count;
    g_job->status = 3;
}

// ── 副随机流消耗取证 ──
// 0x65C780 next / 0x65C7E0 RandomRanged 都是 thiscall（ecx=Randomizer*）。
// 过滤 ecx == [0xA8B230]+0x218（Scenario 内嵌副随机流），记录调用者返回地址。
static void RandLog(uint32_t retaddr, uint32_t thisptr, uint32_t hook_id) {
    uint32_t obj = *reinterpret_cast<uint32_t*>(SEC_OBJ);
    if (obj == 0 || thisptr != obj + 0x218) return;
    uint32_t n = g_job->rand_log_n;
    if (n < 512) {
        g_job->rand_log[n] = (hook_id << 29) | retaddr;
        g_job->rand_log_obj[n] = thisptr;
        uint32_t main_rng = 0xABE890;
        g_job->rand_log_main1[n] =
            *reinterpret_cast<uint32_t*>(main_rng + 4);
        g_job->rand_log_main2[n] =
            *reinterpret_cast<uint32_t*>(main_rng + 8);
        g_job->rand_log_n = n + 1;
    }
}

// 0x65C780 原始前 5 字节: 80 39 00 74 03  (cmp byte [ecx],0; je +3)
__declspec(naked) static void HookRandNext() {
    __asm {
        pushad
        mov eax, [esp + 32]
        push 0
        push ecx
        push eax
        call RandLog
        add esp, 12
        popad
        cmp byte ptr [ecx], 0
        je hrn_work
        push 0x65C785
        ret
hrn_work:
        push 0x65C788
        ret
    }
}

// 0x65C7E0 原始前 6 字节: 8B 44 24 04 8B D1  (mov eax,[esp+4]; mov edx,ecx)
__declspec(naked) static void HookRandRanged() {
    __asm {
        pushad
        mov eax, [esp + 32]
        push 1
        push ecx
        push eax
        call RandLog
        add esp, 12
        popad
        mov eax, dword ptr [esp + 4]
        mov edx, ecx
        push 0x65C7E6
        ret
    }
}

// TechnoClass 构造函数入口取证：hook_id=2，obj=ecx(this)，
// main1 槽位改存构造参数（[esp+4]，疑似类型 ID），main2 存主 RNG idx1
static void CtorLog(uint32_t retaddr, uint32_t thisptr, uint32_t arg) {
    uint32_t n = g_job->rand_log_n;
    if (n < 512) {
        g_job->rand_log[n] = (2u << 29) | retaddr;
        g_job->rand_log_obj[n] = thisptr;
        g_job->rand_log_main1[n] = arg;
        g_job->rand_log_main2[n] =
            *reinterpret_cast<uint32_t*>(0xABE890 + 4);
        g_job->rand_log_n = n + 1;
    }
}

// 0x6F2B40 原始前 6 字节: 53 55 56 57 8B F1 (push ebx/ebp/esi/edi; mov esi,ecx)
__declspec(naked) static void HookTechnoCtor() {
    __asm {
        pushad
        mov eax, [esp + 32]       // 返回地址（调用者）
        mov edx, [esp + 36]       // 构造参数
        push edx
        push ecx
        push eax
        call CtorLog
        add esp, 12
        popad
        push ebx
        push ebp
        push esi
        push edi
        mov esi, ecx
        push 0x6F2B46
        ret
    }
}

// 0x5A966A（阶段 7 撒几栋的 do-while 判定处）原始前 5 字节:
//   83 F8 04 77 D6  (cmp eax,4; ja 0x5A9645)
// eax = m 候选值。id=4：main1=eax，main2=主 RNG idx1
static void TechCountLog(uint32_t m_val) {
    uint32_t n = g_job->rand_log_n;
    if (n < 512) {
        g_job->rand_log[n] = (4u << 29);
        g_job->rand_log_obj[n] = 0;
        g_job->rand_log_main1[n] = m_val;
        g_job->rand_log_main2[n] =
            *reinterpret_cast<uint32_t*>(0xABE890 + 4);
        g_job->rand_log_n = n + 1;
    }
}

__declspec(naked) static void HookTechCount() {
    __asm {
        pushad
        push eax
        call TechCountLog
        add esp, 4
        popad
        cmp eax, 4
        ja htc_retry
        push 0x5A966F
        ret
htc_retry:
        push 0x5A9645
        ret
    }
}

static bool WriteJump(uintptr_t at, uintptr_t to, uint8_t* save,
                      SIZE_T patch_len) {
    DWORD old_prot;
    if (!VirtualProtect(reinterpret_cast<void*>(at), patch_len,
                        PAGE_EXECUTE_READWRITE, &old_prot)) {
        return false;
    }
    if (save != nullptr) {
        memcpy(save, reinterpret_cast<const void*>(at), patch_len);
    }
    uint8_t* p = reinterpret_cast<uint8_t*>(at);
    p[0] = 0xE9;   // JMP rel32
    int32_t rel = static_cast<int32_t>(to - (at + 5));
    memcpy(p + 1, &rel, 4);
    for (SIZE_T i = 5; i < patch_len; ++i) p[i] = 0x90;
    VirtualProtect(reinterpret_cast<void*>(at), patch_len, old_prot,
                   &old_prot);
    return true;
}

static void Restore(uintptr_t at, const uint8_t* orig, SIZE_T len) {
    DWORD old_prot;
    VirtualProtect(reinterpret_cast<void*>(at), len, PAGE_EXECUTE_READWRITE,
                   &old_prot);
    memcpy(reinterpret_cast<void*>(at), orig, len);
    VirtualProtect(reinterpret_cast<void*>(at), len, old_prot, &old_prot);
}

static DWORD WINAPI Worker(LPVOID) {
    g_hooked = WriteJump(RMG_ENTRY,
                         reinterpret_cast<uintptr_t>(&HookEntry),
                         g_orig_entry, 6);
    if (g_hooked) {
        g_hooked = WriteJump(RMG_DONE,
                             reinterpret_cast<uintptr_t>(&HookDone),
                             g_orig_done, 5);
    }
    // 副随机流取证钩：独立成败，不影响主钩
    bool hr1 = WriteJump(RAND_NEXT,
                         reinterpret_cast<uintptr_t>(&HookRandNext),
                         g_orig_rand_next, 5);
    bool hr2 = WriteJump(RAND_RANGED,
                         reinterpret_cast<uintptr_t>(&HookRandRanged),
                         g_orig_rand_ranged, 6);
    bool hr3 = WriteJump(TECHNO_CTOR,
                         reinterpret_cast<uintptr_t>(&HookTechnoCtor),
                         g_orig_techno, 6);
    bool hr4 = WriteJump(TECH_COUNT,
                         reinterpret_cast<uintptr_t>(&HookTechCount),
                         g_orig_techcount, 5);
    g_job->status = g_hooked ? 1 : 0xFFFFFFFF;
    SubclassGameWindow();

    if (g_unload_evt != nullptr) {
        WaitForSingleObject(g_unload_evt, INFINITE);
    }
    if (g_game_wnd != nullptr && g_old_wndproc != nullptr) {
        SetWindowLongPtrA(g_game_wnd, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(g_old_wndproc));
        g_game_wnd = nullptr;
    }
    if (g_hooked) {
        Restore(RMG_ENTRY, g_orig_entry, 6);
        Restore(RMG_DONE, g_orig_done, 5);
    }
    if (hr1) Restore(RAND_NEXT, g_orig_rand_next, 5);
    if (hr2) Restore(RAND_RANGED, g_orig_rand_ranged, 6);
    if (hr3) Restore(TECHNO_CTOR, g_orig_techno, 6);
    if (hr4) Restore(TECH_COUNT, g_orig_techcount, 5);
    if (g_job != nullptr) {
        g_job->status = 0;
        g_job->magic = 0;
        UnmapViewOfFile(g_job);
        g_job = nullptr;
    }
    if (g_map != nullptr) CloseHandle(g_map);
    if (g_unload_evt != nullptr) CloseHandle(g_unload_evt);
    FreeLibraryAndExitThread(GetModuleHandleA("rmg_hook.dll"), 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hMod);
        // 由 DLL 自己创建/持有共享内存与卸载事件——生命周期跟随 DLL，
        // 不依赖注入方进程是否存活（trainer 模式的关键）。
        HANDLE map = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr,
                                        PAGE_READWRITE, 0, JOB_MAP_SIZE,
                                        JOB_NAME);
        if (map == nullptr) return FALSE;
        g_map = map;
        g_job = static_cast<RmgJob*>(MapViewOfFile(
            map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(RmgJob)));
        if (g_job == nullptr) return FALSE;
        if (g_job->magic != 0x4A474D52u) {
            memset((void*)g_job, 0, sizeof(RmgJob));
            g_job->magic = 0x4A474D52u;
        }
        g_unload_evt = CreateEventA(nullptr, TRUE, FALSE, UNLOAD_EVT);
        if (g_unload_evt != nullptr) ResetEvent(g_unload_evt);
        HANDLE th = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
        if (th != nullptr) CloseHandle(th);
    }
    return TRUE;
}
