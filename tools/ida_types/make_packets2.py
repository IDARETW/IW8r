"""Second-pass evidence packets for the structs the builder still reports (out/ida_types/packets2/<S>.json).

Adds to the first-pass packet: the current IDA layout, deleting-destructor leads (scan_deleting_dtors.py, with the
game-test check), typed globals, IDA structs that hold S by value (and whether they were rebuilt from Replay
evidence), Replay functions named after S, and every earlier proposal entry for S with its agent.

  python tools/ida_types/make_packets2.py [STRUCT ...]      (default: every struct whose verdict is still "report")
"""
import glob, json, os, re, sys, collections

S = "out/ida_types/scan/"


def san(n):
    return re.sub(r"[^A-Za-z0-9_]", "_", n)


def main():
    lay = json.load(open(S + "replay_layouts.json"))["structs"]
    names = sys.argv[1:] or [n for n, v in lay.items() if v["action"] == "report"]
    ida = json.load(open(S + "ida_udts.json"))
    gt = json.load(open(S + "gt_types_all.json"))
    dt = json.load(open(S + "dtor_sizes_checked.json")) if os.path.exists(S + "dtor_sizes_checked.json") else {}
    rd = json.load(open(S + "dtor_sizes.json")) if os.path.exists(S + "dtor_sizes.json") else {}
    rds = {san(k): (k, v) for k, v in rd.items()}
    funcs = [n for s, e, n in json.load(open(S + "functions.json"))]
    plan_names = set()
    for f in glob.glob(S + "apply_plan*.json"):
        if f.endswith("_renames.json"):
            continue
        for e in json.load(open(f)):
            if e.get("mode") == "relayout":
                plan_names.add(e["name"])
    emb = collections.defaultdict(list)
    for pn, t in ida.items():
        mem = t["members"]
        for i, m in enumerate(mem):
            base = re.sub(r"\[\d+\]", "", m[3]).strip()
            if base in ida and "*" not in m[3]:
                nx = mem[i + 1] if i + 1 < len(mem) else None
                emb[base].append({"parent": pn, "member": m[1], "offset": m[0], "size": m[2], "type": m[3],
                                  "next_member": [nx[1], nx[0]] if nx else None, "parent_size": t["size"],
                                  "parent_rebuilt_from_replay_evidence": pn in plan_names})
    gtpar = collections.defaultdict(list)
    for pn, t in gt.items():
        for f in t["fields"]:
            b = re.sub(r"\[\d+\]", "", f[3]).strip()
            gtpar[b].append([pn, f[1], f[0], f[2]])
    prev = collections.defaultdict(list)
    for f in sorted(glob.glob("out/ida_types/proposals/*.json")):
        try:
            d = json.load(open(f, encoding="utf-8"))
        except Exception:
            continue
        for x in d.get("structs", []):
            if isinstance(x, dict) and x.get("name"):
                prev[x["name"]].append({"agent": d.get("agent", os.path.basename(f)[:-5]), "entry": x,
                                        "report": "E:/IW8-Radiant/out/ida_types/results/%s.md" % d.get("agent", "")})
    os.makedirs("out/ida_types/packets2", exist_ok=True)
    for n in names:
        v = lay[n]
        g = v["gt"]
        short = n.split("__")[-1]
        pk = {"struct": n, "game_test_name": g, "ida_size": ida[n]["size"], "game_test_size": gt[g]["size"],
              "builder_size": v["size"], "builder_size_proof": v["size_proof"],
              "why_not_applied": v["extra"].get("why"), "unresolved_residuals": v["unresolved"],
              "game_test_layout": gt[g]["fields"], "game_test_bases": gt[g]["bases"],
              "ida_layout": ida[n]["members"],
              "game_test_parents_holding_it_by_value": gtpar.get(g, [])[:30],
              "embedded_in_ida": emb.get(n, [])[:20],
              "deleting_dtor_lead": None,
              "related_replay_functions_by_name": [f for f in funcs if f.startswith(n + "::") or
                                                   f.startswith(n + "_") or f.startswith(short + "::")][:60],
              "earlier_passes": prev.get(n, [])}
        if n in rds:
            k, dv = rds[n]
            pk["deleting_dtor_lead"] = {"replay_function_name_prefix": k, "sizes_passed_to_sized_delete": dv["all"],
                                        "game_test_check": dt.get(n),
                                        "warning": "names came from an earlier matching tool; prove the destructor "
                                                   "is in this class's own vtable first"}
        json.dump(pk, open("out/ida_types/packets2/%s.json" % n, "w"), indent=0)
    print(len(names), "packets:", names)


if __name__ == "__main__":
    main()
