# -*- coding: utf-8 -*-
"""根据 export_full_atlas.py 的 TSV 生成完整依赖统计与施工顺序。

这是排序工具，不是筛选工具：所有函数均保留在 functions.tsv 中。
"""
import codecs
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_DEFAULT = os.path.join(os.path.dirname(_HERE), "memory", "data", "callgraph")


def rows(path):
    f = codecs.open(path, "r", "utf-8")
    header = f.readline().rstrip("\r\n").split("\t")
    out = [dict(zip(header, line.rstrip("\r\n").split("\t"))) for line in f if line.strip()]
    f.close()
    return out


def write(path, header, data):
    f = codecs.open(path, "w", "utf-8")
    f.write("\t".join(header) + "\n")
    for row in data:
        f.write("\t".join(str(row.get(k, "")) for k in header) + "\n")
    f.close()


def number(row, key):
    try:
        return int(row.get(key, "0"))
    except ValueError:
        return 0


def main(directory):
    funcs = rows(os.path.join(directory, "functions.tsv"))
    edges = rows(os.path.join(directory, "direct_edges.tsv"))
    status_path = os.path.join(directory, "frontier_status.tsv")
    statuses = {}
    if os.path.exists(status_path):
        statuses = dict((row["entry"], row) for row in rows(status_path))
    callers = {}
    callees = {}
    for edge in edges:
        a = edge["caller_entry"]
        b = edge["callee_entry"]
        callers.setdefault(b, set()).add(a)
        callees.setdefault(a, set()).add(b)

    reverse = {}
    for callee, direct_callers in callers.items():
        for caller in direct_callers:
            reverse.setdefault(caller, set()).add(callee)

    transitive = {}
    for row in funcs:
        root = row["entry"]
        seen = set()
        stack = list(callers.get(root, set()))
        while stack:
            current = stack.pop()
            if current in seen or current == root:
                continue
            seen.add(current)
            stack.extend(reverse.get(current, set()))
        transitive[root] = len(seen)

    ranked = []
    for row in funcs:
        entry = row["entry"]
        direct = callers.get(entry, set())
        caller_names = []
        for other in funcs:
            if other["entry"] in direct:
                caller_names.append(other["name"])
        status = statuses.get(entry, {})
        ranked.append({
            "entry": entry,
            "name": row["name"],
            "body_bytes": number(row, "body_bytes"),
            "callsite_count": number(row, "callsite_count"),
            "unique_callers": len(direct),
            "unique_callees": len(callees.get(entry, set())),
            "transitive_dependents": transitive[entry],
            "caller_examples": ",".join(sorted(caller_names)[:8]),
            "status": status.get("reconstruction_status", "unclassified"),
            "confirmed_identity": status.get("confirmed_identity", ""),
            "verification_status": status.get("verification_status", ""),
            "evidence": status.get("evidence", ""),
        })
    ranked.sort(key=lambda x: (-x["transitive_dependents"], -x["unique_callers"], x["entry"]))
    write(os.path.join(directory, "ranking.tsv"),
          ("entry", "name", "body_bytes", "callsite_count", "unique_callers",
           "unique_callees", "transitive_dependents", "caller_examples", "status",
           "confirmed_identity", "verification_status", "evidence"), ranked)

    summary = codecs.open(os.path.join(directory, "ranking_summary.txt"), "w", "utf-8")
    summary.write("全量函数依赖图统计\n")
    summary.write("函数总数\t%d\n" % len(funcs))
    summary.write("直接边总数\t%d\n" % len(edges))
    summary.write("已登记重建状态\t%d\n" % sum(1 for row in ranked if row["status"] != "unclassified"))
    summary.write("未登记重建状态\t%d\n" % sum(1 for row in ranked if row["status"] == "unclassified"))
    summary.write("直接调用者最多（前20）\n")
    for row in sorted(ranked, key=lambda x: (-x["unique_callers"], -x["callsite_count"], x["entry"]))[:20]:
        summary.write("%s\t%s\tcallers=%d\tcallsites=%d\tdependents=%d\n" % (
            row["entry"], row["name"], row["unique_callers"], row["callsite_count"],
            row["transitive_dependents"]))
    summary.write("传递依赖者最多（前20）\n")
    for row in ranked[:20]:
        summary.write("%s\t%s\tcallers=%d\tdependents=%d\n" % (
            row["entry"], row["name"], row["unique_callers"], row["transitive_dependents"]))
    summary.write("\n说明：status=unclassified 表示尚未人工登记，不表示该函数可跳过。\n")
    summary.close()
    print("written: %s (%d functions)" % (os.path.join(directory, "ranking.tsv"), len(ranked)))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else _DEFAULT)
