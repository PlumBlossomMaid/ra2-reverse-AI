# -*- coding: utf-8 -*-
"""把当前 gamemd.exe 的 MapSeedClass::Seed 改成指定值并读回核验。"""
from __future__ import print_function

import ctypes
import ctypes.wintypes as wt
import struct
import sys

GAME_NAME = "gamemd.exe"
MAPSEED = 0xABDFD8
PARAMETERS = {
    0x38: 0,       # Theater
    0x3C: 3,       # MapType
    0x40: 3,       # Resources
    0x44: 31,      # Ruggedness
    0x48: 0,       # Time
    0x4C: 30,      # WaterAmount
    0x50: 2,       # NumPlayers
    0x54: 60,      # Tiberium
    0x58: 52,      # TiberiumLayout
    0x5C: 83,      # Vegetation
    0x60: 65,      # UrbanPresence
    0x64: 0,       # Width
    0x68: 0,       # Height
    0x6C: 39,      # Accessibility
    0x70: 68,      # RegionSize
    0x74: 62298,   # Seed
}
PROCESS_VM_READ = 0x0010
PROCESS_VM_WRITE = 0x0020
PROCESS_VM_OPERATION = 0x0008
PROCESS_QUERY_INFORMATION = 0x0400
TH32CS_SNAPPROCESS = 0x00000002
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
K = ctypes.windll.kernel32


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", wt.DWORD),
        ("cntUsage", wt.DWORD),
        ("th32ProcessID", wt.DWORD),
        ("th32DefaultHeapID", ctypes.c_void_p),
        ("th32ModuleID", wt.DWORD),
        ("cntThreads", wt.DWORD),
        ("th32ParentProcessID", wt.DWORD),
        ("pcPriClassBase", ctypes.c_long),
        ("dwFlags", wt.DWORD),
        ("szExeFile", wt.WCHAR * 260),
    ]


def find_pid():
    snapshot = K.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if snapshot == INVALID_HANDLE_VALUE:
        return None
    entry = PROCESSENTRY32W()
    entry.dwSize = ctypes.sizeof(entry)
    try:
        ok = K.Process32FirstW(snapshot, ctypes.byref(entry))
        while ok:
            if entry.szExeFile.lower() == GAME_NAME:
                return int(entry.th32ProcessID)
            ok = K.Process32NextW(snapshot, ctypes.byref(entry))
    finally:
        K.CloseHandle(snapshot)
    return None


def read_u32(handle, address):
    data = ctypes.create_string_buffer(4)
    got = ctypes.c_size_t()
    if not K.ReadProcessMemory(handle, ctypes.c_void_p(address), data, 4, ctypes.byref(got)) or got.value != 4:
        raise RuntimeError("ReadProcessMemory failed at 0x%08X err=%d" % (address, ctypes.GetLastError()))
    return struct.unpack("<I", data.raw)[0]


def write_u32(handle, address, value):
    data = struct.pack("<I", value & 0xFFFFFFFF)
    got = ctypes.c_size_t()
    if not K.WriteProcessMemory(handle, ctypes.c_void_p(address), data, 4, ctypes.byref(got)) or got.value != 4:
        raise RuntimeError("WriteProcessMemory failed at 0x%08X err=%d" % (address, ctypes.GetLastError()))


def main():
    seed = int(sys.argv[1], 0) if len(sys.argv) > 1 else 62298
    if seed < 0 or seed > 0xFFFFFFFF:
        raise SystemExit("seed must be in [0, 2^32)")
    pid = find_pid()
    if pid is None:
        raise SystemExit("gamemd.exe not found")
    access = PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION
    handle = K.OpenProcess(access, False, pid)
    if not handle:
        raise SystemExit("OpenProcess failed pid=%d err=%d" % (pid, ctypes.GetLastError()))
    try:
        values = dict(PARAMETERS)
        values[0x74] = seed
        before = {off: read_u32(handle, MAPSEED + off) for off in values}
        for off, value in values.items():
            write_u32(handle, MAPSEED + off, value)
        after = {off: read_u32(handle, MAPSEED + off) for off in values}
        print("pid=%d MapSeed=0x%08X" % (pid, MAPSEED))
        for off in sorted(values):
            print("  +0x%02X: %d -> %d" % (off, before[off], after[off]))
        if any(after[off] != values[off] for off in values):
            raise SystemExit("parameter readback mismatch")
    finally:
        K.CloseHandle(handle)


if __name__ == "__main__":
    main()
