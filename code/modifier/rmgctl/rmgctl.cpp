// rmgctl —— rmg_hook.dll 的前端：注入/参数/点击/等待 dump/状态/卸载。
//
// 用法：
//   rmgctl inject [pid]                找 gamemd.exe（或指定 pid），注入 rmg_hook.dll
//   rmgctl arm --sed FILE              从 .sed 读 16 参数写入作业（之后每次生成都按它）
//   rmgctl disarm                      停止注入（生成回到游戏自身参数）
//   rmgctl setdump FILE                设置 rmg_done 的 cells dump 路径
//   rmgctl gen                         消息点击「产生地图」（后台可用）
//   rmgctl click X Y                   消息点击窗口坐标 (X,Y)（后台可用）
//   rmgctl click --real X Y            真实鼠标点击屏幕坐标（需前台，后备）
//   rmgctl waitdump [秒]               等 rmg_done dump 完成
//   rmgctl status                      打印作业状态
//   rmgctl eject                       卸载钩子（恢复原始字节，游戏无感）
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

#define JOB_NAME   "Local\\RMGHookJob2"
#define JOB_MAP_SIZE 0x10000
#define UNLOAD_EVT "Local\\RMGHookUnload"
#define DLL_PATH   "E:\\code\\ra2-reverse-AI\\code\\modifier\\build\\rmg_hook.dll"

#pragma pack(push, 1)
struct RmgJob {
    volatile uint32_t magic;
    volatile uint32_t cmd;
    volatile uint32_t params[16];
    volatile uint32_t status;
    volatile uint32_t inject_count;
    volatile uint32_t dump_count;
    char dump_path[260];
    volatile uint32_t rand_log[512];
    volatile uint32_t rand_log_obj[512];
    volatile uint32_t rand_log_main1[512];
    volatile uint32_t rand_log_main2[512];
    volatile uint32_t rand_log_n;
    volatile uint32_t rand_state_enable;
    uint8_t rand_state[0x3F4];
    volatile uint32_t pristine;
    volatile uint32_t dump_format;
    volatile uint32_t reset_scenario;
    volatile uint32_t tibdump;
    // 干净连生：非 0 时（需在 inject 前设置），hook 安装补丁强制每局走
    // 0x599650 完整重置路径 = 每局都是进程首局语义
    volatile uint32_t clean_regen;
};
#pragma pack(pop)

static HANDLE g_map = nullptr;
static RmgJob* g_job = nullptr;

static const char* kKeys[16] = {
    "Theater", "MapType", "Resources", "Ruggedness", "Time", "WaterAmount",
    "NumPlayers", "Tiberium", "TiberiumLayout", "Vegetation", "UrbanPresence",
    "Width", "Height", "Accessibility", "RegionSize", "Seed",
};

static uint32_t FindGame() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    uint32_t pid = 0;
    uint32_t fallback = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"gamemd.exe") == 0) {
                if (fallback == 0) fallback = pe.th32ProcessID;
                // 可能有僵尸进程（Syringe 挂起的壳，OpenProcess 拒绝）——
                // 优先返回能完整打开的实例
                HANDLE h = OpenProcess(PROCESS_CREATE_THREAD |
                                       PROCESS_VM_OPERATION |
                                       PROCESS_VM_WRITE | PROCESS_VM_READ |
                                       PROCESS_QUERY_INFORMATION,
                                       FALSE, pe.th32ProcessID);
                if (h != nullptr) {
                    // 僵尸壳进程 OpenProcess 能开但 VirtualAllocEx 拒绝——
                    // 试分配 4 字节才算数
                    void* probe = VirtualAllocEx(h, nullptr, 4,
                                                 MEM_COMMIT | MEM_RESERVE,
                                                 PAGE_READWRITE);
                    if (probe != nullptr) {
                        VirtualFreeEx(h, probe, 0, MEM_RELEASE);
                        pid = pe.th32ProcessID;
                        CloseHandle(h);
                        break;
                    }
                    CloseHandle(h);
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid != 0 ? pid : fallback;
}

static bool OpenJob(bool create) {
    g_map = create ? CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr,
                                        PAGE_READWRITE, 0, JOB_MAP_SIZE,
                                        JOB_NAME)
                   : OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, JOB_NAME);
    if (g_map == nullptr) return false;
    g_job = static_cast<RmgJob*>(MapViewOfFile(
        g_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(RmgJob)));
    if (g_job == nullptr) return false;
    if (create) {
        memset((void*)g_job, 0, sizeof(RmgJob));
        g_job->magic = 0x4A474D52u;   // 'RMGJ'
    }
    return true;
}

static int CmdInject(uint32_t pid) {
    if (pid == 0) pid = FindGame();
    if (pid == 0) {
        std::fprintf(stderr, "找不到 gamemd.exe\n");
        return 1;
    }
    if (!OpenJob(true)) {
        std::fprintf(stderr, "创建共享内存失败\n");
        return 1;
    }
    HANDLE evt = CreateEventA(nullptr, TRUE, FALSE, UNLOAD_EVT);
    if (evt != nullptr) ResetEvent(evt);

    HANDLE h = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
                               PROCESS_VM_WRITE | PROCESS_VM_READ |
                               PROCESS_QUERY_INFORMATION,
                           FALSE, pid);
    if (h == nullptr) {
        std::fprintf(stderr, "OpenProcess 失败 err=%lu\n", GetLastError());
        return 1;
    }
    char full[MAX_PATH];
    GetFullPathNameA(DLL_PATH, MAX_PATH, full, nullptr);
    SIZE_T len = std::strlen(full) + 1;
    void* remote = VirtualAllocEx(h, nullptr, len, MEM_COMMIT | MEM_RESERVE,
                                  PAGE_READWRITE);
    if (remote == nullptr ||
        !WriteProcessMemory(h, remote, full, len, nullptr)) {
        std::fprintf(stderr, "写路径失败 err=%lu\n", GetLastError());
        CloseHandle(h);
        return 1;
    }
    HANDLE th = CreateRemoteThread(
        h, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(LoadLibraryA), remote, 0,
        nullptr);
    if (th == nullptr) {
        std::fprintf(stderr, "CreateRemoteThread 失败 err=%lu\n",
                     GetLastError());
        CloseHandle(h);
        return 1;
    }
    WaitForSingleObject(th, 5000);
    CloseHandle(th);
    CloseHandle(h);
    // 等 Worker 装钩（status=1）
    for (int i = 0; i < 40; ++i) {
        if (g_job->status == 1) {
            std::printf("注入成功 pid=%lu，钩子已就位\n", pid);
            return 0;
        }
        if (g_job->status == 0xFFFFFFFFu) {
            std::fprintf(stderr, "装钩失败（WriteJump 返回 false）\n");
            return 1;
        }
        Sleep(250);
    }
    std::fprintf(stderr, "等待装钩超时\n");
    return 1;
}

static bool ParseSed(const char* path, uint32_t out[16]) {
    FILE* f = nullptr;
    if (fopen_s(&f, path, "r") != 0 || f == nullptr) return false;
    bool found[16] = {};
    char line[512];
    bool in_section = false;
    while (fgets(line, sizeof(line), f) != nullptr) {
        char* s = line;
        while (*s == ' ' || *s == '\t') ++s;
        if (*s == '[') {
            in_section = (strstr(s, "RandomMap") != nullptr);
            continue;
        }
        if (!in_section) continue;
        char* eq = strchr(s, '=');
        if (eq == nullptr) continue;
        *eq = 0;
        const char* val = eq + 1;
        for (int i = 0; i < 16; ++i) {
            if (_stricmp(s, kKeys[i]) == 0) {
                out[i] = static_cast<uint32_t>(strtoul(val, nullptr, 0));
                found[i] = true;
            }
        }
    }
    fclose(f);
    for (int i = 0; i < 16; ++i) {
        if (!found[i]) {
            std::fprintf(stderr, ".sed 缺键 %s\n", kKeys[i]);
            return false;
        }
    }
    return true;
}

// 找 gamemd 主窗口
static HWND FindGameWindow() {
    uint32_t pid = FindGame();
    if (pid == 0) return nullptr;
    struct Ctx { uint32_t pid; HWND hwnd; } ctx{pid, nullptr};
    EnumWindows([](HWND w, LPARAM lp) -> BOOL {
        Ctx* c = reinterpret_cast<Ctx*>(lp);
        DWORD wp = 0;
        GetWindowThreadProcessId(w, &wp);
        if (wp == c->pid && IsWindowVisible(w)) {
            c->hwnd = w;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.hwnd;
}

// 在整棵控件树里按控制 ID 找可见可用控件（对话框会重建，hwnd 每次不同，
// 控制 ID 稳定；多个页面叠放可能同 ID，取可见可用者）
static HWND FindControl(int id) {
    HWND main = FindGameWindow();
    if (main == nullptr) return nullptr;
    struct Ctx { int id; HWND hwnd; } ctx{id, nullptr};
    EnumChildWindows(main, [](HWND w, LPARAM lp) -> BOOL {
        Ctx* c = reinterpret_cast<Ctx*>(lp);
        if (GetDlgCtrlID(w) == c->id && IsWindowVisible(w) &&
            IsWindowEnabled(w)) {
            c->hwnd = w;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.hwnd;
}

// 窗口消息点击（后台可用）：(x,y) 为窗口坐标（含边框，与截图一致）
static bool MsgClick(int x, int y) {
    HWND hwnd = FindGameWindow();
    if (hwnd == nullptr) {
        std::fprintf(stderr, "找不到游戏窗口\n");
        return false;
    }
    RECT rc;
    GetWindowRect(hwnd, &rc);
    POINT pt{rc.left + x, rc.top + y};
    ScreenToClient(hwnd, &pt);
    LPARAM lp = MAKELPARAM(pt.x, pt.y);
    PostMessageA(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lp);
    Sleep(80);
    PostMessageA(hwnd, WM_LBUTTONUP, 0, lp);
    return true;
}

// 把 gamemd 主窗口提到前台（SetForegroundWindow 常被系统拒绝，
// 用 AttachThreadInput 挂到当前前台线程抢焦点）
static void BringGameForeground() {
    HWND hwnd = FindGameWindow();
    if (hwnd == nullptr) return;
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    DWORD fg_tid = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    DWORD my_tid = GetCurrentThreadId();
    DWORD gm_tid = GetWindowThreadProcessId(hwnd, nullptr);
    AttachThreadInput(my_tid, fg_tid, TRUE);
    AttachThreadInput(my_tid, gm_tid, TRUE);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    AttachThreadInput(my_tid, fg_tid, FALSE);
    AttachThreadInput(my_tid, gm_tid, FALSE);
    for (int i = 0; i < 20 && GetForegroundWindow() != hwnd; ++i) Sleep(50);
}

static void PrintStatus() {
    if (!OpenJob(false)) {
        std::printf("（无共享内存作业）\n");
        return;
    }
    std::printf("magic=%08X cmd=%u status=%d inject=%u dump=%u path=%s\n",
                g_job->magic, g_job->cmd, (int)g_job->status,
                g_job->inject_count, g_job->dump_count, g_job->dump_path);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("rmgctl inject|arm|disarm|setdump|gen|click|waitdump|status|eject\n");
        return 1;
    }
    const char* cmd = argv[1];
    if (_stricmp(cmd, "inject") == 0) {
        uint32_t pid = argc > 2 ? strtoul(argv[2], nullptr, 0) : 0;
        return CmdInject(pid);
    }
    if (_stricmp(cmd, "arm") == 0) {
        if (argc < 4 || _stricmp(argv[2], "--sed") != 0) {
            std::fprintf(stderr, "用法: rmgctl arm --sed FILE\n");
            return 1;
        }
        if (!OpenJob(false)) {
            std::fprintf(stderr, "先 inject\n");
            return 1;
        }
        uint32_t p[16];
        if (!ParseSed(argv[3], p)) return 1;
        for (int i = 0; i < 16; ++i) g_job->params[i] = p[i];
        g_job->cmd = 1;
        std::printf("已 arm：之后每次生成按 %s 的参数\n", argv[3]);
        return 0;
    }
    if (_stricmp(cmd, "disarm") == 0) {
        if (!OpenJob(false)) return 1;
        g_job->cmd = 0;
        std::printf("已 disarm\n");
        return 0;
    }
    if (_stricmp(cmd, "cleanregen") == 0) {
        // 干净连生开关：必须在 inject 之前设置（补丁在安装时写入）。
        // on 后每局 = 进程首局语义（原版自己的完整重置路径）。
        // OpenJob(true)：未注入时预创建共享内存，inject 时 hook 读取标志。
        if (argc < 3 || !OpenJob(true)) return 1;
        g_job->clean_regen = _stricmp(argv[2], "off") != 0 ? 1 : 0;
        std::printf("干净连生 %s（下次 inject 生效）\n",
                    g_job->clean_regen ? "开" : "关");
        return 0;
    }
    if (_stricmp(cmd, "setdump") == 0) {
        if (argc < 3 || !OpenJob(false)) return 1;
        strncpy_s(g_job->dump_path, sizeof(g_job->dump_path), argv[2],
                  _TRUNCATE);
        std::printf("dump 路径 = %s\n", g_job->dump_path);
        return 0;
    }
    if (_stricmp(cmd, "click") == 0) {
        if (argc < 4) return 1;
        if (_stricmp(argv[2], "--real") == 0) {
            // 真实鼠标（屏幕坐标，需前台），仅作后备
            if (argc < 5) return 1;
            int x = atoi(argv[3]), y = atoi(argv[4]);
            BringGameForeground();
            SetCursorPos(x, y);
            Sleep(150);
            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
            Sleep(80);
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            return 0;
        }
        // 默认：窗口消息点击（后台可用），(x,y) 窗口坐标
        if (!MsgClick(atoi(argv[2]), atoi(argv[3]))) return 1;
        return 0;
    }
    if (_stricmp(cmd, "waitmenu") == 0) {
        // 等游戏停在 RMG 页（「预览地图」0x0620 出现），最长 secs 秒
        int secs = argc > 2 ? atoi(argv[2]) : 3600;
        for (int i = 0; i < secs * 2; ++i) {
            if (FindControl(0x0620) != nullptr) {
                std::printf("RMG 页面就绪\n");
                return 0;
            }
            Sleep(500);
        }
        std::fprintf(stderr, "等 RMG 页面超时\n");
        return 1;
    }
    if (_stricmp(cmd, "gen") == 0) {
        // 「预览地图」(0x0620)：触发生成（生成后画小地图预览，无模态弹窗）。
        // 注意不是「替我挑选」(0x0621)——它只随机化对话框参数不生成。
        // BM_CLICK 纯消息，游戏在后台、用户动鼠标都不影响。
        HWND btn = FindControl(0x0620);
        if (btn == nullptr) {
            std::fprintf(stderr, "找不到「预览地图」按钮（游戏不在随机地图页？）\n");
            return 1;
        }
        SendMessageA(btn, BM_CLICK, 0, 0);
        std::printf("已点击「预览地图」触发生成\n");
        return 0;
    }
    if (_stricmp(cmd, "waitdump") == 0) {
        int secs = argc > 2 ? atoi(argv[2]) : 300;
        if (!OpenJob(false)) return 1;
        uint32_t base = g_job->dump_count;
        for (int i = 0; i < secs * 2; ++i) {
            if (g_job->dump_count > base) {
                std::printf("dump 完成（第 %u 次）\n", g_job->dump_count);
                return 0;
            }
            Sleep(500);
        }
        std::fprintf(stderr, "等待 dump 超时\n");
        return 1;
    }
    if (_stricmp(cmd, "randlog") == 0) {
        if (!OpenJob(false)) return 1;
        uint32_t n = g_job->rand_log_n;
        if (n > 512) n = 512;
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t e = g_job->rand_log[i];
            uint32_t id = e >> 29;
            const char* tag = id == 0 ? "next  " : id == 1 ? "ranged" :
                              id == 2 ? "ctor  " : id == 3 ? "entry " :
                              "techm ";
            std::printf("%s 0x%06X  obj=0x%08X  arg/main=(%u,%u)\n",
                        tag, e & 0x1FFFFFFFu,
                        g_job->rand_log_obj[i],
                        g_job->rand_log_main1[i], g_job->rand_log_main2[i]);
        }
        std::printf("（共 %u 条，已清零）\n", g_job->rand_log_n);
        g_job->rand_log_n = 0;
        return 0;
    }
    if (_stricmp(cmd, "fixstream") == 0) {
        // 固定副随机流：FILE 为 0x3F4 字节 Randomizer 原始快照；
        // "off" 解除固定
        if (argc < 3 || !OpenJob(false)) return 1;
        if (_stricmp(argv[2], "off") == 0) {
            g_job->rand_state_enable = 0;
            std::printf("已解除副随机流固定\n");
            return 0;
        }
        FILE* f = nullptr;
        if (fopen_s(&f, argv[2], "rb") != 0 || f == nullptr) {
            std::fprintf(stderr, "打不开 %s\n", argv[2]);
            return 1;
        }
        size_t n = fread((void*)g_job->rand_state, 1, 0x3F4, f);
        fclose(f);
        if (n != 0x3F4) {
            std::fprintf(stderr, "文件应为 0x3F4 字节，实读 %zu\n", n);
            return 1;
        }
        g_job->rand_state_enable = 1;
        std::printf("副随机流已固定（每次生成入口覆写）\n");
        return 0;
    }
    if (_stricmp(cmd, "pristine") == 0) {
        if (argc < 3 || !OpenJob(false)) return 1;
        // off=0；on=1；数字直通（2 = 批量模式，额外 NOP 渲染链，
        // 规避 ts-ddraw ra2md.dll+0x2BAA 分配崩溃）
        if (_stricmp(argv[2], "off") == 0) {
            g_job->pristine = 0;
        } else if (_stricmp(argv[2], "on") == 0) {
            g_job->pristine = 1;
        } else {
            g_job->pristine = static_cast<uint32_t>(atoi(argv[2]));
        }
        std::printf("干净模式 %u\n", g_job->pristine);
        return 0;
    }
    if (_stricmp(cmd, "dumpfmt") == 0) {
        if (argc < 3 || !OpenJob(false)) return 1;
        g_job->dump_format = atoi(argv[2]);
        std::printf("dump 格式 = %uB/格\n", g_job->dump_format);
        return 0;
    }
    if (_stricmp(cmd, "tibdump") == 0) {
        if (argc < 3 || !OpenJob(false)) return 1;
        g_job->tibdump = _stricmp(argv[2], "off") != 0 ? 1 : 0;
        std::printf("布矿前后 dump %s\n", g_job->tibdump ? "开" : "关");
        return 0;
    }
    if (_stricmp(cmd, "reset") == 0) {
        // 官方场景重置：向游戏主窗口发 WM_RMG_RESET，游戏 WndProc 在
        // 消息边界调 Scenario_ClearClasses。同步等待完成。
        if (!OpenJob(false)) return 1;
        HWND hwnd = FindGameWindow();
        if (hwnd == nullptr) {
            std::fprintf(stderr, "找不到游戏窗口\n");
            return 1;
        }
        uint32_t base = g_job->reset_scenario;
        DWORD_PTR result = 0;
        LRESULT ok = SendMessageTimeoutA(hwnd, WM_APP + 0x42, 0, 0,
                                         SMTO_BLOCK, 30000, &result);
        if (ok == 0 || g_job->reset_scenario == base) {
            std::fprintf(stderr, "重置超时/未回执（游戏忙或未注入）\n");
            return 1;
        }
        std::printf("场景已重置（第 %u 次）\n", g_job->reset_scenario);
        return 0;
    }
    if (_stricmp(cmd, "status") == 0) {
        PrintStatus();
        return 0;
    }
    if (_stricmp(cmd, "eject") == 0) {
        HANDLE evt = OpenEventA(EVENT_MODIFY_STATE, FALSE, UNLOAD_EVT);
        if (evt != nullptr) {
            SetEvent(evt);
            CloseHandle(evt);
        }
        std::printf("已请求卸载\n");
        return 0;
    }
    std::fprintf(stderr, "未知命令 %s\n", cmd);
    return 1;
}
