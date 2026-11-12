"""Lists the literal strings inside IW8 Replay 1.20 .gscbin script files exported by ACTS (research 87).
Read-only on its inputs. Header: 'GSC\\0', then three little-endian uint32 (compressed size, bytecode size,
buffer size: the exact meaning is UNVERIFIED), then a zlib stream; strings are taken from the inflated data.

Usage: py -3 -I tools/warzone_gscbin_strings.py <gscbin file or folder> [--grep word] [--min 4]
Writes nothing; prints '<file>\\t<string>' lines."""
import os, re, sys, zlib, glob


def inflate(path):
    raw = open(path, 'rb').read()
    if raw[:4] != b'GSC\x00':
        return None, raw
    for off in (16, 12, 20, 24):
        try:
            return zlib.decompress(raw[off:]), raw
        except zlib.error:
            continue
    return None, raw


def strings(data, minimum):
    for m in re.finditer(rb'[\x20-\x7e]{%d,}' % minimum, data):
        yield m.group().decode('ascii')


def main():
    args = sys.argv[1:]
    if not args:
        raise SystemExit(__doc__)
    grep = None
    minimum = 4
    if '--grep' in args:
        i = args.index('--grep'); grep = args[i + 1].lower(); del args[i:i + 2]
    if '--min' in args:
        i = args.index('--min'); minimum = int(args[i + 1]); del args[i:i + 2]
    target = args[0]
    files = [target] if os.path.isfile(target) else glob.glob(os.path.join(target, '**', '*.gscbin'), recursive=True)
    for f in sorted(files):
        data, _ = inflate(f)
        if data is None:
            print('%s\t<not inflated>' % f)
            continue
        for s in strings(data, minimum):
            if grep is None or grep in s.lower():
                print('%s\t%s' % (f.replace('\\', '/').split('/assets/scriptfile/')[-1], s))


if __name__ == '__main__':
    main()
