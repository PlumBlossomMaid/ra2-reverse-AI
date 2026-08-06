# -*- coding: utf-8 -*-
"""随机数消耗的传递闭包——用 Ghidra 的真实函数边界，而不是线性反汇编猜。

用途：判断某个阶段/函数究竟会不会动全局 Randomizer。这件事必须精确：
少算一次取数，整条随机数流就错位，之后每一格地形都对不上。
输出「从根函数到取数点」的完整调用链，便于人工核对而不是只给一个结论。
"""
from ghidra.program.model.symbol import RefType
import codecs, os

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(_REPO, "memory", "data", "decomp", "rng_closure.txt")

ROOTS = [("0x57a0c0", "岸线校验/平滑总入口"),
         ("0x59bbc0", "涂块核心"),
         ("0x59a6c0", "Seeding water 阶段调度")]

SINKS = {0x65C780: "Randomizer::Random", 0x65C7E0: "Randomizer::RandomRanged",
         0x65C6D0: "Randomizer::Randomizer(seed)"}

fm = currentProgram.getFunctionManager()
af = currentProgram.getAddressFactory()
mon = None

def callees(fn):
    """函数体内所有 call 目标（用 Ghidra 的 body，边界可靠）"""
    out = set()
    body = fn.getBody()
    listing = currentProgram.getListing()
    for ins in listing.getInstructions(body, True):
        for r in ins.getReferencesFrom():
            if r.getReferenceType().isCall():
                out.add(r.getToAddress().getOffset())
    return out

f = codecs.open(OUT, "w", "utf-8")
f.write("## 随机数消耗的传递闭包（Ghidra 真实函数边界）\n")
f.write("## sink = Randomizer::Random / RandomRanged / ctor\n")

for sa, label in ROOTS:
    root = af.getAddress(sa).getOffset()
    f.write("\n" + "=" * 70 + "\n## 根：%s @ %s\n\n" % (label, sa))
    seen, parent, stack, chains = set(), {}, [root], []
    while stack:
        cur = stack.pop()
        if cur in seen:
            continue
        seen.add(cur)
        fn = fm.getFunctionAt(af.getAddress("0x%x" % cur))
        if fn is None:
            continue
        for c in callees(fn):
            if c in SINKS:
                chains.append((cur, c))
            if c not in seen:
                parent[c] = cur
                stack.append(c)
    f.write("可达函数数：%d\n" % len(seen))
    if not chains:
        f.write("**调用树内没有任何取数点**\n")
    else:
        f.write("取数点 %d 处：\n" % len(chains))
        for site, sink in sorted(set(chains)):
            path, p = ["%08X" % site], site
            while p in parent:
                p = parent[p]; path.append("%08X" % p)
            f.write("  %-24s  <-  %s\n" % (SINKS[sink], " <- ".join(path)))
f.close()
print("written: " + OUT)
