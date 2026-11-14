"""Mean luminance of 8 x 4 blocks of a reference picture and of one or more viewport pictures, and the error between them.
Usage: py -3 -I tools/compare_blocks.py <reference image> <picture> [<picture> ...]   (all are scaled to 1920 x 1080)"""
import sys
from PIL import Image
import numpy as np
g = np.asarray(Image.open(sys.argv.pop(1)).convert('RGB').resize((1920, 1080))).astype(float)
def lum(a): return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
G = lum(g)
rows, cols = 4, 8
def blocks(L): return np.array([[L[r * 270:(r + 1) * 270, c * 240:(c + 1) * 240].mean() for c in range(cols)] for r in range(rows)])
GB = blocks(G)
print('game  ', np.round(GB).astype(int).tolist())
for name in sys.argv[1:]:
    v = np.asarray(Image.open(name).convert('RGB')).astype(float)
    VB = blocks(lum(v))
    err = np.abs(VB - GB)
    print(name.split('/')[-2], np.round(VB).astype(int).tolist())
    print('   mean abs error %.1f  (top row %.1f, rows 2-4 %.1f)  mean ratio %.3f' % (err.mean(), err[0].mean(), err[1:].mean(), (VB / GB).mean()))
