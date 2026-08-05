# -*- coding: utf-8 -*-
"""查 [PublicKey] 字符串 (0x7E1A81) 的引用点 + 反编译使用者
回答：内嵌 RSA 公钥的意义（谁在验签什么）
运行方式: analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
          -postScript decompile_publickey_xref.py -scriptPath <repo>/code/ghidra_scripts
"""
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import codecs

OUT = os.path.join(_REPO, "memory", "data", "decomp", "publickey_xref.txt")
f = codecs.open(OUT, "w", "utf-8")

af = currentProgram.getAddressFactory()
fm = currentProgram.getFunctionManager()
listing = currentProgram.getListing()
decomp = DecompInterface()
decomp.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

TARGETS = ["0x7e1a81", "0x7e1a8b"]  # "[PublicKey]" / "1=AihRvNo..."（公钥体）

for s in TARGETS:
    addr = af.getAddress(s)
    f.write("=" * 70 + "\n## xrefs to %s\n" % s)
    refs = list(currentProgram.getReferenceManager().getReferencesTo(addr))
    for r in refs:
        f.write("  %s %s\n" % (r.getFromAddress(), r.getReferenceType()))
    f.write("\n")
    for r in refs:
        from_addr = r.getFromAddress()
        fn = fm.getFunctionContaining(from_addr)
        f.write("## ref from %s  fn=%s\n" % (
            from_addr, fn.getName() if fn else "NONE"))
        if fn is not None:
            res = decomp.decompileFunction(fn, 180, monitor)
            if res.decompileCompleted():
                f.write(res.getDecompiledFunction().getC())
            else:
                f.write("DECOMPILE FAILED: %s\n" % res.getErrorMessage())
        else:
            # 汇编兜底
            a = from_addr.subtract(0x20)
            end = from_addr.add(0x40)
            while a is not None and a.compareTo(end) < 0:
                inst = listing.getInstructionAt(a)
                if inst is None:
                    inst = listing.getDataAt(a)
                if inst is not None:
                    f.write("%s\t%s\n" % (a, inst))
                    a = a.add(inst.getLength())
                else:
                    f.write("%s\t???\n" % a)
                    a = a.add(1)
        f.write("\n")

f.close()
decomp.dispose()
print("PUBLICKEY_XREF_DONE")
