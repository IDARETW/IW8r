import io

# core face_edges: the coplanar-cover and hidden-face rules rejected a neighbour by comparing bounds exactly. A face's
# bounds come from its winding (plane intersections), so a face in z = 144 could measure 143.99999999999997 and a
# brush standing on that plane 144.00000000000003: the neighbour was dropped and both faces were drawn (Cinder Rail:
# the undersides of two overlapping roof slabs). The comparison now has the slack the edge search already had.

P = 'E:/IW8-Radiant/src/core/face_edges.cpp'
s = io.open(P, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
old = ('        if (C.bounds.mins.x > ab.maxs.x || C.bounds.maxs.x < ab.mins.x || C.bounds.mins.y > ab.maxs.y || C.bounds.maxs.y < ab.mins.y ||' + nl +
       '            C.bounds.mins.z > ab.maxs.z || C.bounds.maxs.z < ab.mins.z)' + nl +
       '            continue;')
new = ('        // With slack: bounds come from windings, and two faces of one plane can differ in the last bits.' + nl +
       '        if (C.bounds.mins.x > ab.maxs.x + kBoundsSlack || C.bounds.maxs.x < ab.mins.x - kBoundsSlack || C.bounds.mins.y > ab.maxs.y + kBoundsSlack ||' + nl +
       '            C.bounds.maxs.y < ab.mins.y - kBoundsSlack || C.bounds.mins.z > ab.maxs.z + kBoundsSlack || C.bounds.maxs.z < ab.mins.z - kBoundsSlack)' + nl +
       '            continue;')
assert s.count(old) == 2, s.count(old)
s = s.replace(old, new)
anchor = 'constexpr double kMinSpan = 0.25;'
assert s.count(anchor) == 1
line_end = s.index(nl, s.index(anchor))
s = s[:line_end] + nl + 'constexpr double kBoundsSlack = 0.1;  // world units: bounds this close count as touching' + s[line_end:]
io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('ok')
