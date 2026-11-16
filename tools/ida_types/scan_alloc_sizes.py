"""Replay struct sizes from allocation sites: `mov ecx, SIZE ; call <allocator> ; ... ; call T::T` (no IDA needed).

For every call to a constructor `T::T`, the immediate loaded into ecx / edx / r8d / r9d (or rcx..r9) just before
the closest preceding call (the allocator, whose rax is the new object) is a size candidate for T. Run on both exes:
a (caller, T) site whose game-test immediate equals the game-test sizeof(T) validates the pattern, and its Replay
immediate is then a proven Replay sizeof(T). Replay sites without a game-test twin count when two or more agree.

  python tools/ida_types/scan_alloc_sizes.py --exe <exe> --functions <functions.json> [--thunks <thunks.json>] --out <out>
"""
import argparse, collections, json, multiprocessing as mp, os, re, sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone import x86_const as X

SIZE_REGS = ("ecx", "edx", "r8d", "r9d", "rcx", "rdx", "r8", "r9")
G = {}


def ctor_class(name):
    if not name or "::" not in name:
        return None
    parts = re.split(r"::(?![^<]*>)", name)
    if len(parts) < 2:
        return None
    last, cls = parts[-1], parts[-2]
    if last == re.sub(r"<.*>$", "", cls):
        return "::".join(parts[:-1])
    return None


def init(exe, names, thunks):
    G["pe"] = pefile.PE(exe, fast_load=True)
    G["base"] = G["pe"].OPTIONAL_HEADER.ImageBase
    G["md"] = Cs(CS_ARCH_X86, CS_MODE_64)
    G["md"].detail = True
    G["names"] = names
    G["thunks"] = thunks


def resolve(ea):
    seen = 0
    while ea in G["thunks"] and seen < 4:
        ea = G["thunks"][ea]
        seen += 1
    return G["names"].get(ea)


def scan(task):
    start, end, name = task
    try:
        code = G["pe"].get_data(start - G["base"], min(end - start, 200000))
    except Exception:
        return None
    win = collections.deque(maxlen=24)
    out = []
    for ins in G["md"].disasm(code, start):
        win.append(ins)
        if ins.mnemonic != "call" or not ins.operands or ins.operands[0].type != X.X86_OP_IMM:
            continue
        cls = ctor_class(resolve(ins.operands[0].imm))
        if not cls:
            continue
        lst = list(win)[:-1]
        # closest preceding call = the allocator
        k = next((i for i in range(len(lst) - 1, -1, -1) if lst[i].mnemonic == "call"), None)
        if k is None or len(lst) - k > 10:
            continue
        # rax of the allocator must reach rcx (directly, or through one register copy)
        tail = lst[k + 1:]
        if not any(t.mnemonic == "mov" and t.op_str in ("rcx, rax", "rbx, rax", "rdi, rax", "rsi, rax") for t in tail) \
                and not any(t.mnemonic == "test" and t.op_str == "rax, rax" for t in tail):
            continue
        imm = None
        for t in reversed(lst[max(0, k - 8):k]):
            if t.mnemonic == "mov" and len(t.operands) == 2 and t.operands[1].type == X.X86_OP_IMM \
                    and t.operands[0].type == X.X86_OP_REG and G["md"].reg_name(t.operands[0].reg) in SIZE_REGS:
                v = t.operands[1].imm
                if 8 <= v < (1 << 31):
                    imm = v
                    break
        if imm:
            out.append((cls, imm))
    return (name, out) if out else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--functions", required=True)
    ap.add_argument("--thunks", default=None)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    funcs = json.load(open(a.functions))
    names = {s: n for s, e, n in funcs}
    thunks = {int(k): v for k, v in json.load(open(a.thunks)).items()} if a.thunks else {}
    res = {}
    with mp.Pool(max(1, os.cpu_count() - 2), initializer=init, initargs=(a.exe, names, thunks)) as pool:
        for r in pool.imap_unordered(scan, funcs, chunksize=256):
            if r:
                res.setdefault(r[0], []).extend(r[1])
    json.dump(res, open(a.out + ".json", "w"))
    print("callers with allocation + constructor sites:", len(res), file=sys.stderr)


if __name__ == "__main__":
    main()
