"""Compare two Local Types dumps (dump_udts) after an apply: sizes that changed, members that moved, and how many
Replay access pairs land on the right / a wrong member before and after (replay_layouts scoring).

  python tools/ida_types/check_apply.py out/ida_types/scan/ida_udts.json out/ida_types/scan/ida_udts_after.json
"""
import collections, json, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import replay_layouts as RL

PAD = re.compile(r"^(_pad|pad|gap)[0-9A-Fa-f_]*$", re.I)


def main():
    before = json.load(open(sys.argv[1]))
    after = json.load(open(sys.argv[2]))
    plan = json.load(open("out/ida_types/scan/apply_plan.json"))
    planned = {e["name"] for e in plan if e["mode"] == "relayout"}
    size_changed, unplanned, moved = [], [], collections.Counter()
    for n, t in before.items():
        a = after.get(n)
        if not a:
            unplanned.append((n, "missing"))
            continue
        if a["size"] != t["size"]:
            size_changed.append((n, t["size"], a["size"]))
            if n not in planned:
                unplanned.append((n, "size %d -> %d" % (t["size"], a["size"])))
        bm = {m[1]: m[0] for m in t["members"] if not PAD.match(m[1])}
        for m in a["members"]:
            if m[1] in bm and bm[m[1]] != m[0]:
                moved[n] += 1
                if n not in planned:
                    unplanned.append((n, "moved %s %d -> %d" % (m[1], bm[m[1]], m[0])))
    named_b = sum(sum(m[2] for m in t["members"] if not PAD.match(m[1])) for t in before.values())
    named_a = sum(sum(m[2] for m in t["members"] if not PAD.match(m[1])) for t in after.values())
    print("size changes:", len(size_changed), "(planned rebuilds: %d)" % len(planned))
    print("structs with moved members:", len(moved))
    print("UNPLANNED changes:", len(unplanned))
    for u in unplanned[:40]:
        print("  ", u)
    print("named bytes: %d -> %d (%+d)" % (named_b, named_a, named_a - named_b))
    # Replay evidence score over every struct with access pairs
    gt = json.load(open(RL.S + "gt_types_all.json"))
    al = json.load(open(RL.S + "all_align.json"))
    consts = collections.defaultdict(collections.Counter)
    agent = {f.split(".")[0] for f in os.listdir("out/ida_types/before")}
    tot = {}
    for label, ida in (("before", before), ("after", after)):
        w = RL.World(gt, ida, {k: dict(v) for k, v in al["pairs"].items()}, consts, agent)
        order = w.order()
        for g in order:
            w.build(g)
        ok = bad = 0
        for g in order:
            o, b = RL.score(w, g, ida[w.g2i[g]]["members"])
            ok += o
            bad += b
        tot[label] = (ok, bad)
    print("Replay access pairs on the right member / on a wrong member: before %s, after %s" % (tot["before"],
                                                                                              tot["after"]))


if __name__ == "__main__":
    main()
