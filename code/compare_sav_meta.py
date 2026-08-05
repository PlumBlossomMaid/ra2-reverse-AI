# -*- coding: utf-8 -*-
"""对比多个 .sav 存档的元数据 stream 值"""
import olefile
import struct

files = [
    r"E:\YRLauncher\SAVE79C7.SAV",
    r"E:\YRLauncher\SOV05UMD.sav",
    r"E:\YRLauncher\SAVE4AFC.SAV",
]

fields = ['Internal Version', 'Version', 'GameType', 'Campaign', 'Scenario Number',
          'Executable Name', 'Player House', 'Scenario Description',
          'Player Name', 'Player Name2']

for f in files:
    o = olefile.OleFileIO(f)
    print("=== %s ===" % f.split("\\")[-1])
    for s in fields:
        d = o.openstream(s).read()
        if len(d) == 4:
            v = struct.unpack('<I', d)[0]
            print("  %-20s int=0x%08X (%d)" % (s, v, v))
        else:
            print("  %-20s str=%r" % (s, d.rstrip(b'\x00')))
    o.close()
    print()
