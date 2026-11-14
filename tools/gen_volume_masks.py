"""Makes the mask pictures the editor ships for volumetric boxes (data/volume_masks): tileable cloud patterns.

clouds_soft.png: wide soft patches (a drifting mist layer); clouds_hard.png: smaller patches with clearer gaps.
Both are 256 x 256 grey pictures that repeat without a seam (periodic value noise, several octaves). A mask thins a
box's fog by the picture's brightness: white keeps the fog, black removes it.

Usage: py -3 -I tools/gen_volume_masks.py     (writes into data/volume_masks; the result is the same every time)"""
import math
import os
import random
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'data', 'volume_masks')
SIZE = 256


def lattice(period, seed):
    rnd = random.Random(seed)
    return [[rnd.random() for _ in range(period)] for _ in range(period)]


def smooth(t):
    return t * t * (3 - 2 * t)


def noise(grid, period, x, y):
    """Periodic value noise: x, y in lattice units."""
    x0, y0 = int(math.floor(x)), int(math.floor(y))
    fx, fy = smooth(x - x0), smooth(y - y0)
    a = grid[y0 % period][x0 % period]
    b = grid[y0 % period][(x0 + 1) % period]
    c = grid[(y0 + 1) % period][x0 % period]
    d = grid[(y0 + 1) % period][(x0 + 1) % period]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def clouds(seed, base, octaves, low, high):
    """fBm over periods base, 2 base, ...; then the range low..high stretched to black..white."""
    grids = [(base << o, lattice(base << o, seed * 31 + o)) for o in range(octaves)]
    rows = []
    for y in range(SIZE):
        row = bytearray()
        for x in range(SIZE):
            v, amp, total = 0.0, 1.0, 0.0
            for period, grid in grids:
                v += amp * noise(grid, period, x * period / SIZE, y * period / SIZE)
                total += amp
                amp *= 0.5
            v /= total
            v = min(1.0, max(0.0, (v - low) / (high - low)))
            row.append(int(round(smooth(v) * 255)))
        rows.append(bytes(row))
    return rows


def png(path, rows):
    raw = b''.join(b'\x00' + r for r in rows)

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', SIZE, SIZE, 8, 0, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


os.makedirs(OUT, exist_ok=True)
png(os.path.join(OUT, 'clouds_soft.png'), clouds(7, 3, 4, 0.30, 0.70))
png(os.path.join(OUT, 'clouds_hard.png'), clouds(19, 5, 5, 0.42, 0.60))
print('wrote', OUT)
