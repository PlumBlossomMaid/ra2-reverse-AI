# -*- coding: utf-8 -*-
"""导出 gamemd.exe 全量函数、调用边、未解析调用点与可执行区覆盖。

运行：analyzeHeadless.bat <proj> <name> -process gamemd.exe -noanalysis
      -postScript export_full_atlas.py -scriptPath <repo>/code/ghidra_scripts

输出位于 memory/data/callgraph/。调用频度只描述依赖，不筛除任何函数。
"""
import codecs
import os

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT_DIR = os.path.join(_REPO, "memory", "data", "callgraph")
if not os.path.isdir(OUT_DIR):
    os.makedirs(OUT_DIR)

fm = currentProgram.getFunctionManager()
listing = currentProgram.getListing()
refmgr = currentProgram.getReferenceManager()
mem = currentProgram.getMemory()


def text(value):
    if value is None:
        return u""
    return unicode(value).replace(u"\t", u" ").replace(u"\r", u" ").replace(u"\n", u" ")


def hexaddr(addr):
    return "0x%08X" % addr.getOffset()


def write_tsv(name, header, rows):
    path = os.path.join(OUT_DIR, name)
    f = codecs.open(path, "w", "utf-8")
    f.write(u"\t".join(header) + u"\n")
    for row in rows:
        f.write(u"\t".join(text(v) for v in row) + u"\n")
    f.close()
    print("written: %s (%d rows)" % (path, len(rows)))


functions = list(fm.getFunctions(True))
functions_by_entry = dict((fn.getEntryPoint().getOffset(), fn) for fn in functions)
callsite_counts = dict((fn.getEntryPoint().getOffset(), 0) for fn in functions)
caller_sets = dict((fn.getEntryPoint().getOffset(), set()) for fn in functions)
callee_sets = dict((fn.getEntryPoint().getOffset(), set()) for fn in functions)

edges = []
unresolved = []
for caller in functions:
    caller_entry = caller.getEntryPoint().getOffset()
    instructions = listing.getInstructions(caller.getBody(), True)
    while instructions.hasNext():
        ins = instructions.next()
        flow = ins.getFlowType()
        is_call = flow.isCall()
        is_tail_jump = ins.getMnemonicString() == "JMP"
        if not is_call and not is_tail_jump:
            continue

        refs = []
        for ref in ins.getReferencesFrom():
            ref_type = ref.getReferenceType()
            if ref_type.isCall() or (is_tail_jump and ref_type.isJump()):
                refs.append(ref)

        targets = []
        for ref in refs:
            target = ref.getToAddress()
            target_fn = fm.getFunctionAt(target)
            if target_fn is not None:
                targets.append((ref, target_fn))

        edge_kind = "call" if is_call else "tail_jump"
        if targets:
            seen_targets = set()
            for ref, target_fn in targets:
                callee_entry = target_fn.getEntryPoint().getOffset()
                if not is_call and callee_entry == caller_entry:
                    continue
                key = (callee_entry, ref.getReferenceType().toString())
                if key in seen_targets:
                    continue
                seen_targets.add(key)
                edges.append((
                    hexaddr(caller.getEntryPoint()), caller.getName(),
                    hexaddr(ins.getAddress()), edge_kind,
                    hexaddr(target_fn.getEntryPoint()), target_fn.getName(),
                    ref.getReferenceType().toString(), "ghidra_reference"))
                callsite_counts[callee_entry] = callsite_counts.get(callee_entry, 0) + 1
                caller_sets.setdefault(callee_entry, set()).add(caller_entry)
                callee_sets.setdefault(caller_entry, set()).add(callee_entry)
        elif is_call:
            unresolved.append((
                hexaddr(caller.getEntryPoint()), caller.getName(),
                hexaddr(ins.getAddress()), ins.toString(),
                text(ins.getDefaultOperandRepresentation(0))))

function_rows = []
for fn in functions:
    entry = fn.getEntryPoint().getOffset()
    body = fn.getBody()
    namespace = fn.getParentNamespace()
    symbol = fn.getSymbol()
    source = symbol.getSource().toString() if symbol is not None else ""
    function_rows.append((
        hexaddr(fn.getEntryPoint()),
        hexaddr(body.getMinAddress()),
        hexaddr(body.getMaxAddress()),
        body.getNumAddresses(),
        fn.getName(),
        namespace.getName(True) if namespace is not None else "",
        source,
        fn.getCallingConventionName(),
        int(fn.isThunk()),
        int(fn.isExternal()),
        callsite_counts.get(entry, 0),
        len(caller_sets.get(entry, set())),
        len(callee_sets.get(entry, set()))))

coverage_rows = []
total_exec = 0
function_bytes = 0
unassigned_instruction_bytes = 0
defined_data_bytes = 0
undefined_bytes = 0
for block in mem.getBlocks():
    if not block.isExecute():
        continue
    block_size = block.getSize()
    total_exec += block_size
    assigned = 0
    unassigned = 0
    data_bytes = 0
    undefined = 0
    addr = block.getStart()
    end = block.getEnd()
    while addr.compareTo(end) <= 0:
        cu = listing.getCodeUnitAt(addr)
        if cu is None:
            undefined += 1
            addr = addr.add(1)
            continue
        length = cu.getLength()
        if fm.getFunctionContaining(addr) is not None:
            assigned += length
        elif listing.getInstructionAt(addr) is not None:
            unassigned += length
        else:
            data_bytes += length
        addr = addr.add(length)
    function_bytes += assigned
    unassigned_instruction_bytes += unassigned
    defined_data_bytes += data_bytes
    undefined_bytes += undefined
    coverage_rows.append((
        block.getName(), hexaddr(block.getStart()), hexaddr(block.getEnd()), block_size,
        assigned, unassigned, data_bytes, undefined))

write_tsv("functions.tsv",
          (u"entry", u"body_min", u"body_max", u"body_bytes", u"name", u"namespace",
           u"symbol_source", u"calling_convention", u"is_thunk", u"is_external",
           u"callsite_count", u"unique_callers", u"unique_callees"),
          function_rows)
write_tsv("direct_edges.tsv",
          (u"caller_entry", u"caller_name", u"callsite", u"edge_kind", u"callee_entry",
           u"callee_name", u"reference_type", u"evidence"),
          edges)
write_tsv("unresolved_calls.tsv",
          (u"caller_entry", u"caller_name", u"callsite", u"instruction", u"operand"),
          unresolved)
write_tsv("coverage.tsv",
          (u"block", u"start", u"end", u"bytes", u"function_bytes",
           u"unassigned_instruction_bytes", u"defined_data_bytes", u"undefined_bytes"),
          coverage_rows)

summary = codecs.open(os.path.join(OUT_DIR, "summary.txt"), "w", "utf-8")
summary.write(u"functions\t%d\n" % len(functions))
summary.write(u"direct_edges\t%d\n" % len(edges))
summary.write(u"unresolved_calls\t%d\n" % len(unresolved))
summary.write(u"executable_bytes\t%d\n" % total_exec)
summary.write(u"function_bytes\t%d\n" % function_bytes)
summary.write(u"unassigned_instruction_bytes\t%d\n" % unassigned_instruction_bytes)
summary.write(u"defined_data_bytes\t%d\n" % defined_data_bytes)
summary.write(u"undefined_bytes\t%d\n" % undefined_bytes)
summary.close()
print("written: %s" % os.path.join(OUT_DIR, "summary.txt"))
