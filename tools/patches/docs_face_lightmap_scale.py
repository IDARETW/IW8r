import io

# Docs for the per-face lightmap scale in the bake (plan 1.11, 2026-11-14).

ROOT = 'E:/IW8-Radiant/'


def read(p):
    return io.open(ROOT + p, encoding='utf-8', newline='').read()


def write(p, s):
    io.open(ROOT + p, 'w', encoding='utf-8', newline='').write(s)


def patch(p, a, b):
    s = read(p)
    nl = '\r\n' if '\r\n' in s else '\n'
    a = a.replace('\n', nl); b = b.replace('\n', nl)
    assert s.count(a) == 1, (p, s.count(a), a[:70])
    write(p, s.replace(a, b))


patch('CHANGELOG.md', '''## 2026-11-14

''', '''## 2026-11-14

- **The lightmap scale of a face now does something.** The Surface panel (and the web page's Surface form) has had
  "Lightmap scale" fields, but the bake gave every face the same texel size. Now a face with half the scale (8192
  instead of 16384) gets twice the lightmap texels along each side, so its shadows are sharper; a larger scale makes
  a face coarser and cheaper. The range is one eighth to eight times the map's texel size (Bake Settings). The
  editor makes the lightmap and its coordinates itself, so the build tool did not have to change. View > Game
  Lighting previews it. The build log counts the triangles with their own scale. In game: not checked.

''')
patch('docs/PROGRESS.md', '''  The web list of plan 3.3 / 3.4 is done (the Vision Editor form closed it).''', '''  Per-face lightmap scale: core lightmap_fields lightmapDensityOfSuffix (16384 / sqrt(L1 x L2), 1/8 .. 8),
  BakeSurface::triDensity + Chart::density (charts never mix densities; projected x density / texel in the three
  places), compile_iw8.cpp Chunk::lmDensity -> RenderSurface::triDensity, viewport PrimGeom::polyLmDensity ->
  GameBakeInput::Poly::density. Tests: lightmap_bake_face_density, lightmap_density_of_suffix,
  compile_lightmap_bake_honours_face_lightmap_scale.
  The web list of plan 3.3 / 3.4 is done (the Vision Editor form closed it).''')
P = 'docs/PLAN_REMAINING.md'
s = read(P)
nl = '\r\n' if '\r\n' in s else '\n'
a = 'per-face lightmap scale, HDR probes, the parity proof.'
assert s.count(a) == 1
s = s.replace(a, 'HDR probes, the parity proof. The per-face lightmap scale is DONE 2026-11-14 (in the bake itself; no converter change).')
start = s.index('| 1.11 a per-face lightmap scale, HDR probe capture |')
end = s.index(nl, start) + len(nl)
s = s[:start] + ('| 1.11 HDR probe capture | Bounces in the surfaces\' colours with the later bounces estimated, a lightmap scale per face, the texel size as a Bake Setting (all 2026-11-14), 8-bit probes captured before every build | HDR probes change glossy reflections by a little and need the converter to take another image format. |' + nl) + s[end:]
write(P, s)
patch('docs/ROADMAP.md', '''A GPU bake and a per-face lightmap scale in the build are not built''',
      '''A face's lightmap scale is honoured by the bake since 2026-11-14 (half the scale = twice the texels). A GPU bake is not built''')
patch('docs/USER_GUIDE.md', '''- **What the bake bounces.**''', '''- **Sharper shadows where you want them.** Select faces or brushes and lower "Lightmap scale" in the Surface panel
  (16384 is the default; 8192 gives twice the lightmap texels, 4096 four times, down to an eighth of the map's texel
  size). Raise it on big plain faces to save lightmap space.
- **What the bake bounces.**''')
print('ok')
