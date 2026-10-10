# -*- coding: utf-8 -*-
"""启动 x32dbg，自动附加当前 gamemd.exe 并设置随机地图阶段断点。"""
from __future__ import print_function

import ctypes
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.abspath(__file__))
DEBUGGER = r"E:\code\x64dbg\release\x32\x32dbg.exe"
GAME_NAME = "gamemd.exe"
ADDRESSES = (
    0x598960,
    0x598B14, 0x598B19, 0x598D44,
    0x58D7AD,
    0x598D67, 0x598D6C, 0x598D73, 0x598D80, 0x598D87,
    0x598E5B, 0x598EB0, 0x598EBF, 0x599172,
    0x599216, 0x599355, 0x5994B1,
)


def find_game_pid():
    snapshot = ctypes.windll.kernel32.CreateToolhelp32Snapshot(0x2, 0)
    if snapshot == ctypes.c_void_p(-1).value:
        return None

    class ProcessEntry(ctypes.Structure):
        _fields_ = [
            ("size", ctypes.c_ulong), ("usage", ctypes.c_ulong),
            ("pid", ctypes.c_ulong), ("heap", ctypes.c_void_p),
            ("module", ctypes.c_ulong), ("threads", ctypes.c_ulong),
            ("parent", ctypes.c_ulong), ("priority", ctypes.c_long),
            ("flags", ctypes.c_ulong), ("name", ctypes.c_wchar * 260),
        ]

    entry = ProcessEntry()
    entry.size = ctypes.sizeof(entry)
    try:
        ok = ctypes.windll.kernel32.Process32FirstW(snapshot, ctypes.byref(entry))
        while ok:
            if entry.name.lower() == GAME_NAME:
                return int(entry.pid)
            ok = ctypes.windll.kernel32.Process32NextW(snapshot, ctypes.byref(entry))
    finally:
        ctypes.windll.kernel32.CloseHandle(snapshot)
    return None


def main():
    if not os.path.isfile(DEBUGGER):
        print("x32dbg not found: %s" % DEBUGGER)
        return 1
    pid = find_game_pid()
    if pid is None:
        print("gamemd.exe not found")
        return 1
    # x32dbg 断点仍由 bp 命令安装；监听器消费 x32dbg 写入的命中事件日志。
    event_log = os.path.join(ROOT, "snapshot_gamemd_62298", "x32dbg_rmg_events.log")
    commands = "logopen \"%s\"" % event_log.replace("\\", "/")
    commands += ";" + ";".join(
        "bp 0x%08X;SetBreakpointCommand 0x%08X,\"log 'RMG_BP 0x%08X'\"" %
        (address, address, address) for address in ADDRESSES)
    commands += ";pause"
    args = [DEBUGGER, "-pid", str(pid), "-c", commands]
    print("launching x32dbg pid=%d" % pid)
    print("breakpoints=%s" % ", ".join("0x%08X" % address for address in ADDRESSES))
    print("ScyllaHide + x32rmgdump must be loaded from x32\\plugins before attach")
    sys.stdout.flush()
    try:
        subprocess.Popen(args, cwd=os.path.dirname(DEBUGGER))
    except OSError as exc:
        print("failed to launch x32dbg: %s" % exc)
        return 1
    time.sleep(1.0)
    print("x32dbg launched; check plugins log for x32rmgdump: loaded")
    return 0


if __name__ == "__main__":
    sys.exit(main())
