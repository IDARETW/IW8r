"""Name a struct's Replay offsets by function correspondence, then pick one increasing chain.

For every game-test field F of the struct:
  1. game-test users of F: functions in the game-test decompile that write `<base>->F` (base = cgameGlob, cg,
     LocalClientGlobals[...], or `this` inside the struct's methods);
  2. the same-named Replay functions;
  3. candidate Replay offsets: the struct offsets those Replay functions touch (from scan_struct_access.py), scored by
     how many of them touch it (width must fit F; aggregates count `lea` / first-member accesses at their start).
Then a weighted longest-increasing-chain keeps fields in game-test order. Output: <out>.txt / <out>.json with, per
field, the chosen Replay offset, delta to game-test, score, and the runner-ups. Evidence, not a final layout:
segments where the delta is constant and several fields agree are the trustworthy part.

  python tools/ida_types/align_struct.py --struct cg_t --gt-fields out/ida_types/scan/gt_cg_t.json
      --scan out/ida_types/scan/cg_t --functions out/ida_types/scan/functions.json --max-size 599712
      --base-regex "(?:cgameGlob|\\bcg|LocalClientGlobals\\[[^\\]]*\\])" --this-prefix cg_t:: CgGlobalsMP:: CgGlobalsSP::
      --out out/ida_types/scan/cg_t_align
"""
import argparse, bisect, collections, json, math, os, re, sys

SRC = r"E:\idapluginforcod\sources\cleaned\code_source\src"
HDR = re.compile(r"/\*\n=+\n(.+?)\n=+\n\*/")


def gt_users(fields, base_regex, this_prefix):
    names = {f["name"] for f in fields}
    rx = re.compile(base_regex + r"->(\w+)")
    rthis = re.compile(r"\bthis->(\w+)")
    users = collections.defaultdict(set)
    for dp, dn, fs in os.walk(SRC):
        for fn in fs:
            if not fn.endswith((".cpp", ".c", ".h", ".inl")):
                continue
            try:
                s = open(os.path.join(dp, fn), encoding="utf-8", errors="ignore").read()
            except OSError:
                continue
            pos = [(m.start(), m.group(1).strip()) for m in HDR.finditer(s)]
            for i, (st, name) in enumerate(pos):
                body = s[st: pos[i + 1][0] if i + 1 < len(pos) else len(s)]
                hit = set(m.group(1) for m in rx.finditer(body))
                if any(name.startswith(p) for p in this_prefix):
                    hit |= set(m.group(1) for m in rthis.finditer(body))
                for h in hit & names:
                    users[h].add(name)
    return users


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--struct", required=True)
    ap.add_argument("--gt-fields", required=True)
    ap.add_argument("--scan", required=True, help="prefix of scan_struct_access output")
    ap.add_argument("--functions", required=True)
    ap.add_argument("--max-size", type=int, required=True)
    ap.add_argument("--base-regex", required=True)
    ap.add_argument("--this-prefix", nargs="*", default=[])
    ap.add_argument("--max-level", type=int, default=1)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    fields = sorted(json.load(open(a.gt_fields)), key=lambda f: f["offset"])
    funcacc = {int(k): v for k, v in json.load(open(a.scan + ".funcs.json")).items()}
    byname = {}
    for s, e, n in json.load(open(a.functions)):
        byname.setdefault(n, s)
    # global popularity of each offset (for weighting)
    pop = collections.Counter()
    for f, accs in funcacc.items():
        for off in {x[0] for x in accs if x[3] <= a.max_level}:
            pop[off] += 1
    users = gt_users(fields, a.base_regex, a.this_prefix)
    print("game-test fields with users:", sum(1 for f in fields if users.get(f["name"])), "of", len(fields), file=sys.stderr)
    cand = {}
    for f in fields:
        us = users.get(f["name"], set())
        rs = [byname[u] for u in us if u in byname and byname[u] in funcacc]
        if not rs:
            continue
        score = collections.Counter()
        why = collections.defaultdict(list)
        for r in rs:
            seen = set()
            for off, size, kind, lv in funcacc[r]:
                if lv > a.max_level or off >= a.max_size or off in seen:
                    continue
                ok = False
                if f["size"] <= 16:
                    ok = size <= f["size"] or kind == "addr"
                else:
                    ok = kind == "addr" or size <= 16
                if ok:
                    seen.add(off)
                    score[off] += 1
                    if len(why[off]) < 3:
                        why[off].append(r)
        if not score:
            continue
        top = []
        for off, sc in score.most_common(40):
            w = sc / math.log(2 + pop[off])          # rare offsets that all users share score highest
            top.append((off, sc, round(w, 3), len(rs)))
        top.sort(key=lambda t: -t[2])
        cand[f["name"]] = {"gt": f["offset"], "size": f["size"], "type": f["type"], "users": len(rs), "top": top[:8],
                           "why": {str(o): why[o] for o, _, _, _ in top[:3]}}
    # weighted increasing chain over fields in game-test order
    items = []
    for i, f in enumerate(fields):
        c = cand.get(f["name"])
        if not c:
            continue
        for off, sc, w, n in c["top"][:5]:
            if sc >= 2 or (n == 1 and sc == 1):
                items.append((i, off, w, f["name"]))
    items.sort(key=lambda t: (t[0], t[1]))
    best = {}
    prev = {}
    # O(n^2) is fine for a few thousand items
    order = sorted(range(len(items)), key=lambda k: (items[k][0], items[k][1]))
    dp = [0.0] * len(items)
    par = [-1] * len(items)
    for xi, k in enumerate(order):
        i, off, w, nm = items[k]
        dp[k] = w
        for k2 in order[:xi]:
            i2, off2, w2, _ = items[k2]
            if i2 < i and off2 < off and dp[k2] + w > dp[k]:
                dp[k] = dp[k2] + w
                par[k] = k2
    if items:
        k = max(range(len(items)), key=lambda k: dp[k])
        chain = []
        while k != -1:
            chain.append(items[k])
            k = par[k]
        chain.reverse()
    else:
        chain = []
    chosen = {nm: off for i, off, w, nm in chain}
    res = []
    for f in fields:
        c = cand.get(f["name"])
        row = {"name": f["name"], "gt": f["offset"], "size": f["size"], "type": f["type"],
               "replay": chosen.get(f["name"]), "delta": (chosen[f["name"]] - f["offset"]) if f["name"] in chosen else None,
               "users": c["users"] if c else 0, "top": c["top"][:4] if c else [], "why": c["why"] if c else {}}
        res.append(row)
    json.dump(res, open(a.out + ".json", "w"), indent=1)
    with open(a.out + ".txt", "w", encoding="utf-8") as o:
        o.write("# %s: game-test field -> Replay offset by function correspondence (chain-consistent)\n" % a.struct)
        o.write("# gt_off   size   name                                   replay    delta   users  top candidates (off:score:weight)\n")
        for r in res:
            o.write("+0x%06X %7d %-38s %-9s %-8s %4d   %s\n" % (r["gt"], r["size"], r["name"][:38],
                    ("+0x%06X" % r["replay"]) if r["replay"] is not None else "-",
                    str(r["delta"]) if r["delta"] is not None else "", r["users"],
                    " ".join("0x%X:%d:%.2f" % (t[0], t[1], t[2]) for t in r["top"][:3])))
    print("chain:", len(chain), "fields placed", file=sys.stderr)


if __name__ == "__main__":
    main()
