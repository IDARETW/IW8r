"""Every game-test struct / class / union layout from the llvm-pdbutil dump (E:\\1-game-test\\types.txt).

Writes <out>.json: {name: {"size", "kind", "bases": [[off, type]], "fields": [[off, name, size, type, tag]]}}
tag = base / ptr / array / struct / enum / bitfield; `type` is the C spelling (base type, struct name, `T[n]`, `T *`).
Game-test is the NAME oracle (names, order, base types); Replay code decides offsets and sizes.

  python tools/ida_types/gt_types_dump.py out/ida_types/scan/gt_types_all
"""
import json, re, sys

SRC = r"E:\1-game-test\types.txt"
REC = re.compile(r"^\s*0x([0-9A-F]+) \| (LF_\w+) \[size = \d+\](?: `(.*)`)?")
BASE_SIZE = {"char": 1, "signed char": 1, "unsigned char": 1, "bool": 1, "short": 2, "unsigned short": 2,
             "wchar_t": 2, "char16_t": 2, "int": 4, "unsigned": 4, "long": 4, "unsigned long": 4, "float": 4,
             "char32_t": 4, "__int64": 8, "unsigned __int64": 8, "double": 8, "HRESULT": 4, "void": 0,
             "std::nullptr_t": 8, "char8_t": 1}


def parse():
    recs = {}
    cur = None
    for line in open(SRC, encoding="utf-8", errors="ignore"):
        m = REC.match(line)
        if m:
            cur = {"k": m.group(2), "n": m.group(3), "l": []}
            recs[int(m.group(1), 16)] = cur
            continue
        if cur is not None:
            cur["l"].append(line.strip())
    return recs


def basetype(ref):
    m = re.match(r"0x([0-9A-F]+)(?: \((.*)\))?", ref)
    idx = int(m.group(1), 16)
    return idx, m.group(2)


class T:
    def __init__(self, recs):
        self.recs = recs
        self.byname = {}
        for i, r in recs.items():
            if r["k"] in ("LF_STRUCTURE", "LF_CLASS", "LF_UNION") and r["n"]:
                txt = " ".join(r["l"])
                ms = re.search(r"sizeof (\d+)", txt)
                if ms and "forward ref" not in txt:
                    self.byname.setdefault(r["n"], i)
        self.cache = {}

    def desc(self, ref):
        """(size, ctype, tag) of a type reference such as `0x0074 (int)` or `0x1234`."""
        idx, simple = basetype(ref)
        if idx < 0x1000:
            s = simple or "?"
            if s.endswith("*"):
                return 8, s, "ptr"
            return BASE_SIZE.get(s, 0), s, "base"
        if idx in self.cache:
            return self.cache[idx]
        self.cache[idx] = (0, "?", "base")
        r = self.recs.get(idx)
        out = (0, "?", "base")
        if r:
            txt = " ".join(r["l"])
            k = r["k"]
            if k == "LF_POINTER":
                out = (8, "void *", "ptr")
                mm = re.search(r"referent = (0x[0-9A-F]+(?: \([^)]*\))?)", txt)
                if mm:
                    sub = self.desc(mm.group(1))
                    out = (8, sub[1] + " *", "ptr")
            elif k == "LF_MODIFIER":
                mm = re.search(r"referent = (0x[0-9A-F]+(?: \([^)]*\))?)", txt)
                out = self.desc(mm.group(1)) if mm else out
            elif k == "LF_ARRAY":
                mm = re.search(r"size: (\d+), index type: .*?, element type: (0x[0-9A-F]+(?: \([^)]*\))?)", txt)
                if mm:
                    tot = int(mm.group(1))
                    el = self.desc(mm.group(2))
                    n = tot // el[0] if el[0] else 0
                    base = el[1]
                    dims = ""
                    m2 = re.match(r"^(.*?)((?:\[\d+\])+)$", base)
                    if m2:
                        base, dims = m2.group(1), m2.group(2)
                    out = (tot, "%s[%d]%s" % (base, n, dims), "array")
            elif k in ("LF_STRUCTURE", "LF_CLASS", "LF_UNION"):
                name = r["n"]
                full = self.byname.get(name)
                size = 0
                if full is not None:
                    ms = re.search(r"sizeof (\d+)", " ".join(self.recs[full]["l"]))
                    size = int(ms.group(1)) if ms else 0
                out = (size, name, "struct")
            elif k == "LF_ENUM":
                mm = re.search(r"underlying type: (0x[0-9A-F]+(?: \([^)]*\))?)", txt)
                sz = self.desc(mm.group(1))[0] if mm else 4
                out = (sz, r["n"], "enum")
            elif k == "LF_BITFIELD":
                mm = re.search(r"type = (0x[0-9A-F]+(?: \([^)]*\))?), bit offset = (\d+), # bits = (\d+)", txt)
                if mm:
                    sub = self.desc(mm.group(1))
                    out = (sub[0], "%s:%s@%s" % (sub[1], mm.group(3), mm.group(2)), "bitfield")
            elif k == "LF_PROCEDURE" or k == "LF_MFUNCTION":
                out = (0, "fn", "base")
        self.cache[idx] = out
        return out

    def layout(self, idx):
        r = self.recs[idx]
        txt = " ".join(r["l"])
        size = int(re.search(r"sizeof (\d+)", txt).group(1))
        fl = re.search(r"field list: (0x[0-9A-F]+)", txt)
        fields, bases = [], []
        if fl:
            f = self.recs.get(int(fl.group(1), 16))
            lines = f["l"] if f else []
            i = 0
            while i < len(lines):
                ln = lines[i]
                if ln.startswith("- LF_MEMBER"):
                    mm = re.match(r"- LF_MEMBER \[name = `(.*)`, Type = (0x[0-9A-F]+(?: \([^)]*\))?), offset = (\d+)",
                                  ln)
                    if mm:
                        sz, ct, tag = self.desc(mm.group(2))
                        fields.append([int(mm.group(3)), mm.group(1), sz, ct, tag])
                elif ln.startswith("- LF_BCLASS") and i + 1 < len(lines):
                    mm = re.match(r"type = (0x[0-9A-F]+), offset = (\d+)", lines[i + 1])
                    if mm:
                        bases.append([int(mm.group(2)), self.desc(mm.group(1))[1]])
                elif ln.startswith("- LF_VFUNCTAB"):
                    fields.append([0, "__vftable", 8, "void *", "ptr"])
                i += 1
        kind = {"LF_STRUCTURE": "struct", "LF_CLASS": "class", "LF_UNION": "union"}[r["k"]]
        return {"size": size, "kind": kind, "bases": bases, "fields": fields}


def main():
    recs = parse()
    t = T(recs)
    out = {}
    for name, idx in t.byname.items():
        try:
            out[name] = t.layout(idx)
        except Exception as e:
            print("skip", name, e, file=sys.stderr)
    json.dump(out, open(sys.argv[1] + ".json", "w"))
    print("game-test UDTs:", len(out))


if __name__ == "__main__":
    main()
