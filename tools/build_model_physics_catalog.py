"""Builds data/asset_catalog/iw8_model_physics.tsv: which stock models have a physics asset (the game's static model
collision), and that physics asset's shape in model space (rule: src/core/model_physics.h; research 142).

The game (Replay 1.20, StaticModels_AddClipmapCollisionShapes) gives a placed static model collision only from the
clipmap's static model collision lists, and every entry there is exactly the XModel's physicsAsset / detailCollision
pair (mp_shipment: 198 of 198). A model without a physics asset (most bushes, grass, flowers) has no collision at all.

The physics asset is a Havok 2018 tagfile without types of its own (TCRF: the Replay compendium 0xC16B2E49324C3540).
The types come from the game's global type compendium (code_post_gfx physicslibrary physicsglobaltypecompendium,
exported here with ACTS): its TNA1 / TSTR names each type index and its TBDY gives every member's offset, so this
reader looks members up by name (VERIFIED layouts, research 142 section 3). A pointer, array or relative-array field
holds the index of the item it names. The shape of each body (hknpBodyCinfo: shape, position, orientation) is read:
  - hknpConvexShape and its kinds (hknpConvexPolytopeShape, hknpBoxShape, hknpCylinderShape, hknpCapsuleShape): its
    vertices (hkVector4f, Havok units = inches / 32), widened by the convex radius when that is over half an inch;
  - hknpCompoundShape: every used instance (hknpShapeInstance: hkTransform rotation columns + translation, scale);
  - hknpCompressedMeshShape: the triangles and quads of its mesh tree (packed 11/11/10-bit vertices over each section's
    codec, shared 21/21/22-bit vertices over the tree domain), its custom primitives (convex vertex sets, GLOBAL or
    LOCAL_4 over the primitive's leaf box in the section tree) and its extern convex shapes.
Any other shape type, or a result outside the bounds of every model that uses it (padded by 8 units or a quarter of
its size), is written as `mesh`: the editor then uses the model's render mesh, as before.

For each zone of data/asset_catalog/iw8_models.tsv it exports (ACTS, read-only on the game folder) the xmodel and
physicsasset tables into out/model_physics_study/<zone> (removed after reading unless --keep; the decoded result is
kept as out/model_physics_study/<zone>.json, so a second run reuses it while DECODER_VERSION is the same).

Usage: py -3 -I tools/build_model_physics_catalog.py [--zones a,b] [--keep] [--no-export]"""
import collections, glob, json, math, os, shutil, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STUDY = os.path.join(ROOT, 'out', 'model_physics_study')
OUT = os.path.join(ROOT, 'data', 'asset_catalog', 'iw8_model_physics.tsv')
ACTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'bin', 'release', 'iw8r-acts.exe')
GAME = r'E:\IW8\Builds\1.20-replay'
HAVOK_INCH = 32.0           # inches per Havok unit (x64-zt replay_havok.cpp divides by 32; the decoded shapes fit)
COMPENDIUM_ID = '40354c32492e6bc1'  # TCRF of every stock physics asset: the compendium signature, little-endian
DECODER_VERSION = 2


def zone_file(zone):
    kept = os.path.join(GAME, 'iw8r_stock', zone + '.ff')  # an installed edit keeps the stock file here
    return kept if os.path.exists(kept) else os.path.join(GAME, 'zone', zone + '.ff')


def zones_of_catalog():
    zones = set()
    with open(os.path.join(ROOT, 'data', 'asset_catalog', 'iw8_models.tsv'), encoding='utf-8') as f:
        for line in f:
            if line.startswith('#') or '\t' not in line:
                continue
            for z in line.rstrip('\n').split('\t')[1].split(','):
                if z.strip():
                    zones.add(z.strip())
    return sorted(zones)


def clean(name):
    return (name or '').lstrip(',~')


# ------------------------------------------------------------------------------------------------ Havok tagfiles

def sections(b, o=0, end=None, out=None):
    """{tag: (begin, end)}: each section is a big-endian u32 (flag << 30 | size with header) and a 4-character tag;
    flag 0 marks a container."""
    end = len(b) if end is None else end
    out = {} if out is None else out
    while o + 8 <= end:
        h = struct.unpack_from('>I', b, o)[0]
        size = h & 0x3FFFFFFF
        tag = b[o + 4:o + 8].decode('latin1')
        if size < 8:
            break
        out.setdefault(tag, (o + 8, o + size))
        if (h >> 30) == 0:
            sections(b, o + 8, o + size, out)
        o += size
    return out


def varint(b, o):
    """The type section's packed unsigned integer: the leading bits give the length."""
    x = b[o]
    if x < 0x80:
        return x, o + 1
    if x < 0xC0:
        return ((x & 0x3F) << 8) | b[o + 1], o + 2
    if x < 0xE0:
        return int.from_bytes(b[o:o + 3], 'big') & 0x1FFFFF, o + 3
    if x < 0xE8:
        return int.from_bytes(b[o:o + 4], 'big') & 0x7FFFFFF, o + 4
    if x < 0xF0:
        return int.from_bytes(b[o:o + 5], 'big') & 0x7FFFFFFFF, o + 5
    if x == 0xF8:
        return int.from_bytes(b[o + 1:o + 6], 'big'), o + 6
    if x == 0xF9:
        return int.from_bytes(b[o + 1:o + 9], 'big'), o + 9
    return int.from_bytes(b[o:o + 8], 'big') & 0x7FFFFFFFFFFFFFF, o + 8


class Compendium:
    """Type names, parents, sizes and members (name, offset, type) by type index."""

    def __init__(self, tagfile):
        s = sections(tagfile)
        body = lambda t: tagfile[s[t][0]:s[t][1]] if t in s else b''
        tstr = [x.decode('latin1') for x in body('TSTR').split(b'\0')]
        fstr = [x.decode('latin1') for x in body('FSTR').split(b'\0')]
        q = body('TNAM') or body('TNA1')
        n, o = varint(q, 0)
        self.types = [None]
        for _ in range(n - 1):
            ni, o = varint(q, o)
            npar, o = varint(q, o)
            for _ in range(npar):
                _pn, o = varint(q, o)
                _pv, o = varint(q, o)
            self.types.append({'name': tstr[ni], 'parent': 0, 'members': []})
        q = body('TBDY')
        o = 0
        while o < len(q):
            ti, o = varint(q, o)
            if ti == 0:
                continue
            t = self.types[ti]
            t['parent'], o = varint(q, o)
            flags, o = varint(q, o)
            if flags & 0x01:
                _fmt, o = varint(q, o)
            if flags & 0x02:
                _sub, o = varint(q, o)
            if flags & 0x04:
                _ver, o = varint(q, o)
            if flags & 0x08:
                t['size'], o = varint(q, o)
                _align, o = varint(q, o)
            if flags & 0x10:
                _flags, o = varint(q, o)
            if flags & 0x20:
                nm, o = varint(q, o)
                for _ in range(nm & 0xFFFF):
                    mn, o = varint(q, o)
                    _mf, o = varint(q, o)
                    mo, o = varint(q, o)
                    mt, o = varint(q, o)
                    t['members'].append((fstr[mn], mo, mt))
        self.by_name = {}
        for i in range(1, len(self.types)):
            self.by_name.setdefault(self.types[i]['name'], i)

    def name(self, i):
        return self.types[i]['name'] if 0 < i < len(self.types) else ''

    def size(self, name):
        i = self.by_name[name]
        while 0 < i < len(self.types) and not self.types[i].get('size'):
            i = self.types[i]['parent']
        return self.types[i].get('size', 0) if 0 < i < len(self.types) else 0

    def is_a(self, i, base):
        for _ in range(64):
            if not 0 < i < len(self.types):
                return False
            if self.types[i]['name'] == base:
                return True
            i = self.types[i]['parent']
        return False

    def member(self, i, path):
        """Offset of a member ('a.b' for an embedded record) in type i or its parents."""
        off = 0
        for part in path.split('.'):
            found, t = None, i
            while found is None and 0 < t < len(self.types):
                for (mn, mo, mt) in self.types[t]['members']:
                    if mn == part:
                        found = (mo, mt)
                        break
                t = self.types[t]['parent']
            if found is None:
                raise KeyError('%s has no member %s' % (self.name(i), path))
            off += found[0]
            i = found[1]
        return off


class Fail(Exception):
    pass


class Tagfile:
    def __init__(self, b):
        self.b = b
        s = sections(b)
        if 'DATA' not in s or 'ITEM' not in s:
            raise Fail('not a tagfile')
        self.tcrf = b[s['TCRF'][0]:s['TCRF'][1]].hex()[:16] if 'TCRF' in s else ''
        base = s['DATA'][0]
        self.items = []
        lo, hi = s['ITEM']
        for i in range(lo, hi - 11, 12):
            a, o, c = struct.unpack_from('<3I', b, i)
            self.items.append((a & 0xFFFFFF, a >> 24, base + o, c))

    def u32(self, at):
        return struct.unpack_from('<I', self.b, at)[0]

    def item_at(self, at):
        k = self.u32(at)
        return k if 0 < k < len(self.items) else 0

    def floats(self, at, n):
        return struct.unpack_from('<%df' % n, self.b, at)


def apply(t, p):
    r, s, o = t
    q = (p[0] * s[0], p[1] * s[1], p[2] * s[2])
    return tuple(r[a][0] * q[0] + r[a][1] * q[1] + r[a][2] * q[2] + o[a] for a in range(3))


def compose(outer, inner):
    """outer(inner(p)) with each transform (rotation rows, scale applied first, translation)."""
    r1, s1, o1 = outer
    r2, s2, o2 = inner
    m = [[r1[a][k] * s1[k] for k in range(3)] for a in range(3)]
    rr = [[sum(m[a][k] * r2[k][c] for k in range(3)) for c in range(3)] for a in range(3)]
    oo = tuple(sum(m[a][k] * o2[k] for k in range(3)) + o1[a] for a in range(3))
    return (rr, s2, oo)


class Decoder:
    def __init__(self, comp):
        self.c = comp

    def decode(self, raw):
        """{'pieces': [[(x, y, z)]], 'verts': [...], 'tris': [(i, j, k)]} in Havok units, model space."""
        tf = Tagfile(raw)
        if tf.tcrf != COMPENDIUM_ID:
            raise Fail('other compendium')
        c = self.c
        out = {'pieces': [], 'verts': [], 'tris': []}
        bodies = [k for k, it in enumerate(tf.items) if it[1] == 0x20 and c.is_a(it[0], 'hknpBodyCinfo')]
        if not bodies:
            raise Fail('no body')
        for k in bodies:  # every body of the asset, each at its place
            bt, _, base, count = tf.items[k]
            size = c.types[bt].get('size') or c.size('hknpBodyCinfo')
            for i in range(count):
                bo = base + size * i
                pos = tf.floats(bo + c.member(bt, 'position'), 3)
                qx, qy, qz, qw = tf.floats(bo + c.member(bt, 'orientation'), 4)
                n = math.sqrt(qx * qx + qy * qy + qz * qz + qw * qw)
                if not 0.99 < n < 1.01:
                    raise Fail('body orientation')
                qx, qy, qz, qw = qx / n, qy / n, qz / n, qw / n
                rot = [[1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy - qz * qw), 2 * (qx * qz + qy * qw)],
                       [2 * (qx * qy + qz * qw), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz - qx * qw)],
                       [2 * (qx * qz - qy * qw), 2 * (qy * qz + qx * qw), 1 - 2 * (qx * qx + qy * qy)]]
                shape = tf.item_at(bo + c.member(bt, 'shape'))
                if not shape:
                    raise Fail('body without a shape')
                self.shape(tf, shape, (rot, (1, 1, 1), pos), out, 0)
        return out

    def shape(self, tf, k, xf, out, depth):
        c = self.c
        if depth > 8:
            raise Fail('nesting')
        t, _fl, o, _n = tf.items[k]
        name = c.name(t)
        if c.is_a(t, 'hknpConvexShape'):
            vk = tf.item_at(o + c.member(t, 'vertices'))
            if not vk:
                raise Fail('%s without vertices' % name)
            vt, _, vo, vc = tf.items[vk]
            if c.name(vt) not in ('hkVector4f', 'hkVector4'):
                raise Fail('%s vertices of type %s' % (name, c.name(vt)))
            local = [tf.floats(vo + 16 * i, 3) for i in range(vc)]
            # The convex radius rounds the hull (a capsule is a segment and its radius): add it along the axes and
            # the diagonals of each point when it is more than half an inch (or when the core is not a solid).
            radius = tf.floats(o + c.member(t, 'convexRadius'), 1)[0]
            if len(local) < 4:
                radius = max(radius, 0.25 / HAVOK_INCH)
            if radius * HAVOK_INCH > 0.5 or len(local) < 4:
                dirs = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]
                dirs += [(x * 0.5773, y * 0.5773, z * 0.5773) for x in (-1, 1) for y in (-1, 1) for z in (-1, 1)]
                local = [tuple(p[a] + d[a] * radius for a in range(3)) for p in local for d in dirs]
            if len(local) < 4:
                raise Fail('%s with %d vertices' % (name, len(local)))
            out['pieces'].append([apply(xf, p) for p in local])
            return
        if c.is_a(t, 'hknpCompoundShape'):
            ek = tf.item_at(o + c.member(t, 'instances.elements'))
            if not ek:
                raise Fail('compound without instances')
            _et, _, eo, ec = tf.items[ek]
            self.instances(tf, eo, ec, xf, out, depth, True)
            return
        if c.is_a(t, 'hknpCompressedMeshShape'):
            self.mesh(tf, t, o, xf, out)
            ek = tf.item_at(o + c.member(t, 'externShapes'))
            if ek:
                _et, _, eo, ec = tf.items[ek]
                self.instances(tf, eo, ec, xf, out, depth, False)
            return
        raise Fail('shape type %s' % name)

    def instances(self, tf, eo, ec, xf, out, depth, freelist):
        c = self.c
        it = c.by_name['hknpShapeInstance']
        size = c.size('hknpShapeInstance')
        off_t, off_s, off_shape, off_empty = (c.member(it, 'transform'), c.member(it, 'scale'), c.member(it, 'shape'),
                                              c.member(it, 'isEmpty'))
        for i in range(ec):
            r = eo + size * i
            if freelist and tf.b[r + off_empty]:
                continue  # a free element of the compound's free list
            m = tf.floats(r + off_t, 16)  # hkTransform: three rotation columns, then the translation
            rows = [[m[0], m[4], m[8]], [m[1], m[5], m[9]], [m[2], m[6], m[10]]]
            sc = tf.floats(r + off_s, 3)
            child = tf.item_at(r + off_shape)
            if not child:
                raise Fail('instance without a shape')
            self.shape(tf, child, compose(xf, (rows, sc, (m[12], m[13], m[14]))), out, depth + 1)

    def leaf_box(self, tf, so, st, leaf):
        """The box of node `leaf` of a section's tree: 4-byte nodes, min / max nibbles squared over a 226th of the
        parent box, data bit 0 marks an internal node, its right child is this + (data & 0xFE), the left one this + 1."""
        c = self.c
        nk = tf.item_at(so + c.member(st, 'nodes'))
        if not nk:
            raise Fail('section without a tree')
        _, _, no, nn = tf.items[nk]
        dom = tf.floats(so + c.member(st, 'domain'), 8)

        def unpack(parent, i):
            x = tf.b[no + 4 * i:no + 4 * i + 3]
            ext = [(parent[3 + a] - parent[a]) / 226.0 for a in range(3)]
            return tuple([parent[a] + ext[a] * (x[a] >> 4) ** 2 for a in range(3)] +
                         [parent[3 + a] - ext[a] * (x[a] & 15) ** 2 for a in range(3)])

        stack = [(0, unpack((dom[0], dom[1], dom[2], dom[4], dom[5], dom[6]), 0))]
        while stack:
            i, box = stack.pop()
            if i == leaf:
                return box
            data = tf.b[no + 4 * i + 3]
            if data & 1:
                for child in (i + 1, i + (data & 0xFE)):
                    if child < nn:
                        stack.append((child, unpack(box, child)))
        raise Fail('custom primitive leaf not in the tree')

    def mesh(self, tf, t, o, xf, out):
        c = self.c
        dk = tf.item_at(o + c.member(t, 'data'))
        if not dk or not c.is_a(tf.items[dk][0], 'hknpCompressedMeshShapeData'):
            raise Fail('compressed mesh without its data')
        dt, _, do, _ = tf.items[dk]
        mt = do + c.member(dt, 'meshTree')
        tree = c.by_name['hknpCompressedMeshShapeTree']

        def arr(m):
            k = tf.item_at(mt + c.member(tree, m))
            return tf.items[k] if k else (0, 0, 0, 0)

        dom = tf.floats(mt + c.member(tree, 'domain'), 8)
        lo, hi = dom[0:3], dom[4:7]
        _, _, sec_o, sec_n = arr('sections')
        _, _, prim_o, prim_n = arr('primitives')
        _, _, idx_o, idx_n = arr('sharedVerticesIndex')
        _, _, pv_o, pv_n = arr('packedVertices')
        _, _, sv_o, sv_n = arr('sharedVertices')
        if not sec_n:
            raise Fail('compressed mesh without sections')
        st = c.by_name['hkcdStaticMeshTree::Section']
        ssize = c.size('hkcdStaticMeshTree::Section')
        mo = {k: c.member(st, k) for k in ('codecParms', 'firstPackedVertexIndex', 'firstSharedVertexIndex',
                                           'firstPrimitiveIndex', 'numPackedVertices', 'numPrimitives')}
        shared_step = [(hi[a] - lo[a]) / float((1 << b) - 1) for a, b in zip(range(3), (21, 21, 22))]
        u16 = lambda at: struct.unpack_from('<H', tf.b, at)[0]

        def shared(i):
            if i >= sv_n:
                raise Fail('shared vertex out of range')
            w = struct.unpack_from('<Q', tf.b, sv_o + 8 * i)[0]
            q = (w & 0x1FFFFF, (w >> 21) & 0x1FFFFF, w >> 42)
            return tuple(lo[a] + q[a] * shared_step[a] for a in range(3))

        for s in range(sec_n):
            so = sec_o + ssize * s
            cp = tf.floats(so + mo['codecParms'], 6)  # packed vertices: origin xyz, then step xyz
            fp, fs = tf.u32(so + mo['firstPackedVertexIndex']), tf.u32(so + mo['firstSharedVertexIndex'])
            fprim = tf.u32(so + mo['firstPrimitiveIndex'])
            pk, pn = tf.b[so + mo['numPackedVertices']], tf.b[so + mo['numPrimitives']]
            cache = {}

            def vertex(local):
                if local not in cache:
                    if local < pk:
                        if fp + local >= pv_n:
                            raise Fail('packed vertex out of range')
                        w = tf.u32(pv_o + 4 * (fp + local))
                        q = (w & 0x7FF, (w >> 11) & 0x7FF, w >> 22)
                        p = tuple(cp[a] + q[a] * cp[3 + a] for a in range(3))
                    else:
                        if fs + local - pk >= idx_n:
                            raise Fail('shared index out of range')
                        p = shared(u16(idx_o + 2 * (fs + local - pk)))
                    cache[local] = len(out['verts'])
                    out['verts'].append(apply(xf, p))
                return cache[local]

            for kk in range(pn):
                if fprim + kk >= prim_n:
                    raise Fail('primitive out of range')
                b0, b1, b2, b3 = tf.b[prim_o + 4 * (fprim + kk):prim_o + 4 * (fprim + kk) + 4]
                if (b0, b1, b2, b3) == (0xDE, 0xAD, 0xDE, 0xAD):
                    continue  # removed
                if b1 != b3:  # triangle (b2 == b3) or quad
                    v = [vertex(x) for x in (b0, b1, b2, b3)]
                    out['tris'].append((v[0], v[1], v[2]))
                    if b2 != b3:
                        out['tris'].append((v[0], v[2], v[3]))
                    continue
                if b2 != 3:
                    continue  # EXTERNAL: one of the extern shapes, read from externShapes
                # CUSTOM: section table entry b0 = vertices << 8 | tags << 6 | compression << 4 | type, then the first
                # vertex in the shared vertex array; b1 = the primitive's leaf in the section tree.
                if fs + b0 + 1 >= idx_n:
                    raise Fail('custom primitive header out of range')
                head, first = u16(idx_o + 2 * (fs + b0)), u16(idx_o + 2 * (fs + b0 + 1))
                nv, comp = head >> 8, (head >> 4) & 3
                pts = []
                if comp == 0:  # GLOBAL: shared vertices
                    pts = [shared(first + i) for i in range(nv)]
                elif comp == 1:  # LOCAL_4: 4-byte 11/11/10-bit vertices over the leaf box, pairs swapped per 8 bytes
                    box = self.leaf_box(tf, so, st, b1)
                    step = [(box[3 + a] - box[a]) / float((1 << bb) - 1) for a, bb in zip(range(3), (11, 11, 10))]
                    for i in range(nv):
                        j = (i & ~1) + (1 - (i & 1))
                        if first * 2 + j >= sv_n * 2:
                            raise Fail('custom vertex out of range')
                        w = struct.unpack_from('<I', tf.b, sv_o + 8 * first + 4 * j)[0]
                        q = (w & 0x7FF, (w >> 11) & 0x7FF, w >> 22)
                        pts.append(tuple(box[a] + q[a] * step[a] for a in range(3)))
                else:
                    raise Fail('custom primitive compression %d' % comp)
                if len(pts) >= 4:
                    out['pieces'].append([apply(xf, p) for p in pts])


def read_compendium(keep):
    out = os.path.join(STUDY, 'compendium')
    paths = glob.glob(os.path.join(out, '**', 'physicsglobaltypecompendium*.asset.json'), recursive=True)
    if not paths:
        export('code_post_gfx', 'physicslibrary', out)
        paths = glob.glob(os.path.join(out, '**', 'physicsglobaltypecompendium*.asset.json'), recursive=True)
    if not paths:
        sys.exit('the Havok type compendium (code_post_gfx physicslibrary) could not be exported')
    fields = json.load(open(paths[0], encoding='utf-8'))['asset']['fields']
    return Compendium(bytes.fromhex(fields['havokData']['bytes']))


# ------------------------------------------------------------------------------------------------ zones

def export(zone, types, out):
    os.makedirs(out, exist_ok=True)
    args = [ACTS, '--noUpdater', 'fastfile', '-r', 'mw19replay', '-g', os.path.join(GAME, 'game_dx12_ship_replay.exe'),
            '--oodle', os.path.join(GAME, 'oo2core_7_win64.dll'), '-a', types, '-l', '-o', out, zone_file(zone)]
    r = subprocess.run(args, capture_output=True, text=True, errors='replace')
    tail = (r.stdout + r.stderr).strip().splitlines()[-2:]
    return ' | '.join(tail)


def read_zone(zone, decoder, keep, no_export):
    cached = os.path.join(STUDY, zone + '.json')
    if os.path.exists(cached):
        z = json.load(open(cached, encoding='utf-8'))
        if z.get('version') == DECODER_VERSION:
            return z
    if no_export:
        return None
    out = os.path.join(STUDY, zone)
    print('  %s: %s' % (zone, export(zone, 'xmodel,physicsasset', out)))
    models, physics = {}, {}
    for path in glob.glob(os.path.join(out, '**', '*.asset.json'), recursive=True):
        norm = path.replace(os.sep, '/')
        try:
            fields = json.load(open(path, encoding='utf-8'))['asset']['fields']
        except Exception:
            continue
        if '/assets/xmodel/' in norm:
            bd = fields.get('bounds') or {}
            mid, half = (bd.get('midPoint') or {}).get('v'), (bd.get('halfSize') or {}).get('v')
            models[clean(fields.get('name', {}).get('string'))] = {
                'physics': clean((fields.get('physicsAsset') or {}).get('name')),
                'detail': clean((fields.get('detailCollision') or {}).get('name')),
                'bounds': [[mid[a] - half[a] for a in range(3)], [mid[a] + half[a] for a in range(3)]] if mid and half else None,
            }
        elif '/assets/physicsasset/' in norm:
            contents = 0
            for v in ((fields.get('bodyContents') or {}).get('values') or []):
                contents |= int(v['value'] if isinstance(v, dict) else v)
            raw = bytes.fromhex(((fields.get('havokData') or {}).get('bytes')) or '')
            entry = {'contents': contents, 'pieces': None, 'verts': None, 'tris': None, 'reason': ''}
            try:
                d = decoder.decode(raw)
                entry['pieces'] = [[[c * HAVOK_INCH for c in v] for v in pc] for pc in d['pieces']]
                entry['verts'] = [[c * HAVOK_INCH for c in v] for v in d['verts']]
                entry['tris'] = d['tris']
                if not d['pieces'] and not d['tris']:
                    entry['reason'] = 'empty'
            except (Fail, KeyError, IndexError, struct.error) as e:
                entry['reason'] = str(e) or type(e).__name__
            physics[clean(fields.get('name', {}).get('string'))] = entry
    result = {'version': DECODER_VERSION, 'models': models, 'physics': physics}
    with open(cached, 'w', encoding='utf-8') as f:
        json.dump(result, f)
    if not keep:
        shutil.rmtree(out, ignore_errors=True)
    return result


def fits(points, bounds):
    """Within the model's bounds, padded by 8 units or a quarter of its largest side (physics may reach past the drawn
    mesh a little; a decoder mistake would put it far away)."""
    big = max(bounds[1][k] - bounds[0][k] for k in range(3))
    pad = max(8.0, 0.25 * big)
    return all(bounds[0][a] - pad <= p[a] <= bounds[1][a] + pad for p in points for a in range(3))


def number(v):
    s = '%.2f' % v
    s = s.rstrip('0').rstrip('.') if '.' in s else s
    return '0' if s in ('-0', '') else s


def main():
    argv = sys.argv[1:]
    keep, no_export = '--keep' in argv, '--no-export' in argv
    zones = zones_of_catalog()
    if '--zones' in argv:
        zones = argv[argv.index('--zones') + 1].split(',')
    os.makedirs(STUDY, exist_ok=True)
    decoder = Decoder(read_compendium(keep))
    models, physics, model_bounds = {}, {}, {}
    for zone in zones:
        z = read_zone(zone, decoder, keep, no_export)
        if not z:
            continue
        for name, m in z['models'].items():
            if name and name not in models:
                models[name] = (m['physics'], m['detail'])
                model_bounds[name] = m['bounds']
        for name, p in z['physics'].items():
            if name and name not in physics:
                physics[name] = p
    users = collections.defaultdict(list)
    for name, (pa, dc) in models.items():
        if pa:
            users[pa].append(name)
    stats = collections.Counter()
    rows_p = []
    triangles = 0
    for name in sorted(physics):
        p = physics[name]
        pieces, verts, tris, reason = p['pieces'] or [], p['verts'] or [], p['tris'] or [], p['reason']
        if not reason and users.get(name):
            pts = [v for pc in pieces for v in pc] + verts
            if not any(model_bounds.get(u) and fits(pts, model_bounds[u]) for u in users[name]):
                reason = 'outside the model bounds'
        if reason:
            pieces, verts, tris = [], [], []
            stats['not read: ' + reason.split(' ')[0]] += 1
        else:
            stats['convex + mesh' if pieces and tris else ('mesh' if tris else 'convex')] += 1
            triangles += len(tris)
        shape = ';'.join(' '.join(','.join(number(c) for c in v) for v in pc) for pc in pieces) if pieces else ('-' if tris else 'mesh')
        mesh = ('%s|%s' % (' '.join(','.join(number(c) for c in v) for v in verts), ' '.join('%d,%d,%d' % t for t in tris))
                if tris else '-')
        rows_p.append('P\t%s\t%x\t%s\t%s\t%s' % (name, p['contents'], shape, mesh, reason or '-'))
    rows_m = ['M\t%s\t%s\t%s' % (name, pa or '-', dc or '-') for name, (pa, dc) in sorted(models.items())]
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# Replay 1.20 stock model physics, from ACTS xmodel / physicsasset exports of %d zones and the game\'s Havok '
                'type compendium (tools/build_model_physics_catalog.py; rule: src/core/model_physics.h; research 142).\n' % len(zones))
        f.write('# M <model> <physics asset or -> <detail collision or ->: the XModel\'s collision (no physics asset: the game '
                'gives the model no collision).\n')
        f.write('# P <physics asset> <body contents, hex> <convex pieces> <triangles> <why not read or ->: model space, inches, '
                'Z-up. Pieces: split by ";", vertices by " ", x,y,z ("-": none; "mesh": not read, use the render mesh). '
                'Triangles: "<vertices>|<i,j,k ...>" or "-".\n')
        for line in rows_m + rows_p:
            f.write(line + '\n')
    print('models %d (with a physics asset %d), physics assets %d: %s; %d triangles' %
          (len(models), sum(1 for pa, dc in models.values() if pa), len(physics), dict(stats), triangles))
    print('wrote', OUT)


main()
