"""Align game-test and Replay struct accesses for EVERY struct at once (output of scan_types.py).

For each function both builds share (same name) and each struct it reaches through a typed argument in both, the two
instruction-ordered access lists are aligned (Needleman-Wunsch on width + kind). An aligned pair votes that game-test
offset G of the struct is Replay offset R. Immediate constants are aligned the same way (on mnemonic), so a
game-test constant (e.g. a sizeof used by imul) votes for its Replay counterpart.

Writes <out>.json: {"pairs": {struct: {"G:R": nfuncs}}, "consts": {"G:R": nfuncs}}
  python tools/ida_types/align_types.py --replay out/ida_types/scan/replay_types.jsonl --gt out/ida_types/scan/gt_types.jsonl
      --replay-functions out/ida_types/scan/functions.json --gt-functions out/ida_types/scan/functions_gt.json
      --out out/ida_types/scan/all_align
"""
import argparse, collections, json, multiprocessing as mp, sys

MAXLEN = 1200


def nw(a, b, score):
    n, m = len(a), len(b)
    if not n or not m:
        return []
    GAP = -1
    prev = [j * GAP for j in range(m + 1)]
    back = [bytearray(m + 1) for _ in range(n + 1)]
    for j in range(1, m + 1):
        back[0][j] = 2
    for i in range(1, n + 1):
        cur = [i * GAP] + [0] * m
        bi = back[i]
        bi[0] = 1
        ai = a[i - 1]
        for j in range(1, m + 1):
            d = prev[j - 1] + score(ai, b[j - 1])
            u = prev[j] + GAP
            l = cur[j - 1] + GAP
            if d >= u and d >= l:
                cur[j] = d; bi[j] = 0
            elif u >= l:
                cur[j] = u; bi[j] = 1
            else:
                cur[j] = l; bi[j] = 2
        prev = cur
    i, j, out = n, m, []
    while i > 0 and j > 0:
        k = back[i][j]
        if k == 0:
            if score(a[i - 1], b[j - 1]) > 0:
                out.append((i - 1, j - 1))
            i -= 1; j -= 1
        elif k == 1:
            i -= 1
        else:
            j -= 1
    return out


def acc_score(x, y):
    if x[1] == y[1] and x[2] == y[2]:
        return 2
    if x[1] == y[1]:
        return 1
    return -1


def k_score(x, y):
    if x[0] == y[0] and x[1] == y[1]:
        return 3
    if x[0] == y[0]:
        return 1
    return -1


def work(job):
    ga, ra, gk, rk = job
    pairs = collections.defaultdict(set)
    for st in set(ga) & set(ra):
        a, b = ga[st], ra[st]
        if len(a) > MAXLEN or len(b) > MAXLEN:
            continue
        for i, j in nw(a, b, acc_score):
            pairs[st].add((a[i][0], b[j][0]))
    consts = set()
    if gk and rk and len(gk) <= MAXLEN and len(rk) <= MAXLEN:
        for i, j in nw(gk, rk, k_score):
            consts.add((gk[i][1], rk[j][1]))
    return {k: list(v) for k, v in pairs.items()}, list(consts)


def load(path, funcs):
    name = {s: n for s, e, n in json.load(open(funcs))}
    out = {}
    for line in open(path):
        r = json.loads(line)
        n = name.get(r["s"])
        if not n:
            continue
        by = collections.defaultdict(list)
        for addr, st, off, w, kind in r["acc"]:
            by[st].append((off, w, kind))
        out[n] = (dict(by), [(op, v) for addr, op, v in r["k"]])
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--replay", required=True)
    ap.add_argument("--gt", required=True)
    ap.add_argument("--replay-functions", required=True)
    ap.add_argument("--gt-functions", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    R = load(a.replay, a.replay_functions)
    Gt = load(a.gt, a.gt_functions)
    shared = sorted(set(R) & set(Gt))
    print("shared functions:", len(shared), file=sys.stderr)
    jobs = [(Gt[n][0], R[n][0], Gt[n][1], R[n][1]) for n in shared]
    pairs = collections.defaultdict(collections.Counter)
    consts = collections.Counter()
    with mp.Pool(max(1, mp.cpu_count() - 2)) as pool:
        for k, (p, c) in enumerate(pool.imap_unordered(work, jobs, chunksize=16)):
            for st, lst in p.items():
                for g, r in lst:
                    pairs[st]["%d:%d" % (g, r)] += 1
            for g, r in c:
                consts["%d:%d" % (g, r)] += 1
            if k % 2000 == 0:
                print("  aligned", k, file=sys.stderr)
    json.dump({"pairs": {k: dict(v) for k, v in pairs.items()}, "consts": dict(consts)}, open(a.out + ".json", "w"))
    print("structs with pairs:", len(pairs), "const pairs:", len(consts), file=sys.stderr)


if __name__ == "__main__":
    main()
