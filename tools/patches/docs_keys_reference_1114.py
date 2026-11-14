import io

# docs/KEYS_REFERENCE.md: the seven keys of the entity definitions that had no row (iw8r_volumetric, iw8r_hinge,
# iw8r_airspace, iw8r_haze, iw8r_sky_brightness, iw8r_mount_edges).

p = 'E:/IW8-Radiant/docs/KEYS_REFERENCE.md'
s = io.open(p, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
lines = s.split(nl)


def after(prefix, new_rows):
    hits = [i for i, l in enumerate(lines) if l.startswith(prefix)]
    assert len(hits) == 1, (prefix, len(hits))
    for k, row in enumerate(new_rows):
        lines.insert(hits[0] + 1 + k, row)


after('| `iw8r_ir_intensity` |', [
    '| `iw8r_volumetric` | 0 or 1 (default 0) | `1` | 1 makes the lamp light the volumetric air: a glow around it and beams in fog. It shows only when the map has volumetric air (worldspawn `iw8r_haze`, or `volumetricAirDensity` in `iw8r_vision`). Not seen in game. |'])
after('| `iw8r_door` |', [
    '| `iw8r_hinge` | `far` or `x y z` | `far` or `128 64 0` | Where a hinged door (`iw8r_door rotate`) turns. Without it the hinge is the end of the door\'s longer side with the lower coordinate. `far` takes the other end; `x y z` names the hinge point itself. |'])
after('| `iw8r_sun_sprite` |', [
    '| `iw8r_airspace` | `<up> [out]` in units (default `131072 131072`), or `0` | `16384 8192` | How far the map\'s visibility cell and occlusion data reach above (first number) and around and below (second number) the map\'s geometry. Aircraft killstreaks and their cameras fly there; from a camera outside it the game draws no players or vehicles. `0` keeps the tight cell. |',
    '| `iw8r_haze` | `<preset> [amount]`; presets `desert`, `clear`, `overcast`, `dusk`, `fog`; amount 0.1 to 4 (default 1) | `desert 0.7` | The game\'s own fog, sky blend and volumetric air for a built map (Compile > Atmosphere). The amount scales how thick it is. `iw8r_fog` is left out of the map script when a haze is set; fields in `iw8r_vision` win over the preset. Seen in game 2026-11-14. |',
    '| `iw8r_sky_brightness` | float 0.1 to 16 (default 1) | `3` | Brightness of the built sky (Compile > Sun & Sky). 2 doubles it (its brightest parts go to white), 0.5 halves it. Seen in game 2026-11-14. |',
    '| `iw8r_mount_edges` | 0 or 1 (default 1) | `0` | Mount edges: where a player can rest a weapon on cover (open outside wall corners at least 32 units tall and ledge tops at least 16 units long of the map\'s visible solid brushes). 0 builds the map without them. Not seen in game. |'])
s = nl.join(lines)


def rep(a, b):
    global s
    assert s.count(a) == 1, (s.count(a), a[:60])
    s = s.replace(a, b)


rep('- 59 key rows in the entity tables above (light and light_spot share one table of 9 keys)', '- 65 key rows in the entity tables above (light and light_spot share one table of 10 keys)')
rep('- 41 distinct key names: 39 `iw8r_` keys', '- 47 distinct key names: 45 `iw8r_` keys')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ok')
