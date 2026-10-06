// 本地加载测试：LoadLibrary rmg_hook.dll 并捕获异常地址
#include <windows.h>
#include <cstdio>

static DWORD Filter(EXCEPTION_POINTERS* ep) {
    std::printf("EXCEPTION code=0x%08lX addr=0x%p\n",
                ep->ExceptionRecord->ExceptionCode,
                ep->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_EXECUTE_HANDLER;
}

int main() {
    __try {
        HMODULE m = LoadLibraryA(
            "E:\\code\\ra2-reverse-AI\\code\\modifier\\build\\rmg_hook.dll");
        std::printf("LoadLibrary -> 0x%p err=%lu\n", (void*)m, GetLastError());
        if (m != nullptr) {
            std::printf("loaded OK\n");
            Sleep(2000);
            std::printf("survived 2s\n");
        }
    } __except (Filter(GetExceptionInformation())) {
    }
    return 0;
}
