"""Retype named byte-array members to their game-test struct type when that local type now has exactly the same size.

Earlier passes left members as `unsigned __int8[N]` when their local type had the wrong size for Replay (cg_t's
`shake` was 1008 bytes while `Shake` was 1112). Once the type is fixed, the member can carry it again. Offsets and
sizes never change; structs in the report pass are skipped while agents read them.

  python tools/ida_types/make_retype_plan.py out/ida_types/scan/retype_plan
"""
import collections, json, os, re, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import replay_layouts as RL

S = RL.S
BYTES = re.compile(r"^(unsigned __int8|char|unsigned char|_BYTE)\[(\d+)\]$")


def main():
    out = sys.argv[1]
    gt = json.load(open(S + "gt_types_all.json"))
    ida = json.load(open(S + "ida_udts.json"))
    lay = json.load(open(S + "replay_layouts.json"))["structs"]
    report = {n for n, v in lay.items() if v["action"] == "report"}
    w = RL.World(gt, ida, {}, collections.defaultdict(collections.Counter), set())
    plan, n = [], 0
    for name, t in ida.items():
        g = w.i2g.get(name)
        if not g or t["union"] or name in report:
            continue
        gf = {}
        for off, fname, size, ctype, tag in gt[g]["fields"]:
            gf[fname] = (ctype, tag, size)
        items = []
        for off, mname, size, ty, base in t["members"]:
            m = BYTES.match(ty.strip())
            if not m or mname not in gf or RL.PAD.match(mname):
                continue
            ctype, tag, gsize = gf[mname]
            kind, child, cnt = w.tinfo(ctype, tag, gsize)
            cand = None
            if kind == "struct" and child in w.g2i and ida[w.g2i[child]]["size"] == size:
                cand = w.g2i[child]
            elif kind == "array" and child in w.g2i:
                es = ida[w.g2i[child]]["size"]
                # the element count must stay the game-test count, so a wrong element size cannot hide in it
                if es > 0 and size % es == 0 and size // es == cnt:
                    cand = "%s[%d]" % (w.g2i[child], size // es)
            if cand:
                items.append([off, mname, size, [cand], "retype"])
        if items:
            plan.append({"name": name, "mode": "retype", "items": items})
            n += len(items)
    json.dump(plan, open(out + ".json", "w"))
    print("structs:", len(plan), "members to retype:", n)
    for e in plan[:30]:
        print(" ", e["name"], [(x[1], x[3][0]) for x in e["items"]][:5])


if __name__ == "__main__":
    main()
