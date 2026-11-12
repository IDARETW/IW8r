"""Read-only disassembly of a function of the recompiled game exe, by symbol name, with the symbol map beside it.

The owner's game folder holds game_dx12_ship_replay_dw.exe and its linker map (function and data names). This prints
the instructions of one function with call / jump / data targets named from the map and string literals quoted, so
engine behaviour can be read offline. Nothing is written; the game is not started.

Usage: py -3 -I tools/dw_disasm.py <symbol substring or 0xADDRESS> [--max <bytes>] [--list] [--exe <file>]
       --list prints the matching symbol names only."""
import bisect, os, re, struct, sys

GAME = r'E:\IW8\Builds\1.20-replay'
EXE = os.path.join(GAME, 'game_dx12_ship_replay_dw.exe')


class Image:
    def __init__(self, exe):
        self.data = open(exe, 'rb').read()
        pe = struct.unpack_from('<I', self.data, 0x3c)[0]
        count = struct.unpack_from('<H', self.data, pe + 6)[0]
        opt = struct.unpack_from('<H', self.data, pe + 20)[0]
        self.base = struct.unpack_from('<Q', self.data, pe + 24 + 24)[0]
        self.sections = []
        for i in range(count):
            o = pe + 24 + opt + i * 40
            vsize, va, rsize, raw = struct.unpack_from('<IIII', self.data, o + 8)
            self.sections.append((va, max(vsize, rsize), raw, rsize, self.data[o:o + 8].rstrip(b'\0').decode('latin1')))

    def offset(self, va):
        rva = va - self.base
        for sva, size, raw, rsize, _ in self.sections:
            if sva <= rva < sva + size and rva - sva < rsize:
                return raw + rva - sva
        return None

    def read(self, va, size):
        o = self.offset(va)
        return self.data[o:o + size] if o is not None else b''

    def string(self, va):
        o = self.offset(va)
        if o is None:
            return None
        end = self.data.find(b'\0', o, o + 200)
        s = self.data[o:end] if end > 0 else b''
        if 3 <= len(s) and all(32 <= c < 127 for c in s):
            return s.decode('latin1')
        return None


def load_map(path):
    names, addrs = {}, []
    pat = re.compile(r'^\s*[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+(\S+)\s+([0-9A-Fa-f]{16})\s')
    with open(path, 'r', errors='replace') as f:
        for line in f:
            m = pat.match(line)
            if m:
                va = int(m.group(2), 16)
                if va and va not in names:
                    names[va] = m.group(1)
    addrs = sorted(names)
    return names, addrs


def main():
    args = sys.argv[1:]
    exe, limit, listing = EXE, 0x1400, False
    if '--exe' in args:
        i = args.index('--exe'); exe = args[i + 1]; del args[i:i + 2]
    if '--max' in args:
        i = args.index('--max'); limit = int(args[i + 1], 0); del args[i:i + 2]
    if '--list' in args:
        listing = True; args.remove('--list')
    names, addrs = load_map(os.path.splitext(exe)[0] + '.map')
    want = args[0]
    if want.lower().startswith('0x'):
        start = int(want, 16)
    else:
        hits = [(va, n) for va, n in names.items() if want.lower() in n.lower()]
        exact = [h for h in hits if h[1].lower() == want.lower()]
        if listing or (len(hits) != 1 and not exact):
            for va, n in sorted(hits)[:200]:
                print('%016X %s' % (va, n))
            return 0 if hits else 1
        start = (exact or hits)[0][0]
    image = Image(exe)
    i = bisect.bisect_right(addrs, start)
    end = addrs[i] if i < len(addrs) else start + limit
    size = min(end - start, limit)
    code = image.read(start, size)
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    print('; %s  %016X  %d bytes%s' % (names.get(start, '?'), start, size, ' (cut)' if end - start > limit else ''))

    def label(va):
        if va in names:
            return names[va]
        j = bisect.bisect_right(addrs, va) - 1
        if j >= 0 and va - addrs[j] < 0x4000 and start <= va < start + size:
            return None
        return None

    for ins in md.disasm(code, start):
        note = ''
        for op in ins.operands:
            target = None
            if op.type == capstone.x86.X86_OP_IMM and ins.group(capstone.CS_GRP_CALL) or (op.type == capstone.x86.X86_OP_IMM and ins.group(capstone.CS_GRP_JUMP)):
                target = op.imm
            elif op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                target = ins.address + ins.size + op.mem.disp
            if target is None:
                continue
            if start <= target < start + size and ins.group(capstone.CS_GRP_JUMP):
                continue
            n = names.get(target)
            s = image.string(target) if not ins.group(capstone.CS_GRP_CALL) and not ins.group(capstone.CS_GRP_JUMP) else None
            if n and s:
                note = ' ; %s "%s"' % (n, s)
            elif n:
                note = ' ; ' + n
            elif s:
                note = ' ; "%s"' % s
            else:
                note = ' ; %X' % target
        print('%X  %-7s %s%s' % (ins.address, ins.mnemonic, ins.op_str, note))
    return 0


if __name__ == '__main__':
    sys.exit(main())
