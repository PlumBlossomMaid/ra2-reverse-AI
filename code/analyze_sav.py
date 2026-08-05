# -*- coding: utf-8 -*-
"""分析 YR .sav 存档的 OLE CFB 内部结构"""
import olefile
import sys

files = [
    r"E:\YRLauncher\SAVE79C7.SAV",
    r"E:\YRLauncher\SOV05UMD.sav",
    r"E:\YRLauncher\SAVE4AFC.SAV",
]

def dump_streams(path):
    print("=" * 60)
    print("FILE:", path)
    ole = olefile.OleFileIO(path)
    entries = ole.listdir(streams=True, storages=True)
    for entry in entries:
        name = "/".join(entry)
        if ole.exists(entry) and not ole.get_type(entry) == olefile.STGTY_STORAGE:
            size = ole.get_size(entry)
            print("  STREAM  %-60s %10d" % (name, size))
        else:
            print("  STORAGE %s" % name)
    # 文件头元数据
    header = ole.header
    print("  sector_shift=%d  mini_sector_shift=%d  num_dirsectors=%d"
          % (header.sector_shift, header.mini_sector_shift, header.num_dirsectors))
    ole.close()

for f in files:
    try:
        dump_streams(f)
    except Exception as e:
        print(f, "ERROR:", e)
