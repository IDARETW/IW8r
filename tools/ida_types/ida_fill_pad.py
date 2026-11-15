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


def _get(name, allow_union=False):
    til = ida_typeinf.get_idati()
    t = ida_typeinf.tinfo_t()
    if not t.get_named_type(til, name) or not t.is_udt():
        raise ValueError("no struct named %r in Local Types" % name)
    u = ida_typeinf.udt_type_data_t()
    if not t.get_udt_details(u):
        raise ValueError("cannot read members of %r" % name)
    if u.is_union and not allow_union:
        raise ValueError("%r is a union; fill_pad does not edit unions (report changes as proposals)" % name)
    return til, t, u


def _ctype(text):
    t = ida_typeinf.tinfo_t()
    til = ida_typeinf.get_idati()
    decl = text.strip()
    if not decl.endswith(";"):
        arr = re.match(r"^(.*?)\s*((?:\[\s*\d+\s*\])+)$", decl)
        decl = ("%s __iw8r_tmp%s;" % (arr.group(1), arr.group(2))) if arr else (decl + " __iw8r_tmp;")
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
    til, t, u = _get(name, allow_union=True)
    hi = t.get_size() if hi is None else hi
    print("%s size %d (0x%X), ordinal %d" % (name, t.get_size(), t.get_size(), t.get_ordinal()))
    for i in range(len(u)):
        m = u[i]
        o, s = m.offset // 8, m.size // 8
        if o < hi and o + max(s, 1) > lo:
            print("  +0x%04X %7d  %-36s %s" % (o, s, m.name, m.type.dstr()))


def decl(name):
    til, t, u = _get(name, allow_union=True)
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


def resize_tail_pad(name, new_size, apply=False):
    """COORDINATOR ONLY (agents must not call this). Change a struct's total size by resizing its LAST member, which
    must be a pad. No other member moves. Refuses when the last member is not a pad or would vanish."""
    til, t, u = _get(name)
    old = t.get_size()
    last = u[len(u) - 1]
    lo = last.offset // 8
    if not _PAD.match(last.name or ""):
        raise ValueError("last member of %s is %s, not a pad" % (name, last.name))
    if new_size <= lo:
        raise ValueError("new size %d would remove the tail pad that starts at 0x%X" % (new_size, lo))
    nu = ida_typeinf.udt_type_data_t()
    nu.taudt_bits, nu.pack, nu.sda = u.taudt_bits, u.pack, u.sda
    nu.is_union = False
    for i in range(len(u) - 1):
        nu.push_back(u[i])
    nu.push_back(_pad_member(lo, new_size - lo))
    nt = ida_typeinf.tinfo_t()
    if not nt.create_udt(nu, ida_typeinf.BTF_STRUCT):
        raise ValueError("IDA refused the rebuilt struct")
    chk = ida_typeinf.udt_type_data_t()
    nt.get_udt_details(chk)
    if nt.get_size() != new_size or any(chk[i].offset != u[i].offset for i in range(len(u) - 1)):
        raise ValueError("rebuilt layout differs; nothing changed")
    print("%s: size %d -> %d (tail pad at 0x%X now %d bytes)" % (name, old, new_size, lo, new_size - lo))
    if not apply:
        print("DRY RUN OK")
        return False
    err = nt.set_numbered_type(til, t.get_ordinal(), ida_typeinf.NTF_REPLACE, name)
    if err != ida_typeinf.TERR_OK:
        raise ValueError("IDA could not save %s (error %s)" % (name, err))
    print("APPLIED")
    return True


def narrow_member(name, member, ctext, apply=False):
    """COORDINATOR ONLY. Give a member a NARROWER type and fill the freed bytes with a pad. No other member moves and
    the struct size stays the same."""
    til, t, u = _get(name)
    size = t.get_size()
    nt_m = _ctype(ctext)
    ns = nt_m.get_size()
    idx = next((i for i in range(len(u)) if u[i].name == member), None)
    if idx is None:
        raise ValueError("no member %r in %s" % (member, name))
    m = u[idx]
    mo, ms = m.offset // 8, m.size // 8
    if m.is_bitfield() or not (0 < ns < ms):
        raise ValueError("new type is %d bytes; member %s is %d bytes (must be narrower)" % (ns, member, ms))
    nu = ida_typeinf.udt_type_data_t()
    nu.taudt_bits, nu.pack, nu.sda = u.taudt_bits, u.pack, u.sda
    nu.is_union = False
    for i in range(len(u)):
        if i != idx:
            nu.push_back(u[i])
            continue
        nm = ida_typeinf.udm_t()
        nm.name, nm.type, nm.offset, nm.size = member, nt_m, mo * 8, ns * 8
        nu.push_back(nm)
        nu.push_back(_pad_member(mo + ns, ms - ns))
    nt = ida_typeinf.tinfo_t()
    if not nt.create_udt(nu, ida_typeinf.BTF_STRUCT):
        raise ValueError("IDA refused the rebuilt struct")
    chk = ida_typeinf.udt_type_data_t()
    nt.get_udt_details(chk)
    if nt.get_size() != size:
        raise ValueError("rebuilt size differs; nothing changed")
    old = {(u[i].name, u[i].offset) for i in range(len(u))}
    new = {(chk[i].name, chk[i].offset) for i in range(len(chk))}
    if not old - {(member, m.offset)} <= new:
        raise ValueError("rebuilt offsets differ; nothing changed")
    print("%s.%s: %s (%d) -> %s (%d) + _pad%X (%d)" % (name, member, m.type.dstr(), ms, ctext, ns, mo + ns, ms - ns))
    if not apply:
        print("DRY RUN OK")
        return False
    err = nt.set_numbered_type(til, t.get_ordinal(), ida_typeinf.NTF_REPLACE, name)
    if err != ida_typeinf.TERR_OK:
        raise ValueError("IDA could not save %s (error %s)" % (name, err))
    print("APPLIED")
    return True


def relayout(name, fields, apply=False, new_size=None):
    """COORDINATOR ONLY. Replace a struct's members with `fields` [(offset, name, ctype), ...] keeping the ordinal and
    pack; the size stays unless new_size is given. Gaps become _padXXX members. A field named `baseclass_0` at offset 0
    is marked as the base class. Refuses overlaps or fields past the end."""
    til, t, u = _get(name)
    size = t.get_size() if new_size is None else new_size
    req = sorted((off, nm, _ctype(ct)) for off, nm, ct in fields)
    nu = ida_typeinf.udt_type_data_t()
    nu.taudt_bits, nu.pack, nu.sda = u.taudt_bits, u.pack, u.sda
    nu.is_union = False
    cur = 0
    for off, nm, ft in req:
        fs = ft.get_size()
        if off < cur:
            raise ValueError("%s at 0x%X overlaps the previous field (ends 0x%X)" % (nm, off, cur))
        if off > cur:
            nu.push_back(_pad_member(cur, off - cur))
        m = ida_typeinf.udm_t()
        m.name, m.type, m.offset, m.size = nm, ft, off * 8, fs * 8
        if nm == "baseclass_0" and off == 0:
            try:
                m.set_baseclass(True)
            except Exception:
                pass
        nu.push_back(m)
        cur = off + fs
    if cur > size:
        raise ValueError("fields end at 0x%X, past the size 0x%X" % (cur, size))
    if cur < size:
        nu.push_back(_pad_member(cur, size - cur))
    nt = ida_typeinf.tinfo_t()
    if not nt.create_udt(nu, ida_typeinf.BTF_STRUCT) or nt.get_size() != size:
        raise ValueError("rebuilt struct has the wrong size; nothing changed")
    chk = ida_typeinf.udt_type_data_t()
    nt.get_udt_details(chk)
    for i in range(len(chk)):
        print("  +0x%04X %5d  %-32s %s" % (chk[i].offset // 8, chk[i].size // 8, chk[i].name, chk[i].type.dstr()))
    if not apply:
        print("DRY RUN OK: %s keeps size %d" % (name, size))
        return False
    err = nt.set_numbered_type(til, t.get_ordinal(), ida_typeinf.NTF_REPLACE, name)
    if err != ida_typeinf.TERR_OK:
        raise ValueError("IDA could not save %s (error %s)" % (name, err))
    print("APPLIED")
    return True


def truncate(name, new_size, drop=(), apply=False):
    """COORDINATOR ONLY. Shrink a struct to new_size by dropping the members that start at or after new_size (each must
    be a pad or be named in `drop`) and cutting a pad that crosses new_size. No other member moves."""
    til, t, u = _get(name)
    old = t.get_size()
    nu = ida_typeinf.udt_type_data_t()
    nu.taudt_bits, nu.pack, nu.sda = u.taudt_bits, u.pack, u.sda
    nu.is_union = False
    dropped = []
    for i in range(len(u)):
        m = u[i]
        mo, ms = m.offset // 8, m.size // 8
        if mo + ms <= new_size:
            nu.push_back(m)
        elif mo >= new_size:
            if not (_PAD.match(m.name or "") or m.name in drop):
                raise ValueError("member %s at 0x%X is past the new end and not in drop" % (m.name, mo))
            dropped.append(m.name)
        elif _PAD.match(m.name or ""):
            nu.push_back(_pad_member(mo, new_size - mo))
        else:
            raise ValueError("member %s at 0x%X crosses the new end 0x%X" % (m.name, mo, new_size))
    nt = ida_typeinf.tinfo_t()
    if not nt.create_udt(nu, ida_typeinf.BTF_STRUCT) or nt.get_size() != new_size:
        raise ValueError("rebuilt size differs; nothing changed")
    print("%s: size %d -> %d, dropped %s" % (name, old, new_size, dropped))
    if not apply:
        print("DRY RUN OK")
        return False
    err = nt.set_numbered_type(til, t.get_ordinal(), ida_typeinf.NTF_REPLACE, name)
    if err != ida_typeinf.TERR_OK:
        raise ValueError("IDA could not save %s (error %s)" % (name, err))
    print("APPLIED")
    return True


def embedded_in(child):
    """List (parent, member, offset, size, next member) for every struct that holds `child` by value."""
    til = ida_typeinf.get_idati()
    for o in range(1, ida_typeinf.get_ordinal_limit(til)):
        t = ida_typeinf.tinfo_t()
        if not t.get_numbered_type(til, o) or not t.is_udt():
            continue
        u = ida_typeinf.udt_type_data_t()
        if not t.get_udt_details(u):
            continue
        for i in range(len(u)):
            mt = u[i].type
            base = mt.get_array_element() if mt.is_array() else mt
            if not base.is_ptr() and base.get_type_name() == child:
                nx = u[i + 1] if i + 1 < len(u) else None
                print(t.get_type_name(), u[i].name, u[i].offset // 8, u[i].size // 8,
                      (nx.name, nx.offset // 8) if nx else "(last)")


def remap(name, new_size, keep, extra=(), apply=False):
    """COORDINATOR ONLY. Rebuild a struct at new_size (same ordinal and pack) from measured Replay deltas.
    keep:  [(old_lo, old_hi, delta), ...]  every non-pad member wholly inside [old_lo, old_hi) of the CURRENT layout is
           kept at old_offset + delta;
    extra: [(new_offset, name, ctype), ...] members placed explicitly (retypes, replacements).
    Everything else becomes _padXXX (unconfirmed members are dropped, so list them in the log). Refuses overlaps."""
    til, t, u = _get(name)
    if any(u[i].is_bitfield() for i in range(len(u))):
        raise ValueError("%s has bitfields" % name)
    items = []
    for i in range(len(u)):
        m = u[i]
        if _PAD.match(m.name or ""):
            continue
        mo, ms = m.offset // 8, m.size // 8
        for lo, hi, d in keep:
            if lo <= mo and mo + ms <= hi:
                items.append((mo + d, ms, m.name, m.type))
                break
    for off, nm, ct in extra:
        ft = _ctype(ct)
        items.append((off, ft.get_size(), nm, ft))
    items.sort(key=lambda x: x[0])
    names = [x[2] for x in items]
    if len(names) != len(set(names)):
        raise ValueError("duplicate member names: %s" % sorted({n for n in names if names.count(n) > 1}))
    kept = set(names)
    gone = [u[i].name for i in range(len(u)) if not _PAD.match(u[i].name or "") and u[i].name not in kept]
    nu = ida_typeinf.udt_type_data_t()
    nu.taudt_bits, nu.pack, nu.sda = u.taudt_bits, u.pack, u.sda
    nu.is_union = False
    cur = 0
    for off, ms, nm, ty in items:
        if off < cur:
            raise ValueError("%s at 0x%X overlaps the previous member (ends 0x%X)" % (nm, off, cur))
        if off > cur:
            nu.push_back(_pad_member(cur, off - cur))
        m = ida_typeinf.udm_t()
        m.name, m.type, m.offset, m.size = nm, ty, off * 8, ms * 8
        nu.push_back(m)
        cur = off + ms
    if cur > new_size:
        raise ValueError("members end at 0x%X, past the new size 0x%X" % (cur, new_size))
    if cur < new_size:
        nu.push_back(_pad_member(cur, new_size - cur))
    nt = ida_typeinf.tinfo_t()
    if not nt.create_udt(nu, ida_typeinf.BTF_STRUCT) or nt.get_size() != new_size:
        raise ValueError("rebuilt size differs; nothing changed")
    chk = ida_typeinf.udt_type_data_t()
    nt.get_udt_details(chk)
    want = [(x[2], x[0]) for x in items]
    got = [(chk[i].name, chk[i].offset // 8) for i in range(len(chk)) if not _PAD.match(chk[i].name or "")]
    if want != got:
        raise ValueError("rebuilt offsets differ; nothing changed")
    print("%s: size %d -> %d; %d members kept/placed; dropped (unconfirmed): %s" % (name, t.get_size(), new_size,
          len(items), gone))
    if not apply:
        print("DRY RUN OK")
        return False
    err = nt.set_numbered_type(til, t.get_ordinal(), ida_typeinf.NTF_REPLACE, name)
    if err != ida_typeinf.TERR_OK:
        raise ValueError("IDA could not save %s (error %s)" % (name, err))
    print("APPLIED")
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
