"""Scan an exe for accesses through EVERY struct-pointer argument at once (no IDA needed).

Each function's struct-pointer arguments (from gt_signatures.py: by start for game-test, by name for Replay) seed
typed pointers in rcx/rdx/r8/r9. Propagation: register copies, lea/add/sub with a constant (pointers into the struct),
spills to [rsp+X]. Calls clobber volatile registers. Recorded per function, in instruction order:
  acc: [addr, struct, offset, width, kind]   kind = r / w / rmw / addr
  k:   [addr, op, value]                     immediates of imul / mov / add / sub / cmp (>= 16), for size recovery
Output: <out>.jsonl, one function per line. Runs on all CPU cores.

  python tools/ida_types/scan_types.py --exe <exe> --functions <functions.json> --sigs out/ida_types/scan/sigs_gt.json
      --by name|start [--thunks <thunks.json>] --out out/ida_types/scan/replay_types
"""
import argparse, json, multiprocessing as mp, os, sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone import x86_const as X

_md0 = Cs(CS_ARCH_X86, CS_MODE_64)
NAME2ID = {}
for rid in range(1, X.X86_REG_ENDING):
    n = _md0.reg_name(rid)
    if n:
        NAME2ID[n] = rid
FAM = {"rax": "eax ax al ah", "rbx": "ebx bx bl bh", "rcx": "ecx cx cl ch", "rdx": "edx dx dl dh",
       "rsi": "esi si sil", "rdi": "edi di dil", "rbp": "ebp bp bpl", "rsp": "esp sp spl"}
for i in range(8, 16):
    FAM["r%d" % i] = "r%dd r%dw r%db" % (i, i, i)
PARENT = {}
for p, kids in FAM.items():
    PARENT[NAME2ID[p]] = NAME2ID[p]
    for k in kids.split():
        if k in NAME2ID:
            PARENT[NAME2ID[k]] = NAME2ID[p]
ARGS = [NAME2ID[n] for n in ("rcx", "rdx", "r8", "r9")]
VOLATILE = [NAME2ID[n] for n in ("rax", "rcx", "rdx", "r8", "r9", "r10", "r11")]
RSP, RIP = NAME2ID["rsp"], X.X86_REG_RIP
WRITE_MN = {"mov", "movss", "movsd", "movaps", "movups", "movdqa", "movdqu", "movq", "movd", "movabs", "movhps",
            "movlps", "vmovss", "vmovsd", "vmovaps", "vmovups", "vmovdqa", "vmovdqu", "vmovq", "vmovd"}
RMW_MN = {"add", "sub", "or", "and", "xor", "inc", "dec", "neg", "not", "shl", "shr", "sar", "bts", "btr", "btc"}
CONST_MN = {"imul", "mov", "add", "sub", "cmp"}

G = {}


def init(exe, thunks):
    G["pe"] = pefile.PE(exe, fast_load=True)
    G["base"] = G["pe"].OPTIONAL_HEADER.ImageBase
    G["md"] = Cs(CS_ARCH_X86, CS_MODE_64)
    G["md"].detail = True
    G["thunks"] = thunks


def par(r):
    return PARENT.get(r, r)


def scan(task):
    start, end, args = task
    try:
        code = G["pe"].get_data(start - G["base"], min(end - start, 300000))
    except Exception:
        return None
    st = {ARGS[int(k)]: (s, 0) for k, s in args.items()}
    stack = {}
    acc, ks = [], []
    n = 0
    for ins in G["md"].disasm(code, start):
        n += 1
        if n > 60000:
            break
        mn = ins.mnemonic
        ops = ins.operands
        for i, op in enumerate(ops):
            if op.type != X.X86_OP_MEM:
                continue
            t = st.get(par(op.mem.base))
            if not t:
                continue
            off = t[1] + op.mem.disp
            if off < 0 or off > 50000000:
                continue
            if mn == "lea":
                kind = "addr"
            elif i == 0 and mn in RMW_MN:
                kind = "rmw"
            elif i == 0 and mn in WRITE_MN and len(ops) == 2:
                kind = "w"
            else:
                kind = "r"
            acc.append((ins.address, t[0], off, op.size, kind))
        if mn in CONST_MN and ops:
            for op in ops[1:]:
                if op.type == X.X86_OP_IMM and 16 <= op.imm < (1 << 31):
                    if not (mn in ("add", "sub") and ops[0].type == X.X86_OP_REG and par(ops[0].reg) == RSP):
                        ks.append((ins.address, mn, op.imm))
        if mn == "call":
            for r in VOLATILE:
                st.pop(r, None)
            continue
        if mn in ("ret", "jmp") and ops and ops[0].type == X.X86_OP_IMM and not (start <= ops[0].imm < end):
            continue
        if not ops:
            continue
        dst = ops[0]
        if dst.type == X.X86_OP_MEM:
            if mn == "mov" and par(dst.mem.base) == RSP and not dst.mem.index and len(ops) == 2 \
                    and ops[1].type == X.X86_OP_REG and dst.size == 8:
                t = st.get(par(ops[1].reg))
                if t:
                    stack[dst.mem.disp] = t
                else:
                    stack.pop(dst.mem.disp, None)
            continue
        if dst.type != X.X86_OP_REG:
            continue
        d = par(dst.reg)
        new = None
        if len(ops) == 2:
            src = ops[1]
            if mn == "mov" and dst.size == 8 and src.type == X.X86_OP_REG:
                new = st.get(par(src.reg))
            elif mn == "mov" and dst.size == 8 and src.type == X.X86_OP_MEM and par(src.mem.base) == RSP \
                    and not src.mem.index:
                new = stack.get(src.mem.disp)
            elif mn == "lea" and src.type == X.X86_OP_MEM and not src.mem.index:
                bt = st.get(par(src.mem.base))
                if bt:
                    new = (bt[0], bt[1] + src.mem.disp)
            elif mn in ("add", "sub") and dst.size == 8 and src.type == X.X86_OP_IMM:
                t = st.get(d)
                if t:
                    new = (t[0], t[1] + (src.imm if mn == "add" else -src.imm))
        if new:
            st[d] = new
        else:
            st.pop(d, None)
    if not acc and not ks:
        return None
    return {"s": start, "acc": acc, "k": ks}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--functions", required=True)
    ap.add_argument("--sigs", required=True)
    ap.add_argument("--by", choices=["name", "start"], required=True)
    ap.add_argument("--thunks", default=None)
    ap.add_argument("--out", required=True)
    ap.add_argument("--procs", type=int, default=max(1, os.cpu_count() - 2))
    a = ap.parse_args()
    sigs = json.load(open(a.sigs))
    funcs = json.load(open(a.functions))
    tasks = []
    if a.by == "start":
        bs = sigs["by_start"]
        for s, e, n in funcs:
            x = bs.get(str(s))
            if x:
                tasks.append((s, e, x["args"]))
    else:
        bn = sigs["by_name"]
        for s, e, n in funcs:
            x = bn.get(n)
            if x:
                tasks.append((s, e, x))
    print("functions to scan:", len(tasks), file=sys.stderr)
    thunks = {int(k): v for k, v in json.load(open(a.thunks)).items()} if a.thunks else {}
    done = 0
    with mp.Pool(a.procs, initializer=init, initargs=(a.exe, thunks)) as pool, open(a.out + ".jsonl", "w") as o:
        for r in pool.imap_unordered(scan, tasks, chunksize=64):
            done += 1
            if r:
                o.write(json.dumps(r) + "\n")
            if done % 10000 == 0:
                print("  scanned", done, file=sys.stderr)
    print("done", done, file=sys.stderr)


if __name__ == "__main__":
    main()
