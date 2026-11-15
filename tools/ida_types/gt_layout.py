"""Game-test (older Xbox build) reference lookups for the IDA local-type repair.

The game-test PDB and its decompile are a GUIDE for names, field order and which functions touch a field.
They are NOT evidence for Replay offsets or sizes: only Replay's own code is.

  python tools/ida_types/gt_layout.py TYPE                 layout of TYPE in the game-test PDB
  python tools/ida_types/gt_layout.py TYPE --uses FIELD    game-test functions that read/write FIELD (->FIELD / .FIELD)
  python tools/ida_types/gt_layout.py TYPE --funcs         game-test functions that mention TYPE most
  python tools/ida_types/gt_layout.py --global NAME        type of a game-test global (for arrays such as g_entities)

Reads E:\\1-game-test\\1-game_test.pdb through the owner's read-only DIA helper (pdb-layout.exe) and greps the
cleaned game-test decompile. Never touches IDA.
"""
import argparse, collections, json, os, re, subprocess, sys

PDB = r"E:\1-game-test\1-game_test.pdb"
PDB_LAYOUT = r"E:\1-game-test\tools\gametest-workbench\pdb-layout.exe"
SRC = r"E:\idapluginforcod\sources\cleaned\code_source\src"
HDR = re.compile(r"/\*\n=+\n(.+?)\n=+\n\*/")


def run_layout(name, extra=None):
    cmd = [PDB_LAYOUT, PDB, name] + ([extra] if extra else [])
    r = subprocess.run(cmd, capture_output=True, timeout=30)
    if r.returncode or not r.stdout:
        return None
    return json.loads(r.stdout)


def show_type(name):
    d = run_layout(name)
    types = [t for t in (d or {}).get("types", []) if t["name"] == name]
    if not types:
        print(f"{name}: no UDT with this exact name in the game-test PDB (try the C++ spelling, e.g. A::B or A<B>)")
        return 1
    t = types[0]
    print(f"# game-test {name}: size {t['size']} (0x{t['size']:X}), {len(t['fields'])} fields"
          + ("  [partial: bitfields, a vtable or virtual bases are not listed; base-class fields are flattened in]"
             if t.get("partial") else ""))
    print("# older Xbox build: use for names and order only; Replay offsets come from Replay code")
    for f in t["fields"]:
        ty = f["type"] or {14: "<pointer>", 15: "<array>", 16: "<basic>"}.get(f.get("tag"), "")
        print(f"  +0x{f['offset']:04X} {f['size']:>7}  {f['name']:<36} {ty}")
    return 0


def iter_functions():
    for dp, dn, fs in os.walk(SRC):
        for fn in fs:
            if not fn.endswith((".cpp", ".c", ".h", ".inl")):
                continue
            path = os.path.join(dp, fn)
            try:
                s = open(path, encoding="utf-8", errors="ignore").read()
            except OSError:
                continue
            pos = [(m.start(), m.group(1).strip()) for m in HDR.finditer(s)]
            for i, (st, name) in enumerate(pos):
                end = pos[i + 1][0] if i + 1 < len(pos) else len(s)
                yield path, s.count("\n", 0, st) + 1, name, s[st:end]


def uses(typ, field, limit):
    rx = re.compile(r"(->|\.)" + re.escape(field) + r"\b")
    hits = []
    for path, line, name, body in iter_functions():
        n = len(rx.findall(body))
        if n:
            hits.append((typ in body, n, name, os.path.relpath(path, SRC), line,
                         next((l.strip() for l in body.splitlines() if rx.search(l)), "")))
    hits.sort(key=lambda h: (not h[0], -h[1]))
    print(f"# game-test functions using .{field} / ->{field} (functions that also mention {typ} first)")
    for mentions, n, name, rel, line, sample in hits[:limit]:
        print(f"  {name:<60} x{n:<3} {'' if mentions else '(no ' + typ + ') '}{rel}:{line}")
        print(f"      {sample[:150]}")
    if not hits:
        print("  none")


def funcs(typ, limit):
    rx = re.compile(r"\b" + re.escape(typ) + r"\b")
    c = collections.Counter()
    for path, line, name, body in iter_functions():
        n = len(rx.findall(body))
        if n:
            c[name] += n
    print(f"# game-test functions mentioning {typ} most")
    for name, n in c.most_common(limit):
        print(f"  {name:<70} x{n}")


def global_info(name):
    d = run_layout(name, "--globals")
    for g in (d or {}).get("globals", []):
        print(json.dumps(g))
    if not d or not d.get("globals"):
        print(f"{name}: no game-test global with this name")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("type", nargs="?")
    ap.add_argument("--uses", metavar="FIELD")
    ap.add_argument("--funcs", action="store_true")
    ap.add_argument("--global", dest="glob", metavar="NAME")
    ap.add_argument("--limit", type=int, default=15)
    a = ap.parse_args()
    if a.glob:
        return global_info(a.glob)
    if not a.type:
        ap.print_help(); return 2
    if a.uses:
        return uses(a.type, a.uses, a.limit)
    if a.funcs:
        return funcs(a.type, a.limit)
    return show_type(a.type)


if __name__ == "__main__":
    sys.exit(main() or 0)
