# -*- coding: utf-8 -*-
"""FUN_0065AC40 深层序列化链 —— Type 指针/swizzle ID 写入位置
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_serialize_chain.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

ADDRS = [
    ("0x65ac40", "FUN_0065AC40 (TechnoClass::Save 下一层)"),
    ("0x4103e0", "AbstractClass::GetSizeMax? (vtable+0x1C)"),
    ("0x410300", "AbstractClass 序列化 (vtable+0x4 附近)"),
]

OUT = r"E:\code\ra2-reverse\serialize_chain_decomp.txt"
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

for s, label in ADDRS:
    fn = fm.getFunctionAt(af.getAddress(s))
    f.write("=" * 70 + "\n## %s  %s\n" % (label, s))
    if fn is None:
        f.write("NO FUNCTION\n")
        continue
    res = decomp.decompileFunction(fn, 90, monitor)
    if res.decompileCompleted():
        f.write(res.getDecompiledFunction().getC())
    else:
        f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    f.write("\n")

f.close()
decomp.dispose()
print("SERIALIZE_CHAIN_DONE")
