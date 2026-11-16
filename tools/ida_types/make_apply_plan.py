"""Turn replay_layouts.json into an ordered IDA apply plan (children first) for ida_fill_pad.apply_plan().

  fill      name game-test fields that sit wholly inside _pad members (size and existing members unchanged)
  relayout  rebuild a struct whose Replay accesses prove moved members and/or a new size
  restore   after a struct changes size, every parent that holds it by value and is NOT rebuilt keeps its member
            offsets and size (the freed / needed bytes stay as padding), so no unproven ripple reaches it
A relayout is demoted to fills when any Replay access through a pointer to the struct reaches past the new size.

  python tools/ida_types/make_apply_plan.py out/ida_types/scan/apply_plan
"""
import collections, json, os, re, sys

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
    ignore_mx = set()
    pdir = "out/ida_types/proposals"
    for fn in os.listdir(pdir) if os.path.isdir(pdir) else []:
        if fn.endswith(".json"):
            for x in json.load(open(os.path.join(pdir, fn))).get("structs", []):
                if isinstance(x, dict) and x.get("ignore_max_access"):
                    ignore_mx.add(x["name"])
    relay = set()
    demoted = []
    for n in lay["order"]:
        v = st[n]
        if v["action"] == "relayout":
            if v["size"] != v["ida_size"] and mx.get(v["gt"], 0) > v["size"] and n not in ignore_mx:
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
    # verdicts from the second report pass: struct comments for absent / opaque types, and the rename list
    renames = []
    for fn in sorted(os.listdir(pdir)) if os.path.isdir(pdir) else []:
        if not fn.endswith(".json"):
            continue
        d = json.load(open(os.path.join(pdir, fn), encoding="utf-8"))
        for x in d.get("structs", []):
            if isinstance(x, dict) and x.get("verdict") in ("absent", "opaque") and x.get("name") in ida:
                why = " ".join((x.get("notes") or "").split())[:300]
                text = ("Replay: %s. %s (%s)" % ("not present in this build" if x["verdict"] == "absent"
                                                  else "opaque external interface, only the vtable pointer is real",
                                                  why, d.get("agent", fn[:-5])))
                plan.append({"name": x["name"], "mode": "comment", "text": text})
                cnt["comment"] += 1
        for r in d.get("renames", []) or []:
            if isinstance(r, list) and len(r) >= 3:
                new = re.sub(r"::`scalar deleting destructor'$", "::scalar_deleting_dtor", str(r[2]))
                new = re.sub(r"::`vector deleting destructor'$", "::vector_deleting_dtor", new)
                new = re.sub(r"::scalar deleting destructor$", "::scalar_deleting_dtor", new)
                new = re.sub(r"::vector deleting destructor$", "::vector_deleting_dtor", new)
                renames.append([r[0], r[1], new, r[3] if len(r) > 3 else "", d.get("agent", fn[:-5])])
    json.dump([[a, b, c, d] for a, b, c, d, e in renames], open(out + "_renames.json", "w"), indent=0)
    cnt["renames"] = len(renames)
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
