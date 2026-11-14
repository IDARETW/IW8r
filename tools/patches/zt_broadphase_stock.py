import io

# Converter (x64-zt worktree): the physics broadphase of a built map is at least as large as a stock map's
# (IW8 Radiant research 124). Stock small maps all store the same box, (-30720, -31744, -14336) ..
# (34816, 33792, 51200) (mp_shipment, mp_m_speedball; mp_rust wider in x); a built map stored its geometry padded by
# 16384 / 8192, which ends about 9,000 units up: a body that leaves the broadphase is frozen in place, and aircraft
# of the killstreaks fly higher and start further out than that.

ROOT = 'C:/zsw-iw8r/scriptables/'
p = 'src/games/iw8/zone/map_zone.h'
s = io.open(ROOT + p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
a = '''                       const float pad = k < 2 ? 16384.f : 8192.f;
                       stampf(cm, kCM_bpMin + (size_t)k * 4, bounds.valid ? bounds.mn[k] - pad : -100000.f);
                       stampf(cm, kCM_bpMax + (size_t)k * 4, bounds.valid ? bounds.mx[k] + pad : 100000.f);'''.replace('\n', nl)
b = '''                       const float pad = k < 2 ? 16384.f : 8192.f;
                       // And never smaller than the box every small shipped map stores (mp_shipment,
                       // mp_m_speedball: -30720 -31744 -14336 .. 34816 33792 51200; IW8 Radiant research
                       // 124): killstreak aircraft fly above and start outside a small map's padded box.
                       static const float stockMin[3] = {-30720.f, -31744.f, -14336.f};
                       static const float stockMax[3] = {34816.f, 33792.f, 51200.f};
                       stampf(cm, kCM_bpMin + (size_t)k * 4, bounds.valid ? std::min(bounds.mn[k] - pad, stockMin[k]) : -100000.f);
                       stampf(cm, kCM_bpMax + (size_t)k * 4, bounds.valid ? std::max(bounds.mx[k] + pad, stockMax[k]) : 100000.f);'''.replace('\n', nl)
assert s.count(a) == 1, s.count(a)
s = s.replace(a, b)
io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
