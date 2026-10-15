"""Assemble a PvZ zombie from its parts, the way the game does, so it can be
repainted as one picture -- and cut a repainted picture back into parts.

    python render_zombie.py export <main.pak> <reanim name> <out dir> [--anim anim_idle] [--hide prefix,prefix]
    python render_zombie.py split  <out dir> <repainted layers dir> <parts out dir>

export writes, for one frame of one animation:
    full.png          the zombie as it stands in that frame
    layers/NN_<part>.png   every part on its own full-size layer, in draw order
    layout.json       where each part sits (its transform), for split
split takes repainted layers (same names, same canvas size) and turns each
back into a part image at the original part's size, for assets/coop/zombie-parts.
"""
import io, json, math, os, re, struct, sys
from PIL import Image

MARGIN = 40


def read_pak(path):
    data = bytes(b ^ 0xF7 for b in open(path, 'rb').read())
    pos = 8
    entries = []
    while True:
        flag = data[pos]; pos += 1
        if flag & 0x80:
            break
        n = data[pos]; pos += 1
        name = data[pos:pos + n].decode('latin1'); pos += n
        size = struct.unpack_from('<I', data, pos)[0]; pos += 4 + 8
        entries.append((name, size))
    files = {}
    off = pos
    for name, size in entries:
        files[name.replace('\\', '/').lower()] = (off, size)
        off += size
    return data, files


def parse_reanim(text):
    tracks = []
    for m in re.finditer(r'<track>(.*?)</track>', text, re.S):
        body = m.group(1)
        name = re.search(r'<name>(.*?)</name>', body).group(1)
        frames = []
        cur = {'x': 0.0, 'y': 0.0, 'kx': 0.0, 'ky': 0.0, 'sx': 1.0, 'sy': 1.0, 'f': 0.0, 'a': 1.0, 'i': None}
        for t in re.finditer(r'<t>(.*?)</t>', body, re.S):
            for key, val in re.findall(r'<(\w+)>(.*?)</\1>', t.group(1)):
                if key == 'i':
                    cur['i'] = val.strip()
                elif key in cur:
                    cur[key] = float(val)
            frames.append(dict(cur))
        tracks.append((name, frames))
    return tracks


def part_image(data, files, image_id):
    stem = image_id[len('IMAGE_REANIM_'):].lower()
    for folder in ('images/reanim/', 'reanim/', 'images/'):
        for ext in ('.png', '.jpg'):
            key = folder + stem + ext
            if key in files:
                off, size = files[key]
                im = Image.open(io.BytesIO(data[off:off + size])).convert('RGBA')
                if ext == '.jpg':
                    akey = folder + stem + '_.png'
                    if akey in files:
                        o2, s2 = files[akey]
                        a = Image.open(io.BytesIO(data[o2:o2 + s2])).convert('L')
                        if a.size == im.size:
                            im.putalpha(a)
                return stem, im
    return stem, None


def matrix(fr):
    kx = -math.radians(fr['kx']); ky = -math.radians(fr['ky'])
    m00 = math.cos(kx) * fr['sx']; m10 = -math.sin(kx) * fr['sx']
    m01 = math.sin(ky) * fr['sy']; m11 = math.cos(ky) * fr['sy']
    return (m00, m01, fr['x'], m10, m11, fr['y'])


def apply(m, u, v):
    return (m[0] * u + m[1] * v + m[2], m[3] * u + m[4] * v + m[5])


def inverse(m):
    a, b, c, d, e, f = m
    det = a * e - b * d
    ia, ib, id_, ie = e / det, -b / det, -d / det, a / det
    return (ia, ib, -(ia * c + ib * f), id_, ie, -(id_ * c + ie * f))


def export(pak, reanim_name, out_dir, anim, hide):
    data, files = read_pak(pak)
    off, size = files['reanim/' + reanim_name.lower() + '.reanim']
    tracks = parse_reanim(data[off:off + size].decode('latin1'))
    marker = next((fr for n, fr in tracks if n == anim), None)
    if marker is None:
        sys.exit('no track %s; tracks: %s' % (anim, [n for n, _ in tracks if n.startswith('anim_')]))
    frame = next(i for i, fr in enumerate(marker) if fr['f'] >= 0)

    parts = []
    for name, frames in tracks:
        # Tracks named anim_* are mostly markers with no image, but a few --
        # the head, the hair, the cone -- are parts; skip only the imageless.
        if frame >= len(frames):
            continue
        if any(name.lower().startswith(h.lower()) for h in hide):
            continue
        fr = frames[frame]
        if fr['f'] < 0 or not fr['i'] or fr['a'] <= 0:
            continue
        stem, im = part_image(data, files, fr['i'])
        if im is None:
            continue
        parts.append((name, stem, im, matrix(fr), fr['a']))

    xs, ys = [], []
    for _, _, im, m, _ in parts:
        for u, v in ((0, 0), (im.width, 0), (0, im.height), (im.width, im.height)):
            x, y = apply(m, u, v); xs.append(x); ys.append(y)
    ox, oy = math.floor(min(xs)) - MARGIN, math.floor(min(ys)) - MARGIN
    W, H = math.ceil(max(xs)) - ox + MARGIN, math.ceil(max(ys)) - oy + MARGIN

    os.makedirs(os.path.join(out_dir, 'layers'), exist_ok=True)
    full = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    layout = {'canvas': [W, H], 'origin': [ox, oy], 'anim': anim, 'frame': frame, 'parts': []}
    for n, (track, stem, im, m, alpha) in enumerate(parts):
        placed = (m[0], m[1], m[2] - ox, m[3], m[4], m[5] - oy)
        inv = inverse(placed)
        layer = im.transform((W, H), Image.AFFINE, inv, resample=Image.BICUBIC)
        if alpha < 1:
            a = layer.split()[3].point(lambda p: int(p * alpha))
            layer.putalpha(a)
        fname = '%02d_%s.png' % (n, stem)
        layer.save(os.path.join(out_dir, 'layers', fname))
        full.alpha_composite(layer)
        layout['parts'].append({'layer': fname, 'part': stem, 'track': track,
                                'size': [im.width, im.height], 'matrix': placed})
    full.save(os.path.join(out_dir, 'full.png'))
    json.dump(layout, open(os.path.join(out_dir, 'layout.json'), 'w'), indent=1)
    print('%s: %d parts, %dx%d' % (reanim_name, len(parts), W, H))


def split(out_dir, layers_dir, parts_out):
    layout = json.load(open(os.path.join(out_dir, 'layout.json')))
    os.makedirs(parts_out, exist_ok=True)
    done = set()
    for p in layout['parts']:
        path = os.path.join(layers_dir, p['layer'])
        if not os.path.exists(path) or p['part'] in done:
            continue
        layer = Image.open(path).convert('RGBA')
        w, h = p['size']
        part = layer.transform((w, h), Image.AFFINE, tuple(p['matrix']), resample=Image.BICUBIC)
        part.save(os.path.join(parts_out, p['part'] + '.png'))
        done.add(p['part'])
    print('wrote %d parts to %s' % (len(done), parts_out))


if __name__ == '__main__':
    if sys.argv[1] == 'export':
        anim = 'anim_idle'; hide = []
        args = sys.argv[2:]
        if '--anim' in args:
            i = args.index('--anim'); anim = args[i + 1]; del args[i:i + 2]
        if '--hide' in args:
            i = args.index('--hide'); hide = args[i + 1].split(','); del args[i:i + 2]
        export(args[0], args[1], args[2], anim, hide)
    elif sys.argv[1] == 'split':
        split(sys.argv[2], sys.argv[3], sys.argv[4])
