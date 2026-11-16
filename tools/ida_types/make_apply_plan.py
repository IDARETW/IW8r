"""Turn replay_layouts.json into an ordered IDA apply plan (children first) for ida_fill_pad.apply_plan().

  fill      name game-test fields that sit wholly inside _pad members (size and existing members unchanged)
  relayout  rebuild a struct whose Replay accesses prove moved members and/or a new size
  restore   after a struct changes size, every parent that holds it by value and is NOT rebuilt keeps its member
            offsets and size (the freed / needed bytes stay as padding), so no unproven ripple reaches it
A relayout is demoted to fills when any Replay access through a pointer to the struct reaches past the new size.

  python tools/ida_types/make_apply_plan.py out/ida_types/scan/apply_plan
"""
import collections, json, re, sys

S = "out/ida_types/scan/"
PAD = re.compile(r"^(_pad|pad|gap)[0-9A-Fa-f_]*$", re.I)


def main():
    out = sys.argv[1]
    lay = json.load(open(S + "replay_layouts.json"))
    ida = json.load(open(S + "ida_udts.json"))
    mx = json.load(open(S + "replay_maxacc.json"))
    st = lay["structs"]
    emb = collections.defaultdict(list)
    for pn, t in ida.items():
        for m in t["members"]:
            ty = m[3].strip()
            base = re.sub(r"\[\d+\]", "", ty).strip()
            if base in ida and "*" not in ty:
                emb[base].append(pn)
    relay = set()
    demoted = []
    for n in lay["order"]:
        v = st[n]
        if v["action"] == "relayout":
            if v["size"] != v["ida_size"] and mx.get(v["gt"], 0) > v["size"]:
                demoted.append((n, mx[v["gt"]], v["size"]))
                v["action"] = "report"
                v["items"] = []
                v["extra"].setdefault("why", []).append("Replay accesses reach +%d, past the new size %d"
                                                        % (mx[v["gt"]], v["size"]))
            else:
                relay.add(n)
    protect = {}
    for n in relay:
        v = st[n]
        if v["size"] != v["ida_size"]:
            for p in sorted(set(emb.get(n, []))):
                if p not in relay and not ida[p]["union"]:
                    protect.setdefault(n, []).append(p)
    protected = {p for ps in protect.values() for p in ps}
    plan = []
    cnt = collections.Counter()
    for n in lay["order"]:
        v = st[n]
        if ida.get(n, {}).get("union"):
            continue
        if v["action"] == "relayout":
            plan.append({"name": n, "mode": "relayout", "size": v["size"], "items": v["items"]})
            cnt["relayout"] += 1
            for p in protect.get(n, []):
                t = ida[p]
                plan.append({"name": p, "mode": "restore", "size": t["size"],
                             "members": [[m[0], m[1]] for m in t["members"] if not PAD.match(m[1])]})
                cnt["restore"] += 1
        elif v["action"] in ("fill", "report") and v["items"] and n not in protected:
            plan.append({"name": n, "mode": "fill", "items": v["items"]})
            cnt["fill"] += 1
            cnt["fill_fields"] += len(v["items"])
            cnt["fill_prior"] += sum(1 for x in v["items"] if x[4] == "prior")
    json.dump(plan, open(out + ".json", "w"))
    with open(out + "_report.txt", "w") as f:
        f.write("# structs left for Replay checks (not changed beyond fills)\n")
        for n in lay["order"]:
            v = st[n]
            if v["action"] == "report":
                f.write("%-48s ida %d computed %d gt %d anchors %d: %s\n" % (
                    n, v["ida_size"], v["size"], v["gt_size"], v["anchored"], "; ".join(v["extra"].get("why", []))))
    print(dict(cnt), "demoted:", demoted, "protected parents:", protect)


if __name__ == "__main__":
    main()
