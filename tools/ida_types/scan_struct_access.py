"""Collect every offset Replay code reads or writes through a struct pointer, straight from the exe (no IDA needed).

Built for cg_t, which the game only reaches through the global pointer table LocalClientGlobals[localClientNum]
(and `this` in cg_t:: / CgGlobalsMP:: / CgGlobalsSP:: methods), but it works for any struct reached that way.

Sources of a tracked pointer:
  * a qword load from the global table (rip-relative, or ImageBase/table register + index*8),
  * `this` (rcx) at the entry of functions whose name starts with one of --this-prefix,
  * argument registers of direct callees that a tracked pointer was passed to (repeated --depth times).
Propagation: register copies, lea/add/sub with a constant (tracks pointers into the struct, e.g. &cg->member),
spills to and reloads from [rsp+X]. Calls clobber rax, rcx, rdx, r8-r11. Linear sweep (no CFG), so rare offsets
can be noise: trust offsets seen in several functions or with consistent widths.

  python tools/ida_types/scan_struct_access.py --exe <exe> --functions out/ida_types/scan/functions.json
      --table LocalClientGlobals=0x14F26F940 --this-prefix cg_t:: CgGlobalsMP:: CgGlobalsSP:: --max-size 677040
      --out out/ida_types/scan/cg_t
Writes <out>.json (offset -> widths, kinds, functions, sample instructions) and <out>.txt (sorted table).
"""
import argparse, bisect, collections, json, sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone import x86_const as X

R64 = {}
_fam = {
    "rax": ["rax", "eax", "ax", "al", "ah"], "rbx": ["rbx", "ebx", "bx", "bl", "bh"],
    "rcx": ["rcx", "ecx", "cx", "cl", "ch"], "rdx": ["rdx", "edx", "dx", "dl", "dh"],
    "rsi": ["rsi", "esi", "si", "sil"], "rdi": ["rdi", "edi", "di", "dil"],
    "rbp": ["rbp", "ebp", "bp", "bpl"], "rsp": ["rsp", "esp", "sp", "spl"],
}
for i in range(8, 16):
    _fam["r%d" % i] = ["r%d" % i, "r%dd" % i, "r%dw" % i, "r%db" % i]
_md0 = Cs(CS_ARCH_X86, CS_MODE_64)
_name2id = {}
for rid in range(1, X.X86_REG_ENDING):
    n = _md0.reg_name(rid)
    if n:
        _name2id[n] = rid
for parent, kids in _fam.items():
    for k in kids:
        if k in _name2id:
            R64[_name2id[k]] = _name2id[parent]
RCX, RDX, R8, R9 = (_name2id[n] for n in ("rcx", "rdx", "r8", "r9"))
ARGS = [RCX, RDX, R8, R9]
VOLATILE = [_name2id[n] for n in ("rax", "rcx", "rdx", "r8", "r9", "r10", "r11")]
RSP, RIP = _name2id["rsp"], X.X86_REG_RIP
WRITE_MN = {"mov", "movss", "movsd", "movaps", "movups", "movdqa", "movdqu", "movq", "movd", "movsx", "movzx",
            "movsxd", "movabs", "movhps", "movlps", "movnti", "stosb", "stosd", "stosq", "vmovss", "vmovsd",
            "vmovaps", "vmovups", "vmovdqa", "vmovdqu", "vmovq", "vmovd", "cmovne", "cmove", "setne", "sete",
            "seta", "setb", "setg", "setl", "setae", "setbe", "setge", "setle"}
RMW_MN = {"add", "sub", "or", "and", "xor", "inc", "dec", "neg", "not", "shl", "shr", "sar", "rol", "ror", "bts",
          "btr", "btc", "xadd", "cmpxchg", "lock add", "adc", "sbb"}
SKIP_CALLEE = ("memset", "memcpy", "memmove", "j_", "Mem_", "Hunk_", "Sys_", "Com_Print", "Com_Error", "va",
               "__security", "_guard", "Assert", "strlen", "strcpy", "I_str", "Q_str")


def parent(r):
    return R64.get(r, r)


class Image:
    def __init__(self, path):
        self.pe = pefile.PE(path, fast_load=True)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase

    def read(self, ea, size):
        return self.pe.get_data(ea - self.base, size)


FUNCACC = collections.defaultdict(set)


RETURNS = set()   # function starts whose return value (rax) is the tracked pointer
THUNKS = {}       # incremental-link stub -> real function (game-test build)


def resolve(ea):
    for _ in range(8):
        if ea in THUNKS:
            ea = THUNKS[ea]
        else:
            break
    return ea


def scan_function(md, img, start, end, entry, tables, out, calls, maxsize, limit=200000, level=0):
    """entry: {reg: off}. Records accesses into out[off]; records tracked args of direct calls into calls."""
    try:
        code = img.read(start, end - start)
    except Exception:
        return
    st = dict((r, ("obj", o)) for r, o in entry.items())
    stack = {}
    n = 0
    for ins in md.disasm(code, start):
        n += 1
        if n > limit:
            break
        mn = ins.mnemonic
        ops = ins.operands
        # 1. memory operands through a tracked base
        for i, op in enumerate(ops):
            if op.type != X.X86_OP_MEM:
                continue
            b = parent(op.mem.base)
            t = st.get(b)
            if not t or t[0] != "obj":
                continue
            off = t[1] + op.mem.disp
            if off < 0 or off >= maxsize:
                continue
            if mn == "lea":
                kind = "addr"
            elif i == 0 and len(ops) >= 1 and (mn in WRITE_MN or mn in RMW_MN) and (len(ops) == 2 or mn in RMW_MN):
                kind = "rmw" if mn in RMW_MN else "w"
            else:
                kind = "r"
            rec = out[off]
            rec["n"] += 1
            rec["lv"][level] += 1
            rec["fset"].add(start)
            FUNCACC[start].add((ins.address, off, op.size, kind, level))
            if level == 0:
                rec["fset0"].add(start)
            rec["sizes"][op.size] += 1
            rec["kinds"][kind] += 1
            if op.mem.index:
                rec["indexed"][op.mem.scale] += 1
            if len(rec["funcs"]) < 12:
                rec["funcs"].setdefault(start, "%x %s %s" % (ins.address, mn, ins.op_str))
        # 2. taint updates
        dst = ops[0] if ops else None
        if mn == "call":
            if ops and ops[0].type == X.X86_OP_IMM:
                tgt = resolve(ops[0].imm)
                args = {}
                for k, r in enumerate(ARGS):
                    t = st.get(r)
                    if t and t[0] == "obj":
                        args[k] = t[1]
                if args:
                    calls[tgt].append(args)
            for r in VOLATILE:
                st.pop(r, None)
            if ops and ops[0].type == X.X86_OP_IMM and resolve(ops[0].imm) in RETURNS:
                st[_name2id["rax"]] = ("obj", 0)
            continue
        if mn == "jmp" and ops and ops[0].type == X.X86_OP_IMM and not (start <= ops[0].imm < end):
            args = {k: st[r][1] for k, r in enumerate(ARGS) if st.get(r, ("", 0))[0] == "obj"}
            if args:
                calls[resolve(ops[0].imm)].append(args)
            continue
        if dst is None or len(ops) < 1:
            continue
        if dst.type == X.X86_OP_MEM:
            # spill: mov [rsp+d], reg
            if mn == "mov" and parent(dst.mem.base) == RSP and not dst.mem.index and len(ops) == 2 \
                    and ops[1].type == X.X86_OP_REG and dst.size == 8:
                t = st.get(parent(ops[1].reg))
                if t:
                    stack[dst.mem.disp] = t
                else:
                    stack.pop(dst.mem.disp, None)
            continue
        if dst.type != X.X86_OP_REG:
            continue
        d = parent(dst.reg)
        new = None
        if len(ops) == 2:
            src = ops[1]
            if mn == "mov" and src.type == X.X86_OP_REG and dst.size == 8:
                new = st.get(parent(src.reg))
            elif mn == "mov" and src.type == X.X86_OP_MEM and dst.size == 8:
                m = src.mem
                if m.base == RIP:
                    tgt = ins.address + ins.size + m.disp
                    for tea in tables:
                        if tea <= tgt < tea + 64 and (tgt - tea) % 8 == 0:
                            new = ("obj", 0)
                elif parent(m.base) == RSP and not m.index:
                    new = stack.get(m.disp)
                else:
                    bt = st.get(parent(m.base))
                    if bt and m.index and m.scale == 8:
                        if bt[0] == "table" and m.disp == 0:
                            new = ("obj", 0)
                        elif bt[0] == "imgbase" and any(m.disp == tea - img.base for tea in tables):
                            new = ("obj", 0)
            elif mn == "lea" and src.type == X.X86_OP_MEM:
                m = src.mem
                if m.base == RIP:
                    tgt = ins.address + ins.size + m.disp
                    if tgt in tables:
                        new = ("table",)
                    elif tgt == img.base:
                        new = ("imgbase",)
                else:
                    bt = st.get(parent(m.base))
                    if bt and bt[0] == "obj" and not m.index:
                        new = ("obj", bt[1] + m.disp)
            elif mn in ("add", "sub") and src.type == X.X86_OP_IMM and dst.size == 8:
                t = st.get(d)
                if t and t[0] == "obj":
                    new = ("obj", t[1] + (src.imm if mn == "add" else -src.imm))
        if new:
            st[d] = new
        else:
            st.pop(d, None)
        # other registers written implicitly (e.g. xchg, div) are rare; ignore


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--functions", required=True)
    ap.add_argument("--table", action="append", default=[], help="NAME=0xEA of a global pointer table")
    ap.add_argument("--this-prefix", nargs="*", default=[])
    ap.add_argument("--returns", nargs="*", default=[], help="functions whose return value is the tracked pointer")
    ap.add_argument("--thunks", default=None, help="JSON {stub_ea: target_ea} for incremental-link stubs")
    ap.add_argument("--max-size", type=lambda s: int(s, 0), required=True)
    ap.add_argument("--depth", type=int, default=3)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    img = Image(a.exe)
    funcs = json.load(open(a.functions))
    funcs.sort()
    starts = [f[0] for f in funcs]
    byname = {}
    for s, e, n in funcs:
        byname[s] = (e, n)
    tables = [int(t.split("=")[1], 0) for t in a.table]
    if a.thunks:
        THUNKS.update({int(k): v for k, v in json.load(open(a.thunks)).items()})
    for s0, e0, n0 in funcs:
        if n0 in a.returns:
            RETURNS.add(s0)
    print("return-value sources:", len(RETURNS), file=sys.stderr)
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    out = collections.defaultdict(lambda: {"n": 0, "sizes": collections.Counter(), "kinds": collections.Counter(),
                                           "indexed": collections.Counter(), "funcs": {}, "lv": collections.Counter(),
                                           "fset": set(), "fset0": set()})
    # pass 0: every function (cheap pre-filter: only functions that mention the table or are methods)
    todo = {}
    tbl_bytes = []
    for s, e, n in funcs:
        if any(n.startswith(p) for p in a.this_prefix):
            todo[s] = [{0: 0}]
    # functions referencing the tables: find rip-relative references by scanning code once
    print("finding table users...", file=sys.stderr)
    for s, e, n in funcs:
        try:
            code = img.read(s, e - s)
        except Exception:
            continue
        # quick byte test: disp32 = table - (ip+len) varies, so test with a light disasm only if small enough
        if e - s > 400000:
            continue
        md_lite = Cs(CS_ARCH_X86, CS_MODE_64)
        md_lite.detail = False
        for ins in md_lite.disasm(code, s):
            if "rip" in ins.op_str:
                # parse rip+disp
                try:
                    disp = int(ins.op_str.split("rip")[1].split("]")[0].replace(" ", "").replace("+", ""), 16) \
                        if "+ 0x" in ins.op_str or "- 0x" in ins.op_str else None
                except ValueError:
                    disp = None
                if disp is not None:
                    if "- 0x" in ins.op_str:
                        disp = -disp
                    tgt = ins.address + ins.size + disp
                    if any(t <= tgt < t + 64 for t in tables):
                        todo.setdefault(s, []).append({})
                        break
            elif RETURNS and ins.mnemonic == "call" and ins.op_str.startswith("0x"):
                try:
                    if resolve(int(ins.op_str, 16)) in RETURNS:
                        todo.setdefault(s, []).append({})
                        break
                except ValueError:
                    pass
    print("sources:", len(todo), file=sys.stderr)
    seen = set()
    level = 0
    frontier = todo
    while frontier and level <= a.depth:
        calls = collections.defaultdict(list)
        for s, entries in frontier.items():
            e, n = byname.get(s, (None, None))
            if e is None:
                continue
            merged = {}
            for ent in entries:
                for k, off in ent.items():
                    merged[ARGS[k] if isinstance(k, int) and k < 4 else k] = off
            key = (s, tuple(sorted(merged.items())))
            if key in seen:
                continue
            seen.add(key)
            scan_function(md, img, s, e, merged, tables, out, calls, a.max_size, level=level)
        nxt = {}
        for tgt, lst in calls.items():
            if tgt not in byname:
                continue
            nm = byname[tgt][1]
            if nm.startswith(SKIP_CALLEE):
                continue
            nxt.setdefault(tgt, []).extend(lst[:1])
        frontier = nxt
        level += 1
        print("level", level, "next", len(frontier), "offsets", len(out), file=sys.stderr)
    # write
    res = {}
    for off in sorted(out):
        r = out[off]
        res[off] = {"n": r["n"], "sizes": dict(r["sizes"]), "kinds": dict(r["kinds"]), "indexed": dict(r["indexed"]),
                    "nfuncs": len(r["fset"]), "nfuncs_direct": len(r["fset0"]), "levels": dict(r["lv"]),
                    "samples": [(byname.get(f, (0, "?"))[1], s) for f, s in list(r["funcs"].items())[:6]]}
    json.dump(res, open(a.out + ".json", "w"), indent=0)
    json.dump({str(f): sorted(v) for f, v in FUNCACC.items()}, open(a.out + ".funcs.json", "w"))
    with open(a.out + ".txt", "w", encoding="utf-8") as o:
        o.write("# offset  hits  funcs  widths  kinds  indexed  samples\n")
        for off, r in res.items():
            o.write("+0x%06X %6d %4d %-14s %-22s %-8s %s\n" % (off, r["n"], r["nfuncs"],
                    ",".join("%s:%d" % kv for kv in sorted(r["sizes"].items())),
                    ",".join("%s:%d" % kv for kv in sorted(r["kinds"].items())),
                    ",".join("x%s" % k for k in r["indexed"]) or "-",
                    " | ".join("%s @%s" % (fn, smp) for fn, smp in r["samples"][:2])))
    print("offsets:", len(res), "written to", a.out + ".json/.txt", file=sys.stderr)


if __name__ == "__main__":
    main()
