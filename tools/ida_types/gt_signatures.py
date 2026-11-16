"""Struct-pointer arguments of every game-test function, from the atlas index (built from the game-test MAP).

Writes <out>.json: {"by_start": {ea: {"name": n, "args": {reg_index: struct}}}, "by_name": {name: args}}.
reg_index 0..3 = rcx, rdx, r8, r9 (Windows x64); a non-static method's `this` is index 0. by_name keeps only names
whose overloads agree, so Replay functions (named after game-test) can take the same argument types.
"""
import json, re, sqlite3, sys

ATLAS = r"E:\idapluginforcod\atlas\data\atlas.sqlite3"


def split_params(s):
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch in "<(":
            depth += 1
        elif ch in ">)":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur); cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return [p.strip() for p in out]


def struct_of(param):
    p = param.replace("__ptr64", "").strip()
    if not (p.endswith("*") or p.endswith("&")):
        return None
    if p.count("*") + p.count("&") != 1:
        return None                      # pointer to pointer: not a struct base
    p = p[:-1].strip()
    p = re.sub(r"\bconst\b|\bvolatile\b", "", p).strip()
    m = re.match(r"^(?:struct|class|union)\s+(.+)$", p)
    if not m:
        return None
    return m.group(1).strip()


def parse(name, sig):
    if not sig or "(" not in sig:
        return None
    is_method = bool(re.match(r"^(public|protected|private):", sig)) and " static " not in (" " + sig + " ") \
        and "::" in name
    # parameter list: the parentheses right after the function's own name
    short = name.split("::")[-1]
    i = sig.find(short + "(")
    if i < 0:
        i = sig.find("(")
        if i < 0:
            return None
        start = i
    else:
        start = i + len(short)
    depth, j = 0, start
    for j in range(start, len(sig)):
        if sig[j] == "(":
            depth += 1
        elif sig[j] == ")":
            depth -= 1
            if depth == 0:
                break
    params = split_params(sig[start + 1:j])
    if params == ["void"]:
        params = []
    args = {}
    k = 0
    if is_method:
        args[0] = name.rsplit("::", 1)[0]
        k = 1
    for p in params:
        if k > 3:
            break
        st = struct_of(p)
        if st:
            args[k] = st
        k += 1
    return args


def main():
    out = sys.argv[1]
    con = sqlite3.connect("file:%s?mode=ro" % ATLAS, uri=True)
    by_start, names = {}, {}
    for rva, size, name, sig in con.execute("select rva, size, name, signature from symbols where size > 0"):
        args = parse(name, sig)
        if not args:
            continue
        ea = 0x140000000 + rva
        by_start[ea] = {"name": name, "args": args}
        names.setdefault(name, []).append(args)
    by_name = {}
    for n, lst in names.items():
        if all(x == lst[0] for x in lst):
            by_name[n] = lst[0]
    json.dump({"by_start": {str(k): v for k, v in by_start.items()}, "by_name": by_name}, open(out, "w"))
    print("functions with struct-pointer args:", len(by_start), "unambiguous names:", len(by_name))


if __name__ == "__main__":
    main()
