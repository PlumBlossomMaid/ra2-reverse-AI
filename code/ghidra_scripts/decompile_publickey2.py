# -*- coding: utf-8 -*-
"""反编译 PublicKey 使用者：0x6BD735 所在函数 + 构造函数 0x40D808 + vtable 0x7E1A64 首个函数 0x40D800
回答：[PublicKey] 内嵌 RSA 公钥的意义（谁在用、验签什么）
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_publickey2.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

OUT = os.path.join(_REPO, "memory", "data", "decomp", "publickey_use.txt")
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

for va, label in [
    ("0x6bd735", "PublicKey INI 解析点所在函数"),
    ("0x40d808", "vtable 0x7E1A64 对象构造函数"),
    ("0x40d800", "vtable[0] 函数"),
    ("0x40d5a0", "vtable[1] 函数"),
]:
    addr = af.getAddress(va)
    fn = fm.getFunctionContaining(addr)
    f.write("=" * 70 + "\n## %s @ %s  fn=%s\n" % (
        label, va, fn.getName() if fn else "NONE"))
    if fn is not None:
        res = decomp.decompileFunction(fn, 180, monitor)
        if res.decompileCompleted():
            f.write(res.getDecompiledFunction().getC())
        else:
            f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
    else:
        f.write("no function\n")
    f.write("\n")

f.close()
decomp.dispose()
print("PUBLICKEY2_DONE")
