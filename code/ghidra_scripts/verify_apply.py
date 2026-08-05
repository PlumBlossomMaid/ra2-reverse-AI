# -*- coding: utf-8 -*-
"""验证：导出标注后的命名函数清单"""
import codecs

fm = currentProgram.getFunctionManager()
total = 0
named = []
for fn in fm.getFunctions(True):
    total += 1
    n = fn.getName()
    if n.startswith("FUN_") or n.startswith("thunk"):
        continue
    named.append((n, str(fn.getEntryPoint())))

f = codecs.open(r"E:\code\ra2-reverse\named_functions.txt", "w", "utf-8")
f.write("total=%d named=%d\n" % (total, len(named)))
for n, a in sorted(named):
    f.write("%s @ %s\n" % (n, a))
f.close()
print("VERIFY_DONE total=%d named=%d" % (total, len(named)))
