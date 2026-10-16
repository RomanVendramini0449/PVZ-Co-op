"""Fit a new character into Crazy Dave's parts, so the game animates them
with Dave's own animation, and render previews of that animation.

    python fit_character.py <config.json> <character.png> <dave export dir> <main.pak> <parts out> <preview out>

The character must be painted on the canvas of the Dave export (full.png's
size, same position), roughly in his pose. config.json names, for each Dave
part, a polygon on that canvas that belongs to it (first match wins), which
parts to leave empty, and where the character's mouth and eyes are, for the
talking mouths and the blink.

Each part is written as "<part>@<x>,<y>.png": its picture at true size, with
its top-left at (x, y) in the original part's pixel frame -- so a part can be
bigger than Dave's (hair, a raised arm) without being squeezed. Holes left
in the torso where the head, arms and hands were cut away are filled from
the surrounding shirt, so nothing ghosts there when they move.
"""
import json, math, os, sys
import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(__file__))
import render_zombie as rz


def track_matrices(tracks, frame, origin):
    out = {}
    for name, frames in tracks:
        if frame >= len(frames):
            continue
        fr = frames[frame]
        m = rz.matrix(fr)
        out[name] = (fr, (m[0], m[1], m[2] - origin[0], m[3], m[4], m[5] - origin[1]))
    return out


def cut(canvas_rgba, mask, m):
    """The masked pixels as a part image in the frame of track matrix m
    (part -> canvas): returns (image, ox, oy)."""
    ys, xs = np.nonzero(mask)
    if len(xs) == 0:
        return None
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    inv = rz.inverse(m)
    corners = [rz.apply(inv, x, y) for x, y in ((x0, y0), (x1, y0), (x0, y1), (x1, y1))]
    u0 = math.floor(min(c[0] for c in corners)) - 2
    v0 = math.floor(min(c[1] for c in corners)) - 2
    u1 = math.ceil(max(c[0] for c in corners)) + 2
    v1 = math.ceil(max(c[1] for c in corners)) + 2
    masked = canvas_rgba.copy()
    masked[..., 3] = (masked[..., 3].astype(np.float32) * (mask > 0)).astype(np.uint8)
    img = Image.fromarray(masked)
    # part pixel (i, j) -> canvas: m * (u0 + i, v0 + j)
    a, b, c, d, e, f = m
    coeffs = (a, b, a * u0 + b * v0 + c, d, e, d * u0 + e * v0 + f)
    part = img.transform((u1 - u0, v1 - v0), Image.AFFINE, coeffs, resample=Image.BICUBIC)
    return part, u0, v0


def poly_mask(shape, pts):
    m = np.zeros(shape, np.uint8)
    cv2.fillPoly(m, [np.array(pts, np.int32)], 255)
    return m


def main(cfg_path, char_path, dave_dir, pak, parts_out, preview_out):
    cfg = json.load(open(cfg_path))
    layout = json.load(open(os.path.join(dave_dir, 'layout.json')))
    origin = layout['origin']
    W, H = layout['canvas']
    char = Image.open(char_path).convert('RGBA')
    assert char.size == (W, H), 'character must be painted on the %dx%d Dave canvas' % (W, H)
    rgba = np.array(char)
    alpha = rgba[..., 3]
    present = alpha > 8

    data, files = rz.read_pak(pak)
    off, size = files['reanim/crazydave.reanim']
    tracks = rz.parse_reanim(data[off:off + size].decode('latin1'))
    idle = track_matrices(tracks, layout['frame'], origin)

    # ---- who owns each pixel ----
    owner = np.full((H, W), '', object)
    masks = {}
    for region in cfg['regions']:
        m = poly_mask((H, W), region['polygon']) > 0
        m &= present & (owner == '')
        owner[m] = region['part']
        masks[region['part']] = masks.get(region['part'], np.zeros((H, W), bool)) | m
    # Some parts are only their skin: a hand lying on a printed shirt takes
    # its fingers, not the print showing between them.
    r, g, b = (rgba[..., i].astype(np.int32) for i in range(3))
    skin = (r > 90) & (g > 50) & (b > 35) & (r > g) & (r > b) & ((r - g) > 10) & ((r - g) < 80) & ((r - b) < 125)
    k = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5))
    skin = cv2.morphologyEx(skin.astype(np.uint8), cv2.MORPH_CLOSE, k) > 0
    for part in cfg.get('skin_only', []):
        if part in masks:
            keep = masks[part] & skin
            owner[masks[part] & ~keep] = ''
            masks[part] = keep
    # A part can give its dark pixels below a line back to the body -- the
    # collar under a chin belongs to the shirt, or it bobs with the head.
    luma = 0.299 * r + 0.587 * g + 0.114 * b
    for trim in cfg.get('trim_dark', []):
        part = trim['part']
        if part in masks:
            yy = np.arange(H)[:, None]
            dark = (luma < trim['max_luma']) & (yy > trim['below_y'])
            dark = cv2.morphologyEx(dark.astype(np.uint8), cv2.MORPH_OPEN, np.ones((3, 3), np.uint8)) > 0
            give = masks[part] & dark
            owner[give] = ''
            masks[part] = masks[part] & ~give
    rest = present & (owner == '')
    masks[cfg['leftover']] = masks.get(cfg['leftover'], np.zeros((H, W), bool)) | rest

    # ---- fill the torso where moving parts were cut out ----
    # Moving parts are cut from the untouched picture; only the parts they
    # sat on take the filled-in copy.
    original = rgba.copy()
    filled_parts = set()
    for fill in cfg.get('fill_behind', []):
        target = fill['into']
        hole = np.zeros((H, W), np.uint8)
        for p in fill['from']:
            hole |= masks[p].astype(np.uint8)
        inside = poly_mask((H, W), fill['within']) > 0
        hole = (hole > 0) & inside
        if not hole.any():
            continue
        body = masks[target]
        src = rgba[..., :3].copy()
        known = body & ~hole
        filled = cv2.inpaint(cv2.cvtColor(src, cv2.COLOR_RGB2BGR), ((~known) & (hole | ~present)).astype(np.uint8) * 255,
                             9, cv2.INPAINT_TELEA)
        filled = cv2.cvtColor(filled, cv2.COLOR_BGR2RGB)
        rgba[hole, :3] = filled[hole]
        rgba[hole, 3] = 255
        masks[target] = body | hole
        filled_parts.add(target)

    # Parts that move overlap their neighbours a little, so a joint never
    # opens a gap: grow each moving part into the pixels around it.
    for part, grow in cfg.get('grow', {}).items():
        k = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (2 * grow + 1, 2 * grow + 1))
        masks[part] = masks[part] | ((cv2.dilate(masks[part].astype(np.uint8), k) > 0) & (original[..., 3] > 200))

    os.makedirs(parts_out, exist_ok=True)
    for f in os.listdir(parts_out):
        if f.endswith('.png'):
            os.remove(os.path.join(parts_out, f))
    subs = {}

    def write(part, img, ox, oy, anchor=None):
        name = '%s@%d,%d%s.png' % (part, ox, oy, ('~' + anchor) if anchor else '')
        img.save(os.path.join(parts_out, name))
        subs[part] = (img, ox, oy, anchor)

    track_of = {}
    for tname, frames in tracks:
        for fr in frames:
            if fr['i']:
                track_of.setdefault(fr['i'][len('IMAGE_REANIM_'):].lower(), tname)

    for part, mask in masks.items():
        tname = track_of[part]
        res = cut(rgba if part in filled_parts else original, mask.astype(np.uint8) * 255, idle[tname][1])
        if res:
            write(part, *res)

    blank = Image.new('RGBA', (1, 1), (0, 0, 0, 0))
    for part in cfg['empty']:
        write(part, blank, 0, 0)

    # ---- mouths and blinks: drawn on his face and anchored to the head
    # track, so they move exactly with it. Dave's animation still decides
    # which mouth shows when, and when he blinks.
    head_track = track_of['crazydave_head']
    head_idle = idle[head_track][1]

    def on_face(part, draw_fn):
        layer = Image.new('RGBA', (W, H), (0, 0, 0, 0))
        draw_fn(ImageDraw.Draw(layer))
        arr = np.array(layer)
        res = cut(arr, (arr[..., 3] > 0).astype(np.uint8) * 255, head_idle)
        if res:
            write(part, *res, anchor=head_track)
        else:
            write(part, blank, 0, 0)

    mouth = cfg['mouth']
    for part, (rw, rh, kind) in mouth['shapes'].items():
        if rw <= 0:
            write(part, blank, 0, 0)  # closed: his own mouth, on the head
            continue

        def draw_mouth(d, rw=rw, rh=rh, kind=kind):
            cx, cy = mouth['center']
            d.ellipse([cx - rw - 2, cy - rh - 2, cx + rw + 2, cy + rh + 2], fill=tuple(mouth['lip']) + (255,))
            d.ellipse([cx - rw, cy - rh, cx + rw, cy + rh], fill=(45, 12, 14, 255))
            if kind == 'teeth':
                d.rectangle([cx - rw + 3, cy - rh, cx + rw - 3, cy - rh + max(2, rh // 3)], fill=(235, 230, 220, 255))
        on_face(part, draw_mouth)

    # The blink is its own copy of Dave's animation, attached to his head
    # while he blinks (LawnApp::UpdateCrazyDave), so it already follows the
    # head: the lids go in the blink track's own frame, at his eyes as they
    # sit relative to the head in the blink's frames.
    tdict = dict(tracks)
    blink_track = track_of['crazydave_blink1']
    bf = next(i for i, fr in enumerate(tdict['anim_blink']) if fr['f'] >= 0) + 2
    def at_frame(tname):
        m = rz.matrix(tdict[tname][bf])
        return (m[0], m[1], m[2] - origin[0], m[3], m[4], m[5] - origin[1])
    head_at_blink = at_frame(head_track)
    eyes = []
    for (cx, cy, rw, rh) in cfg['eyes']:
        u, v = rz.apply(rz.inverse(head_idle), cx, cy)
        x, y = rz.apply(head_at_blink, u, v)
        eyes.append((x, y, rw, rh))
    # Each lid takes the colour of the skin just under that eye (above it is
    # usually eyebrow).
    lid_colors = []
    for (cx, cy, rw, rh) in cfg['eyes']:
        patch = original[int(cy + rh + 3):int(cy + rh + 9), int(cx - rw * 0.6):int(cx + rw * 0.6), :3].reshape(-1, 3)
        lid_colors.append(tuple(int(v) for v in np.median(patch, axis=0)) if len(patch) else tuple(cfg['skin']))
    for part, closed in (('crazydave_blink1', 0.55), ('crazydave_blink2', 1.0)):
        layer = Image.new('RGBA', (W, H), (0, 0, 0, 0))
        d = ImageDraw.Draw(layer)
        for (cx, cy, rw, rh), color in zip(eyes, lid_colors):
            # The lid comes down from the top of the eye; its edge is a lash line.
            edge = cy - rh + 2 * rh * closed
            d.chord([cx - rw, cy - rh - 2, cx + rw, edge + (rh * 0.6 if closed >= 1 else rh * 0.9)], 180, 360, fill=color + (255,))
            d.rectangle([cx - rw, cy - rh - 2, cx + rw, edge], fill=color + (255,))
            d.arc([cx - rw, edge - rh * 0.5, cx + rw, edge + rh * 0.5], 0, 180, fill=(55, 35, 30, 255), width=2)
        layer = layer.filter(ImageFilter.GaussianBlur(0.8))
        # Keep it inside the eye's outline so it reads as a lid, not a patch.
        clip = Image.new('L', (W, H), 0)
        cd = ImageDraw.Draw(clip)
        for (cx, cy, rw, rh) in eyes:
            cd.ellipse([cx - rw - 1, cy - rh - 3, cx + rw + 1, cy + rh + 3], fill=255)
        layer.putalpha(Image.fromarray(np.minimum(np.array(layer.split()[3]), np.array(clip))))
        arr = np.array(layer)
        write(part, *cut(arr, (arr[..., 3] > 0).astype(np.uint8) * 255, at_frame(blink_track)))

    # ---- previews: Dave's animations, played with these parts ----
    os.makedirs(preview_out, exist_ok=True)
    hide = ('zombie_', 'grabhand', 'Dave_handinghand')
    anims = {}
    for tname, frames in tracks:
        if tname.startswith('anim_') and not any(fr['i'] for fr in frames):
            on = [i for i, fr in enumerate(frames) if fr['f'] >= 0]
            if on:
                anims[tname] = (on[0], on[-1])
    sheets = []
    for anim in ('anim_idle', 'anim_smalltalk', 'anim_mediumtalk', 'anim_blahblah', 'anim_crazy', 'anim_enter', 'anim_blink', 'anim_leave'):
        a0, a1 = anims[anim]
        step = max(1, (a1 - a0) // 7)
        row = []
        for fidx in range(a0, a1 + 1, step):
            canvas = Image.new('RGBA', (W, H), (235, 235, 225, 255))
            for tname, frames in tracks:
                if any(tname.startswith(h) for h in hide) or fidx >= len(frames):
                    continue
                fr = frames[fidx]
                if fr['f'] < 0 or not fr['i'] or fr['a'] <= 0:
                    continue
                part = fr['i'][len('IMAGE_REANIM_'):].lower()
                if part not in subs:
                    continue  # not replaced: would be Dave's own
                img, ox, oy, anchor = subs[part]
                m = rz.matrix(dict(tracks)[anchor][fidx] if anchor else fr)
                m = (m[0], m[1], m[2] - origin[0] + m[0] * ox + m[1] * oy,
                     m[3], m[4], m[5] - origin[1] + m[3] * ox + m[4] * oy)
                layer = img.transform((W, H), Image.AFFINE, rz.inverse(m), resample=Image.BICUBIC)
                canvas.alpha_composite(layer)
            row.append(canvas.resize((W // 2, H // 2)))
        strip = Image.new('RGBA', (sum(r.width for r in row), row[0].height), (255, 255, 255, 255))
        x = 0
        for r in row:
            strip.alpha_composite(r, (x, 0)); x += r.width
        strip.save(os.path.join(preview_out, anim + '.png'))
        sheets.append(anim)
    print('parts:', len(subs), 'previews:', ', '.join(sheets))


if __name__ == '__main__':
    main(*sys.argv[1:7])
