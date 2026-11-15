"""IDAPython helpers for the local-type repair. Load inside mcp__ida__idascript with:

    exec(open(r"E:\\IW8-Radiant\\tools\\ida_types\\ida_fill_pad.py", encoding="utf-8").read())

then call ONE of:

    show("gentity_s", 0x290, 0x2B0)                 members overlapping [lo, hi)
    decl("gentity_s")                               the full C declaration (save it before you change anything)
    fill_pad("gentity_s", [(0x298, "missileTargetEnt", "EntHandle")], lo=0, hi=0x5B0)          dry run
    fill_pad("gentity_s", [(0x298, "missileTargetEnt", "EntHandle")], lo=0, hi=0x5B0, apply=True)
    backup_all(r"E:\\IW8-Radiant\\out\\ida_types\\backup\\local_types_<stamp>.h")             whole type library as C

fill_pad only places NEW members inside existing _padXXX members. It never changes the struct size, never moves an
existing member, never changes a member's width, and keeps the type's ordinal (so every use of the type updates).
It refuses (and changes nothing) when a new field is not wholly inside one pad, is outside [lo, hi), overlaps
another new field, reuses an existing member name, or when the rebuilt struct would differ in size or offsets.
All these structs are pack(1) and not fixed-layout: a width change anywhere shifts every later field silently.
"""
import re
import ida_typeinf

_PAD = re.compile(r"^(_pad|pad|gap)[0-9A-Fa-f_]*$", re.I)


def _get(name):
    til = ida_typeinf.get_idati()
    t = ida_typeinf.tinfo_t()
    if not t.get_named_type(til, name) or not t.is_udt():
        raise ValueError("no struct named %r in Local Types" % name)
    u = ida_typeinf.udt_type_data_t()
    if not t.get_udt_details(u):
        raise ValueError("cannot read members of %r" % name)
    if u.is_union:
        raise ValueError("%r is a union; edit unions by hand" % name)
    return til, t, u


def _ctype(text):
    t = ida_typeinf.tinfo_t()
    til = ida_typeinf.get_idati()
    decl = text.strip()
    if not decl.endswith(";"):
        decl += " __iw8r_tmp;"
    if ida_typeinf.parse_decl(t, til, decl, ida_typeinf.PT_SIL) is None:
        raise ValueError("cannot parse C type %r" % text)
    return t


def _pad_member(off, size):
    m = ida_typeinf.udm_t()
    m.name = "_pad%X" % off
    arr = ida_typeinf.tinfo_t()
    arr.create_array(ida_typeinf.tinfo_t(ida_typeinf.BT_INT8 | ida_typeinf.BTMT_USIGNED), size)
    m.type = arr
    m.offset = off * 8
    m.size = size * 8
    return m


def show(name, lo=0, hi=None):
    til, t, u = _get(name)
    hi = t.get_size() if hi is None else hi
    print("%s size %d (0x%X), ordinal %d" % (name, t.get_size(), t.get_size(), t.get_ordinal()))
    for i in range(len(u)):
        m = u[i]
        o, s = m.offset // 8, m.size // 8
        if o < hi and o + max(s, 1) > lo:
            print("  +0x%04X %7d  %-36s %s" % (o, s, m.name, m.type.dstr()))


def decl(name):
    til, t, u = _get(name)
    print(t._print(name, ida_typeinf.PRTYPE_DEF | ida_typeinf.PRTYPE_MULTI | ida_typeinf.PRTYPE_TYPE))


def fill_pad(name, fields, lo=0, hi=None, apply=False):
    til, t, u = _get(name)
    size = t.get_size()
    hi = size if hi is None else hi
    if any(u[i].is_bitfield() for i in range(len(u))):
        raise ValueError("%r has bitfields; this helper does not rebuild bitfield structs" % name)
    names = {u[i].name for i in range(len(u))}
    req = []
    for off, fname, ctext in fields:
        ft = _ctype(ctext)
        fs = ft.get_size()
        if fs <= 0 or fs == ida_typeinf.BADSIZE:
            raise ValueError("type %r has no size" % ctext)
        if fname in names:
            raise ValueError("member name %r already exists in %s" % (fname, name))
        if not (lo <= off and off + fs <= hi):
            raise ValueError("%s at 0x%X (+%d) is outside your range [0x%X, 0x%X)" % (fname, off, fs, lo, hi))
        req.append((off, fs, fname, ft))
    req.sort()
    for a, b in zip(req, req[1:]):
        if a[0] + a[1] > b[0]:
            raise ValueError("new fields %s and %s overlap" % (a[2], b[2]))
    out = []
    used = set()
    for i in range(len(u)):
        m = u[i]
        mo, ms = m.offset // 8, m.size // 8
        inside = [r for r in req if mo <= r[0] and r[0] + r[1] <= mo + ms]
        if inside and not _PAD.match(m.name or ""):
            raise ValueError("0x%X is inside real member %s, not a pad" % (inside[0][0], m.name))
        if not inside:
            out.append(m)
            continue
        cur = mo
        for off, fs, fname, ft in inside:
            if off > cur:
                out.append(_pad_member(cur, off - cur))
            nm = ida_typeinf.udm_t()
            nm.name, nm.type, nm.offset, nm.size = fname, ft, off * 8, fs * 8
            out.append(nm)
            used.add(fname)
            cur = off + fs
        if cur < mo + ms:
            out.append(_pad_member(cur, mo + ms - cur))
    missing = [r[2] for r in req if r[2] not in used]
    if missing:
        raise ValueError("not wholly inside a single pad member: %s" % ", ".join(missing))
    nu = ida_typeinf.udt_type_data_t()
    nu.taudt_bits, nu.pack, nu.sda = u.taudt_bits, u.pack, u.sda
    nu.is_union = False
    for m in out:
        nu.push_back(m)
    nt = ida_typeinf.tinfo_t()
    if not nt.create_udt(nu, ida_typeinf.BTF_STRUCT):
        raise ValueError("IDA refused the rebuilt struct")
    chk = ida_typeinf.udt_type_data_t()
    nt.get_udt_details(chk)
    if nt.get_size() != size:
        raise ValueError("rebuilt size %d != %d; nothing changed" % (nt.get_size(), size))
    want = [(m.name, m.offset) for m in out]
    got = [(chk[i].name, chk[i].offset) for i in range(len(chk))]
    if want != got:
        bad = next((w, g) for w, g in zip(want, got) if w != g)
        raise ValueError("rebuilt offsets differ (%s); nothing changed" % (bad,))
    for off, fs, fname, ft in req:
        print("  + 0x%04X %5d  %-32s %s" % (off, fs, fname, ft.dstr()))
    if not apply:
        print("DRY RUN OK: %d field(s) fit; size stays %d. Call again with apply=True to write." % (len(req), size))
        return False
    err = nt.set_numbered_type(til, t.get_ordinal(), ida_typeinf.NTF_REPLACE, name)
    if err != ida_typeinf.TERR_OK:
        raise ValueError("IDA could not save %s (error %s)" % (name, err))
    print("APPLIED: %d field(s) added to %s; size %d unchanged." % (len(req), name, size))
    return True


def backup_all(path):
    import os
    os.makedirs(os.path.dirname(path), exist_ok=True)
    til = ida_typeinf.get_idati()
    n = 0
    with open(path, "w", encoding="utf-8") as f:
        for o in range(1, ida_typeinf.get_ordinal_limit(til)):
            t = ida_typeinf.tinfo_t()
            if not t.get_numbered_type(til, o):
                continue
            nm = t.get_type_name() or ""
            s = t._print(nm, ida_typeinf.PRTYPE_DEF | ida_typeinf.PRTYPE_MULTI | ida_typeinf.PRTYPE_TYPE)
            if not s:
                continue
            pack = 0
            if t.is_udt():
                u = ida_typeinf.udt_type_data_t()
                if t.get_udt_details(u):
                    pack = u.pack
            size = t.get_size()
            f.write("/* ordinal %d, size %s */\n" % (o, size if size != ida_typeinf.BADSIZE else "?"))
            if pack:
                f.write("#pragma pack(push, %d)\n%s;\n#pragma pack(pop)\n\n" % (1 << (pack - 1) if pack > 1 else 1, s))
            else:
                f.write("%s;\n\n" % s)
            n += 1
    print("wrote %d types to %s" % (n, path))
