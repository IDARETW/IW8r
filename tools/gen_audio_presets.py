#!/usr/bin/env python3
"""Generate src/core/audio_presets.inc (roadmap 6.22, research 63 section C).

Input: an ACTS structured export of the stock bank mp_shipment.all:
  set ACTS_REPLAY_STRUCTURED=1
  acts.exe --noUpdater fastfile -r mw19replay -g <replay exe> --oodle <oo2core dll> -a soundbank -o <out> mp_shipment.ff
Channel names come from x64-zt src/games/iw8/assets/replay_sound.cpp (kEntChannels).
Usage: py -3 -I tools/gen_audio_presets.py <out>/mw19replay/mp_shipment/assets/soundbank/mp_shipment.all.0.asset.json
"""
import json, re, sys

def H(n):
    h = 5381
    for c in n.lower():
        o = ord(c)
        h = (65599 * h + (o if o < 128 else o - 256)) & 0xffffffff
    return h or 1

src = open('E:/mw124/x64-zt/src/games/iw8/assets/replay_sound.cpp', encoding='utf-8').read()
i = src.index('constexpr std::array<std::string_view, 177> kEntChannels{')
j = src.index('};', i)
chans = re.findall(r'"([^"]*)"', src[i:j])
assert len(chans) == 177, len(chans)

bank = sys.argv[1]
d = json.load(open(bank))
t = d['asset']['fields']['bank']['soundTable']
zs = t['zones']['values']
occ = t['occlusionFilters']['values']
fil = t['filters']['values']

def rng(kind_start, kind_num, name_key, want):
    for z in zs:
        if (z[name_key] or {}).get('string') == want:
            return z[kind_start], z[kind_num]
    raise SystemExit('no ' + want)

out = ['// Generated from an ACTS export of the stock bank mp_shipment.all (2026-11-11) and x64-zt kEntChannels.',
       '// Occlusion filters: channel, cutoff 1..4, attenuation 1..4. Filters: channel, band, type, freq, gain, q.']
for preset in ('default', 'interior'):
    s, n = rng('startOcclusionIndex', 'numOcclusion', 'occlusionName', preset)
    ids = {occ[k]['id'] for k in range(s, s + n)}
    out.append(f'// occlusion "{preset}": {n} rows, id {sorted(ids)} (H={H(preset)})')
    out.append(f'static const OcclusionRow kOcclusion_{preset}[] = {{')
    for k in range(s, s + n):
        r = occ[k]
        out.append('    {"%s", %.9g, %.9g, %.9g, %.9g, %.9g, %.9g, %.9g, %.9g},' % (
            chans[r['entChannelIdx']], r['cutoffLevel1'], r['cutoffLevel2'], r['cutoffLevel3'], r['cutoffLevel4'],
            r['attnLevel1'], r['attnLevel2'], r['attnLevel3'], r['attnLevel4']))
    out.append('};')
for preset in ('default', 'interior'):
    s, n = rng('startFilterIndex', 'numFilter', 'filterName', preset)
    ids = {fil[k]['id'] for k in range(s, s + n)}
    out.append(f'// filter "{preset}": {n} rows, id {sorted(ids)} (H={H(preset)})')
    out.append(f'static const FilterRow kFilter_{preset}[] = {{')
    for k in range(s, s + n):
        r = fil[k]
        out.append('    {"%s", %d, %d, %.9g, %.9g, %.9g},' % (chans[r['entChannelIdx']], r['band'], (r['type']['value'] if isinstance(r['type'],dict) else r['type']), r['freq'], r['gain'], r['q']))
    out.append('};')
open('E:/IW8-Radiant/src/core/audio_presets.inc', 'w', encoding='utf-8', newline='\n').write('\n'.join(out) + '\n')
print('ok', [l for l in out if l.startswith('// occlusion') or l.startswith('// filter')])
print('filter keys', list(fil[0].keys()))
