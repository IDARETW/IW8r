"""Name Replay struct offsets by aligning the same functions in Replay and game-test.

Both exes are scanned with scan_struct_access.py for the same struct (Replay offsets unknown, game-test offsets known
from its PDB). For every function name present in both scans, the two instruction-ordered access lists
(width, read/write/addr) are aligned with Needleman-Wunsch. Each aligned pair (game-test offset g, Replay offset r)
votes that the game-test field holding g starts at r - (g - field_start) in Replay. A weighted increasing chain then
keeps fields in game-test order. Output <out>.txt/.json: per game-test field the chosen Replay start, delta, number of
functions voting for it, and runner-ups.

  python tools/ida_types/align_by_function.py --replay out/ida_types/scan/cg_t --gt out/ida_types/scan/gt_cg_t_scan
      --replay-functions out/ida_types/scan/functions.json --gt-functions out/ida_types/scan/functions_gt.json
      --gt-type cg_t --gt-type-tail CgGlobalsMP --replay-size 599712 --out out/ida_types/scan/cg_t_byfunc
"""
import argparse, bisect, collections, json, re, subprocess, sys

PDB = r"E:\1-game-test\1-game_test.pdb"
PDB_LAYOUT = r"E:\1-game-test\tools\gametest-workbench\pdb-layout.exe"


def gt_fields(name):
    r = subprocess.run([PDB_LAYOUT, PDB, name], capture_output=True, timeout=60)
    return [t for t in json.loads(r.stdout)["types"] if t["name"] == name][0]


def load_scan(prefix, max_level):
    acc = json.load(open(prefix + ".funcs.json"))
    out = {}
    for f, lst in acc.items():
        seq = sorted((a, off, size, kind) for a, off, size, kind, lv in lst if lv <= max_level)
        if seq:
            out[int(f)] = [(off, size, kind) for a, off, size, kind in seq]
    return out


def names(path):
    d = {}
    for s, e, n in json.load(open(path)):
        d[s] = n
    return d


def norm(n):
    return re.sub(r"_\d+$", "", n)


def align(A, B):
    n, m = len(A), len(B)
    GAP = -1
    S = [[0] * (m + 1) for _ in range(n + 1)]
    for i in range(1, n + 1):
        S[i][0] = i * GAP
    for j in range(1, m + 1):
        S[0][j] = j * GAP
    def sim(a, b):
        if a[1] == b[1] and a[2] == b[2]:
            return 3
        if a[1] == b[1]:
            return 1
        return -2
    for i in range(1, n + 1):
        ai = A[i - 1]
        Si, Sp = S[i], S[i - 1]
        for j in range(1, m + 1):
            v = Sp[j - 1] + sim(ai, B[j - 1])
            if Sp[j] + GAP > v:
                v = Sp[j] + GAP
            if Si[j - 1] + GAP > v:
                v = Si[j - 1] + GAP
            Si[j] = v
    pairs = []
    i, j = n, m
    while i > 0 and j > 0:
        s = sim(A[i - 1], B[j - 1])
        if S[i][j] == S[i - 1][j - 1] + s:
            if s > 0:
                pairs.append((A[i - 1], B[j - 1]))
            i -= 1; j -= 1
        elif S[i][j] == S[i - 1][j] + GAP:
            i -= 1
        else:
            j -= 1
    return pairs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--replay", required=True)
    ap.add_argument("--gt", required=True)
    ap.add_argument("--replay-functions", required=True)
    ap.add_argument("--gt-functions", required=True)
    ap.add_argument("--gt-type", required=True)
    ap.add_argument("--gt-type-tail", default=None, help="derived game-test type whose own fields follow the base")
    ap.add_argument("--replay-size", type=int, required=True)
    ap.add_argument("--max-level", type=int, default=1)
    ap.add_argument("--max-len", type=int, default=600)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    base = gt_fields(a.gt_type)
    fields = list(base["fields"])
    if a.gt_type_tail:
        tail = gt_fields(a.gt_type_tail)
        fields += [f for f in tail["fields"] if f["offset"] >= base["size"]]
    fields.sort(key=lambda f: f["offset"])
    fstarts = [f["offset"] for f in fields]
    R = load_scan(a.replay, a.max_level)
    G = load_scan(a.gt, a.max_level)
    rn, gn = names(a.replay_functions), names(a.gt_functions)
    gt_by_name = collections.defaultdict(list)
    for s in G:
        gt_by_name[gn.get(s, "")].append(s)
    votes = collections.defaultdict(collections.Counter)   # field index -> replay start -> functions
    why = collections.defaultdict(lambda: collections.defaultdict(list))
    common = 0
    for rs, seq in R.items():
        nm = rn.get(rs, "")
        cands = gt_by_name.get(nm) or gt_by_name.get(norm(nm)) or []
        if len(cands) != 1:
            continue
        gseq = G[cands[0]]
        if len(seq) > a.max_len or len(gseq) > a.max_len:
            continue
        common += 1
        seen = set()
        for (r_off, r_sz, r_k), (g_off, g_sz, g_k) in align(seq, gseq):
            k = bisect.bisect_right(fstarts, g_off) - 1
            if k < 0:
                continue
            f = fields[k]
            if g_off >= f["offset"] + max(f["size"], 1):
                continue
            start = r_off - (g_off - f["offset"])
            if start < 0 or start >= a.replay_size + 200000:
                continue
            key = (k, start)
            if key in seen:
                continue
            seen.add(key)
            votes[k][start] += 1
            if len(why[k][start]) < 3:
                why[k][start].append(nm)
    print("functions aligned:", common, "fields with votes:", len(votes), file=sys.stderr)
    # weighted increasing chain
    items = []
    for k, c in votes.items():
        for start, v in c.most_common(4):
            if v >= 2:
                items.append((k, start, v))
    items.sort()
    dp = [0] * len(items)
    par = [-1] * len(items)
    for x in range(len(items)):
        k, st, v = items[x]
        dp[x] = v
        for y in range(x):
            k2, st2, v2 = items[y]
            if k2 < k and st2 + max(1, min(fields[k2]["size"], 64)) <= st + 64 and st2 < st and dp[y] + v > dp[x]:
                dp[x] = dp[y] + v
                par[x] = y
    chain = []
    if items:
        x = max(range(len(items)), key=lambda i: dp[i])
        while x != -1:
            chain.append(items[x]); x = par[x]
        chain.reverse()
    chosen = {k: (st, v) for k, st, v in chain}
    res = []
    for k, f in enumerate(fields):
        c = votes.get(k, collections.Counter())
        st, v = chosen.get(k, (None, 0))
        res.append({"name": f["name"], "gt": f["offset"], "size": f["size"], "type": f["type"], "tag": f.get("tag"), "replay": st,
                    "delta": (st - f["offset"]) if st is not None else None, "votes": v,
                    "top": c.most_common(3), "why": why[k].get(st, []) if st is not None else []})
    json.dump(res, open(a.out + ".json", "w"), indent=1)
    with open(a.out + ".txt", "w", encoding="utf-8") as o:
        o.write("# %s by function alignment: %d common functions\n" % (a.gt_type, common))
        o.write("# gt_off    size  name                                     replay     delta    votes  top\n")
        for r in res:
            o.write("+0x%06X %7d %-40s %-10s %-8s %4d   %s  %s\n" % (r["gt"], r["size"], r["name"][:40],
                    ("+0x%06X" % r["replay"]) if r["replay"] is not None else "-",
                    str(r["delta"]) if r["delta"] is not None else "", r["votes"],
                    " ".join("0x%X:%d" % t for t in r["top"]), ",".join(r["why"][:2])))
    print("chain fields:", len(chain), file=sys.stderr)


if __name__ == "__main__":
    main()
