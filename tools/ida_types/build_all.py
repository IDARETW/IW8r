"""Per-struct verdict for EVERY struct from the all-struct alignment (align_types.py).

For each struct S with aligned access pairs:
  1. a weighted increasing chain of (game-test offset G -> Replay offset R) anchors (votes = shared functions),
  2. each game-test field with an anchor gets a Replay offset (exact = an anchor at the field start),
  3. compared with the IDA local type: OK (IDA member of that name at that offset), MOVED (IDA has it elsewhere),
     FILL (IDA has only padding there: the field can be named in place), CONFLICT (another IDA member is there),
  4. a size verdict from aligned constants (game-test sizeof -> Replay value) and the highest Replay access.
Writes <out>.json (per struct detail) and <out>.txt (summary table, worst first).

  python tools/ida_types/build_all.py --align out/ida_types/scan/all_align.json --gt-types out/ida_types/scan/gt_types_all.json
      --ida out/ida_types/scan/ida_udts.json --out out/ida_types/scan/all_verdict
"""
import argparse, bisect, collections, json, re

PAD = re.compile(r"^(_pad|pad|gap|field_|_unk|unk)[0-9A-Fa-f_]*$", re.I)


def chain(points):
    """points: [(G, R, w)] -> max-weight subset strictly increasing in both G and R."""
    pts = sorted(points)
    n = len(pts)
    best = [0.0] * n
    prev = [-1] * n
    for i in range(n):
        g, r, w = pts[i]
        best[i] = w
        for j in range(max(0, i - 400), i):
            gj, rj, wj = pts[j]
            if gj < g and rj < r and best[j] + w > best[i]:
                best[i] = best[j] + w
                prev[i] = j
    if not n:
        return []
    i = max(range(n), key=lambda k: best[k])
    out = []
    while i >= 0:
        out.append(pts[i])
        i = prev[i]
    return out[::-1]


def gt_fields(gt, name, base_off=0, depth=0):
    """Top-level fields of a game-test UDT with base classes flattened in (offsets relative to name)."""
    t = gt.get(name)
    if not t:
        return []
    out = []
    for boff, bname in t["bases"]:
        if depth < 6:
            out += gt_fields(gt, bname, base_off + boff, depth + 1)
    for off, fname, size, ctype, tag in t["fields"]:
        out.append((base_off + off, fname, size, ctype, tag))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--align", required=True)
    ap.add_argument("--gt-types", required=True)
    ap.add_argument("--ida", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--min-votes", type=int, default=2)
    a = ap.parse_args()
    al = json.load(open(a.align))
    gt = json.load(open(a.gt_types))
    ida = json.load(open(a.ida))
    consts = collections.defaultdict(collections.Counter)
    for k, n in al["consts"].items():
        g, r = map(int, k.split(":"))
        consts[g][r] += n
    size_count = collections.Counter(t["size"] for t in gt.values())
    res = {}
    for st, votes in al["pairs"].items():
        pts = collections.defaultdict(collections.Counter)
        for k, n in votes.items():
            g, r = map(int, k.split(":"))
            pts[g][r] += n
        cand = []
        for g, c in pts.items():
            r, n = c.most_common(1)[0]
            tot = sum(c.values())
            if n * 2 > tot or n >= 3:
                cand.append((g, r, float(n)))
        ch = chain(cand)
        # keep anchors with enough votes, or single votes whose delta matches both neighbours
        keep = []
        for i, (g, r, w) in enumerate(ch):
            d = r - g
            if w >= a.min_votes:
                keep.append((g, r, int(w)))
            elif 0 < i < len(ch) - 1 and ch[i - 1][1] - ch[i - 1][0] == d == ch[i + 1][1] - ch[i + 1][0]:
                keep.append((g, r, int(w)))
        gsz = gt.get(st, {}).get("size")
        it = ida.get(st)
        fields = gt_fields(gt, st)
        gs = [g for g, r, w in keep]
        rows = []
        if fields:
            for off, fname, size, ctype, tag in fields:
                i = bisect.bisect_left(gs, off)
                inside = []
                while i < len(keep) and keep[i][0] < off + max(size, 1):
                    inside.append(keep[i]); i += 1
                if not inside:
                    continue
                exact = [x for x in inside if x[0] == off]
                if exact:
                    rep = exact[0][1]; ev = "exact"; w = exact[0][2]
                else:
                    x = inside[0]
                    rep = x[1] - (x[0] - off); ev = "inner"; w = x[2]
                rows.append({"name": fname, "gt": off, "size": size, "type": ctype, "tag": tag, "replay": rep,
                             "ev": ev, "votes": w})
        # IDA comparison
        status = collections.Counter()
        if it:
            mem = it["members"]
            byname = {m[1]: m for m in mem}
            starts = [m[0] for m in mem]
            for row in rows:
                m = byname.get(row["name"])
                if m and m[0] == row["replay"]:
                    row["ida"] = "OK"
                elif m:
                    row["ida"] = "MOVED %+d" % (m[0] - row["replay"])
                else:
                    j = bisect.bisect_right(starts, row["replay"]) - 1
                    if j >= 0:
                        mm = mem[j]
                        if PAD.match(mm[1]) and mm[0] + mm[2] >= row["replay"] + row["size"]:
                            row["ida"] = "FILL"
                        else:
                            row["ida"] = "CONFLICT %s" % mm[1]
                    else:
                        row["ida"] = "CONFLICT start"
                status[row["ida"].split()[0]] += 1
        maxr = max([r for g, r, w in keep], default=0)
        sv = consts.get(gsz, collections.Counter()) if gsz else collections.Counter()
        size_vote = sv.most_common(3)
        deltas = collections.Counter(r - g for g, r, w in keep)
        res[st] = {"gt_size": gsz, "ida_size": it["size"] if it else None, "anchors": len(keep),
                   "max_replay_off": maxr, "deltas": deltas.most_common(6), "size_votes": size_vote,
                   "size_ambiguity": size_count.get(gsz, 0), "status": dict(status), "rows": rows,
                   "chain": keep}
    json.dump(res, open(a.out + ".json", "w"))
    order = sorted(res.items(), key=lambda kv: (-(kv[1]["status"].get("MOVED", 0) + kv[1]["status"].get("CONFLICT", 0)),
                                                 -kv[1]["anchors"]))
    with open(a.out + ".txt", "w") as f:
        f.write("%-44s %8s %8s %6s %4s %5s %5s %5s  %s | %s\n" % ("struct", "gt", "ida", "anch", "ok", "moved",
                                                               "confl", "fill", "deltas", "size votes"))
        for st, v in order:
            s = v["status"]
            f.write("%-44s %8s %8s %6d %4d %5d %5d %5d  %s | %s%s\n" % (
                st[:44], v["gt_size"], v["ida_size"], v["anchors"], s.get("OK", 0), s.get("MOVED", 0),
                s.get("CONFLICT", 0), s.get("FILL", 0), " ".join("%+d:%d" % d for d in v["deltas"][:4]),
                " ".join("%d:%d" % x for x in v["size_votes"]),
                (" (amb %d)" % v["size_ambiguity"]) if v["size_ambiguity"] > 1 else ""))
    tot = collections.Counter()
    for v in res.values():
        tot.update(v["status"])
    print("structs:", len(res), "with IDA type:", sum(1 for v in res.values() if v["ida_size"] is not None),
          "field verdicts:", dict(tot))


if __name__ == "__main__":
    main()
