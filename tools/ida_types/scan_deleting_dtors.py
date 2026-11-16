"""sizeof(T) from T's deleting destructor (C++14 sized delete): `mov edx, SIZE ; ... ; call operator delete(p, size)`.

MSVC's scalar / vector deleting destructor of a polymorphic class calls the destructor and then the sized
deallocation with the object size in edx. The deallocation function is found empirically: the call target that most
often follows a `mov edx, imm` inside the 2,500 named deleting destructors. No IDA needed.

  python tools/ida_types/scan_deleting_dtors.py --exe <replay exe> --functions out/ida_types/scan/functions.json
      --out out/ida_types/scan/dtor_sizes
"""
import argparse, collections, json, re
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone import x86_const as X

PAT = re.compile(r"^(.*?)(?:::|_)(?:scalar|vector)_deleting_dtor(?:_\d+)?$")
PAT_GT = re.compile(r"^(.*)::`(?:scalar|vector) deleting destructor'$")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--functions", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--thunks", default=None)
    a = ap.parse_args()
    thunks = {int(k): v for k, v in json.load(open(a.thunks)).items()} if a.thunks else {}
    pe = pefile.PE(a.exe, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    funcs = json.load(open(a.functions))
    names = {s: n for s, e, n in funcs}
    sites = []
    for s, e, n in funcs:
        m = PAT.match(n) or PAT_GT.match(n)
        if not m or "vt14" in n:          # names with an embedded address/size came from an earlier tool
            continue
        cls = m.group(1)
        try:
            code = pe.get_data(s - base, min(e - s, 4000))
        except Exception:
            continue
        last_edx = None
        for ins in md.disasm(code, s):
            ops = ins.operands
            if ins.mnemonic == "mov" and len(ops) == 2 and ops[0].type == X.X86_OP_REG \
                    and md.reg_name(ops[0].reg) in ("edx", "rdx") and ops[1].type == X.X86_OP_IMM:
                last_edx = ops[1].imm
            elif ins.mnemonic in ("call", "jmp") and ops and ops[0].type == X.X86_OP_IMM:
                if last_edx is not None:
                    t = ops[0].imm
                    for _ in range(4):
                        t = thunks.get(t, t)
                    sites.append((cls, n, s, t, last_edx))
                last_edx = None
            elif ins.mnemonic == "ret":
                break
    # the sized-delete function: most common target after mov edx, imm
    tgt = collections.Counter(t for c, n, s, t, v in sites)
    deleters = [t for t, k in tgt.most_common(4)]
    print("deallocation targets:", [(hex(t), names.get(t), k) for t, k in tgt.most_common(4)])
    out = collections.defaultdict(collections.Counter)
    for c, n, s, t, v in sites:
        if t in deleters and 1 <= v < (1 << 31):
            out[c][v] += 1
    res = {}
    for c, cnt in out.items():
        (v, k), = cnt.most_common(1)
        res[c] = {"size": v, "agree": k, "all": dict(cnt)}
    json.dump(res, open(a.out + ".json", "w"), indent=0)
    print("classes with a sized delete:", len(res))


if __name__ == "__main__":
    main()
