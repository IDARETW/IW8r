"""Build a Replay layout for a struct from align_by_function.py anchors and the game-test field order.

  * anchor: a game-test field placed by function alignment (votes >= --min-votes);
  * every field gets a Replay size: the size of its type in the IDA local types when that type exists (these are the
    sizes already corrected against Replay), else its game-test size;
  * from each anchor, the following fields are laid out in game-test order with natural alignment (taken from the
    game-test offset: the largest power of two up to 8 that divides it);
  * at the next anchor any residual (anchor start - computed start) is absorbed by the largest field in between whose
    type is NOT a known local type (it becomes a named byte array of the new size); if there is none, the fields after
    the last confident one are left out (pads) and the region is reported as unresolved;
  * member types: the local type when its size matches, `void *` for 8-byte pointers, int / __int16 / unsigned __int8
    / __int64 for basic fields, else `unsigned __int8[n]`.
"""
import argparse, json, sys


def align_of(gt_off):
    for a in (8, 4, 2, 1):
        if gt_off % a == 0:
            return a
    return 1


def up(x, a):
    return (x + a - 1) // a * a


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--align", required=True)
    ap.add_argument("--size", type=int, required=True)
    ap.add_argument("--min-votes", type=int, default=2)
    ap.add_argument("--local-sizes", required=True)
    ap.add_argument("--verified", nargs="*", default=[], help="types whose Replay size is proven: never resized")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    rows = sorted(json.load(open(a.align)), key=lambda r: r["gt"])
    sizes = json.load(open(a.local_sizes))

    def local(r):
        t = r["type"] or ""
        for n in (t, "".join(c if c.isalnum() else "_" for c in t)):
            if n and n in sizes and 0 < sizes[n] < 10 ** 9:
                return n, sizes[n]
        return None, None

    info = []
    for r in rows:
        tn, ts = local(r)
        info.append({"r": r, "tname": tn, "rsize": ts if ts else r["size"], "known": bool(tn), "align": align_of(r["gt"])})
    anchors = [i for i, x in enumerate(info) if x["r"]["replay"] is not None and x["r"]["votes"] >= a.min_votes]
    placed = {}
    notes = {}
    unresolved = []
    guess = set()

    def lay(i, j, start):
        """positions for fields i..j-1 starting at field i = start"""
        pos = {}
        cur = start
        for k in range(i, j):
            if k > i:
                cur = up(cur, info[k]["align"])
            pos[k] = cur
            cur += info[k]["rsize"]
        return pos, cur

    for x, i in enumerate(anchors):
        j = anchors[x + 1] if x + 1 < len(anchors) else len(info)
        start = info[i]["r"]["replay"]
        pos, end = lay(i, j, start)
        if j < len(info):
            target = info[j]["r"]["replay"]
            resid = target - up(end, info[j]["align"])
            if resid != 0:
                cands = [k for k in range(i, j) if not info[k]["known"]]
                k = max(cands, key=lambda k: info[k]["rsize"]) if cands else None
                if k is None or info[k]["rsize"] + resid <= 0:
                    # every field in between has a local type: the largest one's local size disagrees with Replay
                    pool = [k for k in range(i, j) if info[k]["tname"] not in a.verified]
                    k2 = max(pool, key=lambda k: info[k]["rsize"]) if pool else None
                    if k2 is not None and info[k2]["rsize"] + resid > 0:
                        k = k2
                        notes[k] = "LOCAL TYPE %s IS WRONG FOR REPLAY" % info[k]["tname"]
                        info[k]["known"] = False
                if k is not None and info[k]["rsize"] + resid > 0:
                    old = info[k]["rsize"]
                    info[k]["rsize"] += resid
                    notes[k] = (notes.get(k, "") + " resized %d->%d (residual %+d to the next anchor)" % (old, info[k]["rsize"], resid)).strip()
                    pos, end = lay(i, j, start)
                    if abs(resid) < 1024 and k != i:
                        guess.update(kk for kk in range(i + 1, j))
                    # alignment may move the end by a few bytes: absorb again once
                    resid2 = target - up(end, info[j]["align"])
                    if resid2 and info[k]["rsize"] + resid2 > 0:
                        info[k]["rsize"] += resid2
                        pos, end = lay(i, j, start)
                else:
                    unresolved.append((info[i]["r"]["name"], info[j]["r"]["name"], resid))
                    # keep only fields up to the first one that would collide with the next anchor
                    pos = {k: p for k, p in pos.items() if p + info[k]["rsize"] <= target}
        for k, p in pos.items():
            if p + info[k]["rsize"] <= a.size:
                placed[k] = p
    out = []
    BASIC = {1: "unsigned __int8", 2: "__int16", 4: "int", 8: "__int64"}
    for k, p in sorted(placed.items(), key=lambda kv: kv[1]):
        x = info[k]; r = x["r"]; n = x["rsize"]
        if n <= 0:
            continue
        if x["tname"] and x["known"] and sizes.get(x["tname"]) == n:
            ct = x["tname"]
        elif not r["type"] and r.get("tag") == 14 and n == 8:
            ct = "void *"
        elif not r["type"] and r.get("tag") == 16 and n in BASIC and n == r["size"]:
            ct = BASIC[n]
        else:
            ct = "unsigned __int8[%d]" % n
        how = "anchor(%d)" % r["votes"] if k in anchors else ("GUESS" if k in guess else "laid out")
        if k in notes:
            how += ", " + notes[k]
        out.append({"off": p, "name": r["name"], "size": n, "ctype": ct, "how": how, "gt": r["gt"], "gt_size": r["size"]})
    clean, dropped, end = [], [], 0
    for o in out:
        if o["how"].startswith("GUESS"):
            dropped.append(o); continue
        if o["off"] < end:
            dropped.append(o); continue
        clean.append(o); end = o["off"] + o["size"]
    json.dump({"members": clean, "dropped": dropped, "unresolved": unresolved}, open(a.out + ".json", "w"), indent=1)
    with open(a.out + ".txt", "w", encoding="utf-8") as f:
        f.write("# %d members, %d dropped (overlap), %d unresolved gaps; size %d\n" % (len(clean), len(dropped),
                len(unresolved), a.size))
        for o in clean:
            f.write("+0x%06X %7d  %-40s %-34s gt +0x%06X  %s\n" % (o["off"], o["size"], o["name"][:40], o["ctype"][:34],
                                                                  o["gt"], o["how"]))
        f.write("# unresolved (no unknown-type field to absorb the residual):\n")
        for u in unresolved:
            f.write("  between %s and %s: %+d\n" % u)
        f.write("# dropped:\n")
        for o in dropped:
            f.write("  +0x%06X %7d %s (%s)\n" % (o["off"], o["size"], o["name"], o["how"]))
    print(len(clean), "members,", len(dropped), "dropped,", len(unresolved), "unresolved", file=sys.stderr)


if __name__ == "__main__":
    main()
