"""Replay layout of EVERY local struct that has a game-test counterpart, children before parents.

Model: Replay(S) = the game-test field order of S, placed at Replay offsets.
  * anchors: aligned access pairs (align_types.py) mapped onto S's fields; an anchor inside a by-value child is
    converted through the child's own Replay layout; members that the agent waves placed from Replay code (structs
    with a snapshot in out/ida_types/before) count as anchors too; the first field sits at 0;
  * a weighted increasing chain keeps the anchors consistent;
  * between two anchors the fields are laid out with their Replay sizes (child structs: their computed Replay size)
    and natural alignment; a residual at the next anchor is absorbed by the ONE field whose size can change (an array,
    or a child struct without its own size proof); with several such fields the fields between them are left as
    padding (unproven), never guessed;
  * Replay size: an agent-verified size, else aligned sizeof constants (unambiguous, >= 3 votes), else the layout end;
  * no Replay evidence at all: the game-test layout (with the children's Replay sizes) is kept as the prior.
Each struct gets: members [(off, name, size, type candidates, how)], size, size proof, and the IDA action:
  same / fill (only pads get names) / relayout (offsets or size change), with the anchors that justify it.

  python tools/ida_types/replay_layouts.py --out out/ida_types/scan/replay_layouts
"""
import argparse, bisect, collections, json, os, re, sys

S = "out/ida_types/scan/"
PAD = re.compile(r"^(_pad|pad|gap)[0-9A-Fa-f_]*$", re.I)
JUNK = re.compile(r"^(m_unk|unknown|unk_|field_)[0-9A-Fa-f]*", re.I)
BASE_ALIGN = {1: 1, 2: 2, 4: 4, 8: 8, 16: 16}
BASE_C = {"unsigned": "unsigned int", "char16_t": "unsigned __int16", "char32_t": "unsigned int", "HRESULT": "int",
          "std::nullptr_t": "void *", "char8_t": "char", "signed char": "char"}
MANUAL_VERIFIED = {"cg_t", "CgGlobalsMP", "CgGlobalsSP"}


def sanitize(n):
    return re.sub(r"[^A-Za-z0-9_]", "_", n)


class World:
    def __init__(self, gt, ida, pairs, consts, agent):
        self.gt, self.ida, self.agent = gt, ida, agent
        self.pairs = pairs
        self.consts = consts
        idx = collections.defaultdict(list)
        for g in gt:
            idx[sanitize(g)].append(g)
            idx[sanitize(g).rstrip("_")].append(g)
        self.g2i, self.i2g = {}, {}
        for n in ida:
            if n in gt:
                g = n
            else:
                c = set(idx.get(n, [])) | set(idx.get(n.rstrip("_"), []))
                g = c.pop() if len(c) == 1 else None
            if g and g not in self.g2i:
                self.g2i[g] = n
                self.i2g[n] = g
        self.memo_align = {}
        self.alloc = json.load(open(S + "alloc_sizes.json")) if os.path.exists(S + "alloc_sizes.json") else {}
        self.res = {}            # gt name -> result
        self.size_hint = {}      # gt name -> Counter of Replay sizes implied by parents
        sc = collections.Counter(gt[g]["size"] for g in self.g2i)
        self.size_amb = sc

    # ---------------- type helpers ----------------
    def tinfo(self, ctype, tag, size):
        """(kind, child gt name or None, element count) for a game-test field type."""
        if tag == "struct" and ctype in self.gt:
            return "struct", ctype, 1
        if tag == "array":
            m = re.match(r"^(.*?)((?:\[\d+\])+)$", ctype)
            if m and m.group(1).strip() in self.gt:
                el = self.gt[m.group(1).strip()]["size"]
                return "array", m.group(1).strip(), (size // el if el else 0)
            return "array", None, 0
        return tag, None, 1

    def align(self, ctype, tag, size, depth=0):
        if tag in ("base", "enum", "bitfield"):
            return BASE_ALIGN.get(size, 1) if size <= 16 else 8
        if tag == "ptr":
            return 8
        kind, child, n = self.tinfo(ctype, tag, size)
        if child:
            return self.struct_align(child, depth + 1)
        if tag == "array":
            m = re.match(r"^(.*?)((?:\[\d+\])+)$", ctype)
            base = m.group(1).strip() if m else ""
            if base.endswith("*"):
                return 8
            dims = [int(x) for x in re.findall(r"\[(\d+)\]", ctype)] if m else []
            tot = 1
            for d in dims:
                tot *= d
            el = size // tot if tot else 1
            return BASE_ALIGN.get(el, 1) if el <= 16 else 8
        return 1

    def gt_last_end(self, g):
        """End of the last game-test member (base classes clamped to the room before the next member)."""
        t = self.gt[g]
        ent = [(off, self.gt.get(b, {}).get("size", 0)) for off, b in t["bases"]]
        ent += [(off, size) for off, fname, size, ctype, tag in t["fields"]]
        ent.sort()
        end = 0
        for i, (off, size) in enumerate(ent):
            nxt = min([o for o, z in ent[i + 1:] if o > off], default=None)
            if nxt is not None:
                size = min(size, nxt - off)
            end = max(end, off + size)
        return end

    def struct_align(self, g, depth=0):
        if g in self.memo_align:
            return self.memo_align[g]
        self.memo_align[g] = 1
        t = self.gt[g]
        a = 1
        if depth < 20:
            for off, b in t["bases"]:
                if b in self.gt:
                    a = max(a, self.struct_align(b, depth + 1))
            for off, fname, size, ctype, tag in t["fields"]:
                a = max(a, self.align(ctype, tag, size, depth + 1))
        a = min(a, 16)
        while a > 1 and t["size"] % a:
            a //= 2
        # explicit alignment (__declspec(align(N)), HK_ALIGN16): the game-test tail padding shows it
        last = self.gt_last_end(g)
        if last and -(-last // a) * a != t["size"]:
            for a2 in (16, 32, 64):
                if a2 > a and -(-last // a2) * a2 == t["size"]:
                    a = a2
                    break
        self.memo_align[g] = a
        return a

    def tail_gap(self, g):
        """Bytes after the aligned last member that alignment does not explain (virtual-base storage)."""
        sa = self.struct_align(g)
        last = self.gt_last_end(g)
        return max(0, self.gt[g]["size"] - -(-last // sa) * sa)

    def ida_type(self, ctype, tag, size):
        """Candidate IDA spellings for a game-test type, best first."""
        out = []
        if tag in ("base", "enum", "bitfield"):
            b = ctype.split(":")[0] if tag == "bitfield" else ctype
            if tag == "enum":
                if b in self.ida or sanitize(b) in self.ida:
                    out.append(sanitize(b))
            else:
                out.append(BASE_C.get(b, b))
        elif tag == "ptr":
            base = ctype[:-1].strip()
            if base in self.g2i:
                out.append(self.g2i[base] + " *")
            elif base in BASE_C or base in ("char", "unsigned char", "float", "int", "void", "__int64", "bool",
                                             "unsigned __int64", "short", "unsigned short", "wchar_t", "double"):
                out.append(BASE_C.get(base, base) + " *")
            out.append("void *")
        elif tag == "struct":
            if ctype in self.g2i:
                out.append(self.g2i[ctype])
        elif tag == "array":
            m = re.match(r"^(.*?)((?:\[\d+\])+)$", ctype)
            if m:
                base, dims = m.group(1).strip(), m.group(2)
                if base in self.g2i:
                    out.append(self.g2i[base] + dims)
                elif base.endswith("*"):
                    out.append("void *" + dims)
                else:
                    out.append(BASE_C.get(base, base) + dims)
        return out

    def stable(self, g, depth=0):
        """Value types whose size does not change between builds: unions and small structs made only of base
        types (vec3_t, Bounds, ...) or of other stable types."""
        t = self.gt.get(g)
        if not t or depth > 8:
            return False
        if t["kind"] == "union":
            return True
        if t["size"] > 64 or t["bases"]:
            return False
        for off, fname, size, ctype, tag in t["fields"]:
            if tag in ("base", "ptr", "enum", "bitfield"):
                continue
            kind, child, n = self.tinfo(ctype, tag, size)
            if child and self.stable(child, depth + 1):
                continue
            if tag == "array" and not child:
                continue
            return False
        return True

    def rsize_of(self, child):
        r = self.res.get(child)
        if r:
            if r["size_proof"] is None and self.stable(child):
                return self.gt[child]["size"], True
            return r["size"], r["size_proof"] is not None
        return self.gt[child]["size"], self.stable(child)

    # ---------------- per struct ----------------
    def slots(self, g):
        t = self.gt[g]
        f = []
        for boff, b in t["bases"]:
            bs = self.gt.get(b, {}).get("size", 0)
            if bs:
                f.append([boff, "baseclass_%X" % boff, bs, b, "struct", True])
        for off, fname, size, ctype, tag in t["fields"]:
            f.append([off, fname, size, ctype, tag, False])
        f.sort(key=lambda x: (x[0], -x[2]))
        clamped = set()
        for i, x in enumerate(f):
            if x[5]:
                nxt = min([y[0] for y in f[i + 1:] if y[0] > x[0]], default=None)
                if nxt is not None and x[0] + x[2] > nxt:
                    x[2] = nxt - x[0]
                    clamped.add(id(x))
        out = []
        for x in f:
            if out and x[0] < out[-1]["off"] + out[-1]["gsize"]:
                s = out[-1]
                s["group"].append(x)
                s["gsize"] = max(s["gsize"], x[0] + x[2] - s["off"])
                continue
            out.append({"off": x[0], "gsize": x[2], "group": [x]})
        for s in out:
            x = s["group"][0]
            single = len(s["group"]) == 1 or all(y[4] == "bitfield" for y in s["group"])
            s["name"], s["ctype"], s["tag"], s["base"] = x[1], x[3], x[4], x[5]
            s["single"] = single and x[2] == s["gsize"]
            kind, child, n = self.tinfo(x[3], x[4], x[2]) if s["single"] else ("blob", None, 0)
            s["kind"], s["child"], s["count"] = kind, child, n
            s["al"] = self.align(x[3], x[4], x[2]) if s["single"] else 1
            if child and id(x) in clamped:
                s["rsize"] = x[2]
                s["fixed"] = True
                s["el"] = 0
            elif child:
                cs, proven = self.rsize_of(child)
                s["rsize"] = cs * n
                s["fixed"] = proven and kind == "struct"
                s["el"] = cs
            else:
                s["rsize"] = s["gsize"]
                s["fixed"] = kind in ("base", "ptr", "enum", "bitfield")
                s["el"] = 0
            if x[4] == "bitfield":
                s["ctype"] = x[3].split(":")[0]
                s["tag"] = "base"
                s["kind"] = "base"
        return out

    def rmap(self, g, inner, depth=0):
        """Replay offset of game-test inner offset `inner` of struct g (None if unknown)."""
        r = self.res.get(g)
        if not r or depth > 20:
            return inner if not r else None
        k = bisect.bisect_right(r["gstarts"], inner) - 1
        if k < 0:
            return None
        s = r["slots"][k]
        if s.get("roff") is None or inner >= s["off"] + s["gsize"]:
            return None
        sub = inner - s["off"]
        if s["child"] and s["el"] and s["kind"] in ("struct", "array"):
            gel = self.gt[s["child"]]["size"]
            e, rest = divmod(sub, gel) if gel else (0, sub)
            ri = self.rmap(s["child"], rest, depth + 1)
            if ri is None:
                return None
            return s["roff"] + e * s["el"] + ri
        if s["rsize"] != s["gsize"]:
            return s["roff"] + sub if sub < s["rsize"] else None
        return s["roff"] + sub

    def build(self, g):
        t = self.gt[g]
        ida_name = self.g2i[g]
        it = self.ida[ida_name]
        sl = self.slots(g)
        gstarts = [s["off"] for s in sl]
        votes = [collections.Counter() for _ in sl]
        nanch = 0
        for key, n in self.pairs.get(g, {}).items():
            G, R = map(int, key.split(":"))
            k = bisect.bisect_right(gstarts, G) - 1
            if k < 0 or G >= sl[k]["off"] + sl[k]["gsize"]:
                continue
            s = sl[k]
            inner = G - s["off"]
            if s["child"] and s["kind"] in ("struct", "array"):
                gel = self.gt[s["child"]]["size"]
                e, rest = divmod(inner, gel) if gel else (0, inner)
                ri = self.rmap(s["child"], rest)
                if ri is None:
                    continue
                ri += e * s["el"]
            else:
                ri = inner
            votes[k][R - ri] += n
            nanch += n
        agent = ida_name in self.agent or g in MANUAL_VERIFIED
        if agent:
            byname = {m[1]: m for m in it["members"] if not PAD.match(m[1])}
            for k, s in enumerate(sl):
                for y in s["group"]:
                    m = byname.get(y[1]) or (byname.get("baseclass_0") if y[5] and y[0] == 0 else None)
                    if m:
                        votes[k][m[0] - (y[0] - s["off"])] += 5
                        break
        pts = []
        for k, c in enumerate(votes):
            if not c:
                continue
            r, n = c.most_common(1)[0]
            tot = sum(c.values())
            if (n >= 2 and n * 2 > tot) or n >= 5:
                pts.append((k, r, float(n)))
        if sl and not any(p[0] == 0 for p in pts) and sl[0]["off"] == 0:
            pts.append((0, 0, 1000.0))
        ch = []
        if pts:
            from build_all import chain
            ch = chain(pts)
        fixed = {k: r for k, r, w in ch}
        how = {k: "anchor(%d)" % w if w < 1000 else "start" for k, r, w in ch}
        # ---- size target ----
        gsz = t["size"]
        proof = None
        target = None
        if agent and it["size"] > 0 and (it["size"] != gsz or g in MANUAL_VERIFIED):
            target, proof = it["size"], "agent-verified"
        elif g in self.alloc:
            target, proof = self.alloc[g]
        elif self.stable(g) and not any(":" in k for k in self.pairs.get(g, {})):
            target, proof = gsz, None
        else:
            c = self.consts.get(gsz)
            if c:
                (r1, n1), = c.most_common(1)
                n2 = c.most_common(2)[1][1] if len(c) > 1 else 0
                amb = self.size_amb.get(gsz, 1)
                if n1 >= 3 and n1 >= 2 * n2 and amb <= 2 and gsz >= 24:
                    target, proof = r1, "sizeof votes %d" % n1
            h = None
            if target is None and h:
                (r1, n1), = h.most_common(1)
                if n1 >= 2 or len(h) == 1:
                    target, proof = r1, "parent layouts"
        # ---- lay out ----
        order = sorted(fixed)
        roff = [None] * len(sl)
        conf = [None] * len(sl)
        unresolved = []
        resized = {}
        own = {k for k in fixed if how[k].startswith("anchor")}

        def lay_fwd(i, j, start):
            pos, cur = {}, start
            for k in range(i, j):
                if k > i:
                    cur = -(-cur // sl[k]["al"]) * sl[k]["al"]
                pos[k] = cur
                cur += sl[k]["rsize"]
            return pos, cur

        sa = self.struct_align(g)

        def close(j, end):
            if j < len(sl):
                return fixed[j] - (-(-end // sl[j]["al"]) * sl[j]["al"])
            return target - (-(-end // sa) * sa) - self.tail_gap(g)

        prior = not own and not agent
        bounds = order + [len(sl)]
        for x in range(len(order)):
            i, j = order[x], bounds[x + 1]
            start = fixed[i]
            pos, end = lay_fwd(i, j, start)
            closed = j < len(sl) or target is not None
            resid = close(j, end) if closed else 0
            base_conf = "prior" if prior else ("interp" if closed else "open")
            if not resid:
                for k, p in pos.items():
                    roff[k] = p
                    conf[k] = base_conf
                continue
            cands = [k for k in range(i, j) if not sl[k]["fixed"]]
            if len(cands) > 1:
                arr = [k for k in cands if sl[k]["kind"] == "array" and sl[k]["el"] and resid % sl[k]["el"] == 0]
                if len(arr) == 1:
                    cands = arr
            if len(cands) == 1 and sl[cands[0]]["rsize"] + resid > 0:
                c = cands[0]
                sl[c]["rsize"] += resid
                resized[c] = resid
                pos, end = lay_fwd(i, j, start)
                r2 = close(j, end)
                if r2 and sl[c]["rsize"] + r2 > 0:
                    sl[c]["rsize"] += r2
                    resized[c] += r2
                    pos, end = lay_fwd(i, j, start)
                for k, p in pos.items():
                    roff[k] = p
                    conf[k] = "prior" if prior else ("absorbed" if k == c else "interp")
                continue
            c1, c2 = (cands[0], cands[-1]) if cands else (i + 1, j - 1)
            for k, p in pos.items():
                if k < c1 or k == i:
                    roff[k] = p
                    conf[k] = "prior" if prior else "fwd"
            for k in range(max(c2 + 1, i + 1), j):
                roff[k] = pos[k] + resid
                conf[k] = "prior" if prior else "bwd"
            unresolved.append([sl[i]["name"], sl[j]["name"] if j < len(sl) else "<end>", resid, len(cands)])
        for k in own:
            conf[k] = "anchor"
        placed = [k for k in range(len(sl)) if roff[k] is not None]
        end = (roff[placed[-1]] + sl[placed[-1]]["rsize"]) if placed else 0
        size = target if target is not None else max(-(-end // sa) * sa + (self.tail_gap(g) if end else 0), 1)
        for c, d in resized.items():
            s = sl[c]
            if s["child"] and s["kind"] == "struct" and not s["fixed"] and conf[c] == "absorbed":
                self.size_hint.setdefault(s["child"], collections.Counter())[s["rsize"]] += 1
        for k, s in enumerate(sl):
            s["roff"], s["conf"] = roff[k], conf[k]
            s["how"] = how.get(k) or ("resized %+d" % resized[k] if k in resized else (conf[k] or ""))
        cur = 0
        for s in sl:
            if s["roff"] is None:
                continue
            if s["roff"] < cur or s["roff"] + s["rsize"] > size or s["rsize"] <= 0:
                s["roff"] = None
                continue
            cur = s["roff"] + s["rsize"]
        anchored = len(own)
        size_proof = proof
        if size_proof is None and anchored >= 3 and order and not unresolved:
            tail = [k for k in range(order[-1] + 1, len(sl))]
            if all(sl[k]["fixed"] for k in tail):
                size_proof = "anchors to the end"
        maxr = max((int(k.split(":")[1]) for k in self.pairs.get(g, {})), default=-1)
        self.res[g] = {"ida": ida_name, "size": size, "gt_size": gsz, "ida_size": it["size"], "size_proof": size_proof,
                       "slots": sl, "gstarts": gstarts, "anchored": anchored, "evidence": nanch, "agent": agent,
                       "unresolved": unresolved, "max_replay": maxr}

    def pushdown(self, order, orig):
        """Top-down sweep: access pairs of each parent that land in a placed by-value child become pairs of that
        child (and so on down). Returns the extra pairs per child."""
        extra = collections.defaultdict(collections.Counter)
        for g in reversed(order):
            r = self.res.get(g)
            if not r:
                continue
            sl = r["slots"]
            items = list(orig.get(g, {}).items()) + list(extra.get(g, {}).items())
            for key, n in items:
                G, R = map(int, key.split(":"))
                k = bisect.bisect_right(r["gstarts"], G) - 1
                if k < 0:
                    continue
                s = sl[k]
                if not s["child"] or s["roff"] is None or s["conf"] not in ("anchor", "interp", "fwd", "bwd")                         or G >= s["off"] + s["gsize"]:
                    continue
                gel = self.gt[s["child"]]["size"]
                if not gel or not s["el"]:
                    continue
                e, rest = divmod(G - s["off"], gel)
                rr = R - s["roff"] - e * s["el"]
                if 0 <= rr < s["el"]:
                    extra[s["child"]]["%d:%d" % (rest, rr)] += n
        return extra

    def order(self):
        deps = {}
        for g in self.g2i:
            d = set()
            t = self.gt[g]
            for off, b in t["bases"]:
                if b in self.g2i:
                    d.add(b)
            for off, fname, size, ctype, tag in t["fields"]:
                kind, child, n = self.tinfo(ctype, tag, size)
                if child and child in self.g2i and child != g:
                    d.add(child)
            deps[g] = d
        done, out = set(), []
        sys.setrecursionlimit(100000)

        def visit(g, stack):
            if g in done or g in stack:
                return
            stack.add(g)
            for c in deps[g]:
                visit(c, stack)
            stack.discard(g)
            done.add(g)
            out.append(g)
        for g in sorted(deps):
            visit(g, set())
        return out


GOOD = ("anchor", "interp", "fwd", "bwd", "absorbed")


def score(w, g, members):
    """Replay access pairs of g that land on the member of the right name (ok) / on another named member (bad)."""
    r = w.res[g]
    mem = sorted((m[0], m[1], m[2]) for m in members if not PAD.match(m[1]))
    starts = [m[0] for m in mem]
    ok = bad = 0
    for key, n in w.pairs.get(g, {}).items():
        G, R = map(int, key.split(":"))
        k = bisect.bisect_right(r["gstarts"], G) - 1
        if k < 0:
            continue
        s = r["slots"][k]
        if G >= s["off"] + s["gsize"]:
            continue
        names = {y[1] for y in s["group"]}
        if s["base"]:
            names |= {"baseclass_0", "baseclass_%X" % s["off"]}
        j = bisect.bisect_right(starts, R) - 1
        if j < 0 or R >= mem[j][0] + max(mem[j][2], 1):
            continue
        m = mem[j]
        if m[1] in names and (s["child"] or R - m[0] == G - s["off"]):
            ok += n
        else:
            bad += n
    return ok, bad


def compare(w, g):
    """IDA action for struct g: same / fill / relayout / report, with the member list to apply."""
    r = w.res[g]
    it = w.ida[r["ida"]]
    mem = [m for m in it["members"] if not PAD.match(m[1])]
    byname = {m[1]: m for m in mem}
    want = []
    for s in r["slots"]:
        if s["roff"] is None:
            continue
        nm = s["name"]
        if s["base"]:
            nm = "baseclass_%X" % s["roff"] if s["roff"] else "baseclass_0"
        cand = []
        m = byname.get(nm)
        if m and m[2] == s["rsize"]:
            cand.append("@" + nm)            # keep the IDA member's own type
        if s["single"] and s["rsize"] == s["gsize"]:
            cand += w.ida_type(s["ctype"], s["tag"], s["gsize"])
        elif s["single"] and s["kind"] == "array" and s["child"] in w.g2i and s["el"] and s["rsize"] % s["el"] == 0:
            cand.append("%s[%d]" % (w.g2i[s["child"]], s["rsize"] // s["el"]))
        elif s["single"] and s["kind"] == "struct" and s["child"] in w.g2i:
            cand.append(w.g2i[s["child"]])
        want.append([s["roff"], nm, s["rsize"], cand, s["conf"]])
    starts = [m[0] for m in it["members"]]

    def fills(allowed):
        out = []
        for x in want:
            if x[1] in byname or x[4] not in allowed:
                continue
            j = bisect.bisect_right(starts, x[0]) - 1
            if j >= 0:
                pm = it["members"][j]
                if PAD.match(pm[1]) and pm[0] + pm[2] >= x[0] + x[2]:
                    out.append(x)
        return out

    old = score(w, g, mem)
    info = {"score_old": old}
    wn = {x[1]: x for x in want}
    # offset moves of existing members (width-only differences are ignored)
    moved = [[m[1], m[0], wn[m[1]][0], wn[m[1]][4]] for m in mem if m[1] in wn and wn[m[1]][0] != m[0]]
    info["moved"] = moved
    size_change = r["size"] != it["size"]
    if r["agent"] or (not moved and not size_change):
        f = fills(GOOD + (() if r["agent"] else ("open", "prior")))
        return ("fill" if f else "same"), f, info
    # candidate relayout: confident builder fields + IDA members that are not game-test fields and do not overlap
    items = [x for x in want if x[4] in GOOD or (x[4] == "open" and not size_change)]
    taken = sorted((x[0], x[0] + x[2]) for x in items)
    gone = []
    for m in mem:
        if m[1] in wn and wn[m[1]] in items:
            continue
        lo, hi = m[0], m[0] + max(m[2], 1)
        k = bisect.bisect_right(taken, (lo, 10 ** 12)) - 1
        okk = hi <= r["size"] and not JUNK.match(m[1])
        if k >= 0 and taken[k][1] > lo:
            okk = False
        if k + 1 < len(taken) and taken[k + 1][0] < hi:
            okk = False
        if okk:
            items.append([m[0], m[1], m[2], ["@" + m[1]], "kept IDA member"])
            bisect.insort(taken, (lo, hi))
        else:
            gone.append(m[1])
    items.sort(key=lambda x: x[0])
    new = score(w, g, items)
    info.update({"score_new": new, "dropped": gone})
    proven_size = r["size_proof"] is not None and (r["size_proof"] != "parent layouts")
    reasons = []
    if size_change and not proven_size:
        reasons.append("size %d -> %d not proven" % (it["size"], r["size"]))
    if size_change and r["max_replay"] >= r["size"]:
        reasons.append("Replay accesses +%d past the new size" % r["max_replay"])
    if moved and not (new[0] > old[0] and new[1] <= old[1] // 2):
        reasons.append("moves not better supported (ok/bad %s -> %s)" % (old, new))
    if not moved and size_change and not reasons and new[1] > old[1]:
        reasons.append("more conflicts after resize")
    if reasons:
        info["why"] = reasons
        f = fills(GOOD)
        return "report", f, info
    return "relayout", items, info


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--passes", type=int, default=2)
    a = ap.parse_args()
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    gt = json.load(open(S + "gt_types_all.json"))
    ida = json.load(open(S + "ida_udts.json"))
    al = json.load(open(S + "all_align.json"))
    consts = collections.defaultdict(collections.Counter)
    for k, n in al["consts"].items():
        g, r = map(int, k.split(":"))
        consts[g][r] += n
    agent = {f.split(".")[0] for f in os.listdir("out/ida_types/before")}
    w = World(gt, ida, al["pairs"], consts, agent)
    order = w.order()
    orig = {k: dict(v) for k, v in w.pairs.items()}
    extra = {}
    for p in range(a.passes):
        merged = {k: dict(v) for k, v in orig.items()}
        for c, cnt in extra.items():
            d = merged.setdefault(c, {})
            for k, n in cnt.items():
                d[k] = d.get(k, 0) + n
        w.pairs = merged
        w.res = {}
        w.size_hint = {} if p == 0 else w.size_hint
        for g in order:
            w.build(g)
        extra = w.pushdown(order, orig)
        print("pass", p + 1, "pushed-down pairs:", sum(len(v) for v in extra.values()), file=sys.stderr)
    out, cnt = {}, collections.Counter()
    for g in order:
        r = w.res[g]
        act, items, extra = compare(w, g)
        cnt[act] += 1
        out[r["ida"]] = {"gt": g, "action": act, "size": r["size"], "ida_size": r["ida_size"], "gt_size": r["gt_size"],
                         "size_proof": r["size_proof"], "anchored": r["anchored"], "evidence": r["evidence"],
                         "agent": r["agent"], "unresolved": r["unresolved"], "items": items, "extra": extra}
    json.dump({"order": [w.g2i[g] for g in order], "structs": out}, open(a.out + ".json", "w"))
    with open(a.out + ".txt", "w") as f:
        for n in [w.g2i[g] for g in order]:
            v = out[n]
            if v["action"] == "same":
                continue
            e = v["extra"]
            f.write("%-9s %-44s size %d -> %d (gt %d, %s) anchors %d score %s -> %s fills %d%s\n" % (
                v["action"], n[:44], v["ida_size"], v["size"], v["gt_size"], v["size_proof"], v["anchored"],
                e.get("score_old"), e.get("score_new", "-"), len(v["items"]) if v["action"] != "relayout" else 0,
                " UNRESOLVED %s" % v["unresolved"] if v["unresolved"] else ""))
            for mv in e.get("moved", [])[:10]:
                f.write("      moved %s %d -> %d (%s)\n" % tuple(mv))
            if e.get("dropped"):
                f.write("      dropped %s\n" % e["dropped"][:12])
            if e.get("why"):
                f.write("      not applied: %s\n" % "; ".join(e["why"]))
    print("structs:", len(order), dict(cnt), "mapped IDA names:", len(w.g2i))


if __name__ == "__main__":
    main()
