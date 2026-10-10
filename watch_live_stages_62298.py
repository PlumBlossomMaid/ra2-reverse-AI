# -*- coding: utf-8 -*-
"""轮询 x32dbg 暂停位置，按「生成轮次」保存随机地图阶段快照。

每次命中入口 0x598960 视为新一轮生成（随机地图对话框的预览生成与遭遇战的
实际重建各算一轮），该轮快照写入 <root>/runNN/<序号>_<阶段>_<样本>/，同时把
每次抓取追加一行到 <root>/capture_log.txt，并把轮次摘要写进 <root>/runs.json。

默认无限运行；给出秒数参数则按秒数设限。
"""
from __future__ import print_function

import ctypes
import json
import os
import re
import sys
import time

import _capture_live_stages_62298 as capture

K = ctypes.windll.kernel32
TARGETS = dict(capture.STAGES)
ENTRY_TAG = "entry_generate"
MAX_SAMPLES_PER_STAGE = 2
# 内部探针（如 SplitRegion 的 take 抽取）会反复命中，放宽上限以便计数。
MAX_SAMPLES_OVERRIDE = {"split_take_draw": 8}
# 工作数组（side²×0x50，70×70 图约 1.97MB/次）只在关键阶段导出，其余阶段跳过以缩短暂停时间
WORK_TAGS = {
    "before_making_regions_log", "after_rebuild_and_split_regions",
    "after_reroll_multitile_cliffs", "before_lats", "rmg_done",
}


def process_handle(pid):
    access = capture.PROCESS_QUERY_INFORMATION | capture.PROCESS_VM_READ
    return K.OpenProcess(access, False, pid)


def thread_context(tid):
    handle = K.OpenThread(capture.THREAD_QUERY_INFORMATION | capture.THREAD_GET_CONTEXT, False, tid)
    if not handle:
        return None
    try:
        context = capture.CONTEXT32()
        context.ContextFlags = capture.CONTEXT_CONTROL
        getter = getattr(K, "Wow64GetThreadContext", None)
        if getter is not None:
            ok = getter(handle, ctypes.byref(context))
        else:
            ok = K.GetThreadContext(handle, ctypes.byref(context))
        if not ok:
            return None
        return context
    finally:
        K.CloseHandle(handle)


def paused_target(pid):
    for tid in capture.thread_ids(pid):
        context = thread_context(tid)
        if context is None:
            continue
        address = context.Eip
        tag = TARGETS.get(address)
        if tag is None:
            address -= 1
            tag = TARGETS.get(address)
        if tag is not None:
            return tid, address, tag, context
    return None


def parse_args(argv):
    pid = None
    root = capture.OUT_ROOT
    seconds = 0.0
    beep = True
    poll_seconds = 0.001
    event_log = None
    for arg in argv[1:]:
        if arg.startswith("--pid="):
            pid = int(arg.split("=", 1)[1], 0)
        elif arg.startswith("--out=") or arg.startswith("--root="):
            root = arg.split("=", 1)[1]
        elif arg.startswith("--poll-ms="):
            poll_seconds = max(0.001, float(arg.split("=", 1)[1]) / 1000.0)
        elif arg.startswith("--event-log="):
            event_log = arg.split("=", 1)[1]
        elif arg == "--no-beep":

            beep = False
        else:
            seconds = float(arg)
    return pid, root, seconds, beep, poll_seconds, event_log


class EventLogReader(object):
    def __init__(self, path):
        self.path = path
        self.offset = 0
        self.pending = []

    def read_events(self):
        if not self.path or not os.path.isfile(self.path):
            return []
        with open(self.path, "r", encoding="utf-8", errors="replace") as f:
            f.seek(self.offset)
            text = f.read()
            self.offset = f.tell()
        events = []
        for line in text.splitlines():
            match = re.search(r"RMG_BP\s+0x([0-9A-Fa-f]+)", line)
            if match:
                events.append(int(match.group(1), 16))
        return events


class CaptureLog(object):
    def __init__(self, root):
        self.root = root
        self.path = os.path.join(root, "capture_log.txt")
        self.json_path = os.path.join(root, "runs.json")
        self.runs = []

    def line(self, text):
        stamp = time.strftime("%Y-%m-%d %H:%M:%S")
        print("%s %s" % (stamp, text))
        sys.stdout.flush()
        with open(self.path, "a", encoding="utf-8") as f:
            f.write("%s %s\n" % (stamp, text))

    def flush_runs(self):
        with open(self.json_path, "w", encoding="utf-8") as f:
            json.dump(self.runs, f, ensure_ascii=False, indent=2)


def wait_until_leaves(pid, address, poll_seconds):
    """等待用户继续运行，避免同一暂停点被轮询反复记成多次命中。"""
    while True:
        current = paused_target(pid)
        if current is None or current[1] != address:
            return
        time.sleep(max(poll_seconds, 0.005))


def main():
    requested_pid, root, seconds, beep, poll_seconds, event_log_path = parse_args(sys.argv)
    pids = [requested_pid] if requested_pid is not None else capture.find_pids()
    if not pids:
        print("gamemd.exe not found")
        return 1
    pid = pids[0]
    handle = process_handle(pid)
    if not handle:
        print("OpenProcess failed pid=%d err=%d" % (pid, ctypes.GetLastError()))
        return 1
    if not os.path.isdir(root):
        os.makedirs(root)
    log = CaptureLog(root)
    event_reader = EventLogReader(event_log_path)
    deadline = time.time() + seconds if seconds > 0 else float("inf")
    log.line("watching pid=%d entry=%s stages=%d out=%s timeout=%s" % (
        pid, hex(capture.STAGES[0][0]), len(TARGETS) - 1, root,
        "none" if seconds <= 0 else "%gs" % seconds))
    run_index = 0
    run_dir = os.path.join(root, "run00")
    record = None
    counts = {}
    order = []
    last_hit = None
    # 入口只置 pending 标记，等第一个非入口阶段才开新轮：
    # 调试器停在入口时线程上下文读取会抖动，直接按"命中次数"计轮会刷出大量空轮。
    pending_entry = None
    try:
        while time.time() < deadline:
            event_reader.pending.extend(event_reader.read_events())
            if event_reader.pending:
                event_address = event_reader.pending.pop(0)
            else:
                # 事件日志不是硬依赖：旧 x32dbg 会话、日志命令未生效或
                # 启动器未传 --event-log 时，直接回退到已验证的暂停 EIP 读取。
                fallback = paused_target(pid)
                if fallback is None:
                    time.sleep(poll_seconds)
                    continue
                event_address = fallback[1]
            tag = TARGETS.get(event_address)
            if tag is None:
                time.sleep(poll_seconds)
                continue
            hit = None
            for _ in range(50):
                hit = paused_target(pid)
                if hit is not None and hit[1] == event_address:
                    break
                time.sleep(poll_seconds)
            if hit is None or hit[1] != event_address:
                log.line("event_without_paused_thread eip=0x%08X" % event_address)
                continue
            tid, address, tag, context = hit
            if tag == ENTRY_TAG:
                if pending_entry is None:
                    pending_entry = (tid, address)
                    log.line("--- entry hit pid=%d tid=%d eip=0x%08X (等待下一阶段开轮)" % (
                        pid, tid, address))
                wait_until_leaves(pid, address, poll_seconds)
                continue
            if pending_entry is not None or record is None:
                run_index += 1
                run_dir = os.path.join(root, "run%02d" % run_index)
                counts = {}
                order = []
                record = {"run": run_index, "dir": run_dir, "pid": pid, "tid": tid,
                          "entry_eip": ("0x%08X" % pending_entry[1]) if pending_entry else None,
                          "stages": []}
                log.runs.append(record)
                log.line("=== run%02d start pid=%d first_stage=%s" % (
                    run_index, pid, tag))
                pending_entry = None
            sample = counts.get(tag, 0) + 1
            cap = MAX_SAMPLES_OVERRIDE.get(tag, MAX_SAMPLES_PER_STAGE)
            if sample > cap:
                continue
            counts[tag] = sample
            order.append(tag)
            name = "%02d_%s_%d" % (len(order), tag, sample)
            meta = capture.dump_state(handle, run_dir, name, {
                "pid": pid, "tid": tid, "eip": "0x%08X" % address,
                "run": run_index, "exception": "x32dbg_breakpoint_poll",
                "eax": "0x%08X" % context.Eax, "ebx": "0x%08X" % context.Ebx,
                "ecx": "0x%08X" % context.Ecx, "edx": "0x%08X" % context.Edx,
                "esi": "0x%08X" % context.Esi, "edi": "0x%08X" % context.Edi,
                "ebp": "0x%08X" % context.Ebp, "esp": "0x%08X" % context.Esp,
            }, include_work=(tag in WORK_TAGS))
            log.line("run%02d %-34s rng=%s cells=%s" % (
                run_index, name, meta.get("rng_head"), meta.get("nonnull")))
            if beep:
                try:
                    import winsound
                    winsound.Beep(1000, 60)
                except Exception:
                    beep = False
            if record is not None:
                record["stages"].append({
                    "name": name, "tag": tag, "eip": "0x%08X" % address,
                    "rng_head": meta.get("rng_head"), "cells": meta.get("nonnull"),
                })
            wait_until_leaves(pid, address, poll_seconds)
            if tag == "rmg_done":
                log.line("=== run%02d done stages=%d" % (run_index, len(order)))
            log.flush_runs()
        log.line("stopped runs=%d" % run_index)
        return 0
    finally:
        log.flush_runs()
        K.CloseHandle(handle)


if __name__ == "__main__":
    sys.exit(main())
