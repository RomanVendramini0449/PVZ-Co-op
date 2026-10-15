"""Make the 9x5 planting area of an Adventure Plus board read as ground that
varies a little from tile to tile -- the way the PvZ lawn alternates two
shades of grass -- without drawing a checkerboard on it.

    python add_tiles.py <in.png|webp> <out.png> [--night]

The lawn grid is 9 columns x 5 rows of 80x100 tiles from (260, 80). Nothing
is outlined. Alternate tiles are nudged slightly lighter/warmer or
darker/cooler, each tile drifts a little on its own, and the boundaries are
feathered wide and warped with noise, so the change is felt more than seen
-- like patches of packed and loose dirt.
"""
import sys
import numpy as np
from PIL import Image, ImageFilter

X0, Y0, COLS, ROWS, TW, TH = 260, 80, 9, 5, 80, 100
FEATHER = 9          # px: how softly one tile's shade melts into the next
WARP = 7             # px: how far the boundaries wander from straight lines


def blur(a, r):
    # Separable Gaussian in float, edges clamped.
    radius = int(r * 3)
    k = np.exp(-0.5 * (np.arange(-radius, radius + 1) / r) ** 2).astype(np.float32)
    k /= k.sum()
    p = np.pad(a, ((0, 0), (radius, radius)), mode='edge')
    a = sum(k[i] * p[:, i:i + a.shape[1]] for i in range(len(k)))
    p = np.pad(a, ((radius, radius), (0, 0)), mode='edge')
    return sum(k[i] * p[i:i + a.shape[0], :] for i in range(len(k))).astype(np.float32)


def smooth_noise(rng, H, W, r):
    n = blur(rng.random((H, W)).astype(np.float32), r)
    n = (n - n.mean()) / (n.std() + 1e-6)
    return n


def main(src, dst, night):
    rng = np.random.default_rng(1347)
    img = np.asarray(Image.open(src).convert('RGB')).astype(np.float32) / 255
    H, W, _ = img.shape
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)

    # Wander the tile boundaries so no edge is ruler-straight.
    wx = xx + smooth_noise(rng, H, W, 18) * WARP
    wy = yy + smooth_noise(rng, H, W, 18) * WARP
    col = np.floor((wx - X0) / TW).astype(int)
    row = np.floor((wy - Y0) / TH).astype(int)
    inside = (col >= 0) & (col < COLS) & (row >= 0) & (row < ROWS)
    col = np.clip(col, 0, COLS - 1)
    row = np.clip(row, 0, ROWS - 1)

    # Two kinds of ground, alternating, plus a little drift per tile.
    parity = np.where((row + col) % 2 == 0, 1.0, -1.0).astype(np.float32)
    drift = rng.normal(0, 0.012, (ROWS, COLS)).astype(np.float32)
    strength = 0.048 if not night else 0.042
    light = np.where(inside, parity * strength + drift[row, col], 0).astype(np.float32)
    warm = np.where(inside, parity, 0).astype(np.float32)
    light = blur(light, FEATHER)
    warm = blur(warm, FEATHER)

    # Mottle inside each tile so it's never a flat patch.
    mottle = smooth_noise(rng, H, W, 10) * 0.012

    # Brightness, and a small warm/cool shift: the lighter ground a touch
    # warmer, the darker a touch cooler, as packed and loose dirt differ.
    hue = np.array([1.0, 0.55, -0.35], np.float32) * (0.010 if not night else 0.008)
    out = img * (1 + light[..., None] + mottle[..., None] * (np.abs(warm[..., None]) > 0.05))
    out = out + warm[..., None] * hue

    # Fade the whole effect in from the grid's edge.
    edge = blur(inside.astype(np.float32), 12)[..., None]
    out = img * (1 - edge) + out * edge
    Image.fromarray(np.clip(out * 255, 0, 255).astype(np.uint8)).save(dst)
    print('wrote', dst)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], '--night' in sys.argv)
