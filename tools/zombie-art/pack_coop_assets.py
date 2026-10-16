"""Prepare the Adventure Plus art and sounds for the game, and generate
src/Lawn/CoopPackedData.cpp, which builds them into the executable with
#embed (fast, unlike the hex route the small art takes).

    python tools/zombie-art/pack_coop_assets.py        (from the project root)

Run it again after changing anything it reads. It needs Pillow and ffmpeg.

  - Animations (folders of numbered PNG frames) become one sprite sheet each:
    every frame cropped to the animation's overall bounds, scaled to its size
    on the lawn, laid out in a grid.
  - Single pictures (plants, projectiles) are trimmed and sized.
  - Sounds are set to the loudness of the game's own sound with the same job
    (measured from main.pak: a shooting plant, an explosion, the music...),
    so nothing is louder than the game around it, and saved as Ogg.
"""
import os, re, subprocess, sys
from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = os.path.join(ROOT, 'assets', 'coop')
OUT = os.path.join(SRC, 'packed')
GEN = os.path.join(ROOT, 'src', 'Lawn', 'CoopPackedData.cpp')
MAX_SHEET_WIDTH = 4096

# name: (folder, scale, anchor in the original 512x512 frame: the point that
# stands on the ground -- shared by every animation of one character)
BOSS_ANCHOR = (237, 506)
SHEETS = {
    'demo_spawn':  ('legendary_demo/spawn_animation', 0.62, BOSS_ANCHOR),
    'demo_idle':   ('legendary_demo/idle',            0.62, BOSS_ANCHOR),
    'demo_move':   ('legendary_demo/move_loop',       0.62, BOSS_ANCHOR),
    'demo_shoot':  ('legendary_demo/shoot_once',      0.62, BOSS_ANCHOR),
    'demo_fly':    ('legendary_demo/idle_fly',        0.62, BOSS_ANCHOR),
    'demo_charge': ('legendary_demo/charge_run',      0.62, BOSS_ANCHOR),
    'demo_bomb':   ('legendary_demo/projectile_bomb', 0.16, (261, 243)),
    'tank':        ('tank',                           0.42, (264, 452)),
    'heavy':       ('dancing_heavy_enemy',            0.28, (241, 510)),
}

# Drop-in art: each character a folder of animations (folders of numbered
# PNG frames), all sharing one size and ground point, worked out from the
# frames themselves: the bottom middle of everything drawn across all of
# them stands on the ground, scaled to the height given. Anything not there
# yet is skipped, and the game draws the plant or zombie underneath instead.
#   group: (folder, {animation name: its folder}, height on the lawn in pixels)
# The packed sheets are named group_animation ("spy_walk").
GROUPS = {
    # About a Peashooter's size.
    'sentry':  ('turret',  {'idle': 'turret_idle', 'fire': 'turret_fire'}, 80),
    # About the Heavy Weapons Guy's size (143 px drawn 1.2x).
    'spy':     ('spy',     {'walk': 'loopwalk', 'attack': 'attack', 'idle': 'still_idle', 'uncloak': 'uncloak'}, 172),
    'soldier': ('soldier', {'walk': 'walk', 'attack': 'attack', 'idle': 'idle_stand'}, 172),
    # Adventure Plus bowling's rockets, in place of the Wall-nuts: about a
    # Wall-nut's size (the plain one some 82 px long).
    'rocket':  ('rocket',  {'normal': 'rocket_normal', 'crit': 'crit_rocket_loop'}, 25),
}

# name: (file, height in pixels to store at)
SINGLES = {
    'noah':             ('noah.png', 180),
    'grs':              ('graz.png', 180),
    'goblin_projectile': ('goblin_projectile.png', 96),
    'sentry_projectile': ('turret/projectile_bullet.png', 9),  # about a Cattail spike; skipped if missing
}

# name: (file, target mean loudness in dB, what it is matched to)
SOUNDS = {
    'grs_shoot':          ('newsoundsandsongs/graz_shoot.wav', -26.0, 'puff/fume-shroom shots'),
    'legend_charge':      ('newsoundsandsongs/legend_boss_charge.wav', -19.0, 'gargantuar thump'),
    'legend_explode':     ('newsoundsandsongs/legend_explode.wav', -13.5, 'explosion / doom-shroom'),
    'legend_shoot':       ('newsoundsandsongs/legend_shoot.wav', -18.0, 'cherry bomb launch'),
    # The boss music. Measured through the game's own mixer (a test program
    # playing both into SDL's disk driver), the final boss's tune comes out at
    # -22.9 dB; the game caps all music at 80/128 of full, below the sound
    # effects, and in his level the explosions and storm bury it. So it is
    # packed hot and peak-limited to 0.55, and Music::LoadPackedMusic adds back
    # x1.6 (COOP_MUSIC_BOOST) past that cap: it comes out about 8 dB over the
    # final boss's tune.
    'rocket_spawn':       ('newsoundsandsongs/rocket_spawn_sound.wav', -18.0, 'cherry bomb launch'),
    'rocket_hit':         ('newsoundsandsongs/rocket_hit.wav', -20.0, 'bowling impact'),         # skipped until it exists
    'rocket_crit_explode': ('newsoundsandsongs/rocket_crit_explode.wav', -13.5, 'an explosion'),  # skipped until it exists
    'soldier_hit':        ('newsoundsandsongs/soldier_hit_sound.wav', -22.0, 'a zombie chomp'),
    'spy_hit':            ('newsoundsandsongs/spy_hit_sound.wav', -22.0, 'a zombie chomp'),
    'spy_uncloak':        ('newsoundsandsongs/uncloak_spyspawn.wav', -20.0, 'a zombie rising'),
    'turret_fire':        ('newsoundsandsongs/turret_fire.wav', -24.0, 'a pea shot'),  # skipped until it exists
    'legend_boss_music':  ('newsoundsandsongs/legend_boss_music.mp3', -10.0, 'well over Brainiac Maniac', 0.55),
}


def frames_of(folder):
    path = os.path.join(SRC, folder)
    return [os.path.join(path, f) for f in sorted(os.listdir(path), key=lambda f: [int(t) if t.isdigit() else t for t in re.split(r'(\d+)', f)])
            if f.lower().endswith('.png')]


# Below this alpha a pixel is dropped: some exports carry a faint haze over
# the whole frame (alpha 8-40), which would otherwise show as a grey box.
ALPHA_FLOOR = 48


def clean(im):
    r, g, b, a = im.split()
    a = a.point(lambda v: 0 if v < ALPHA_FLOOR else v)
    return Image.merge('RGBA', (r, g, b, a))


def pack_sheet(name, folder, scale, anchor, ground_each_frame=False):
    files = frames_of(folder)
    frames = [clean(Image.open(f).convert('RGBA')) for f in files]
    if ground_each_frame:
        # Every frame's lowest point moved down onto the ground line, for a
        # walk exported with the whole figure bobbing: the feet stay planted.
        grounded = []
        for f in frames:
            b = f.split()[3].getbbox()
            g = Image.new('RGBA', f.size, (0, 0, 0, 0))
            g.paste(f, (0, (anchor[1] - b[3]) if b else 0))
            grounded.append(g)
        frames = grounded
    boxes = [f.split()[3].getbbox() for f in frames]
    boxes = [b for b in boxes if b]
    x0, y0 = min(b[0] for b in boxes), min(b[1] for b in boxes)
    x1, y1 = max(b[2] for b in boxes), max(b[3] for b in boxes)
    fw, fh = max(1, round((x1 - x0) * scale)), max(1, round((y1 - y0) * scale))
    cols = max(1, min(len(frames), MAX_SHEET_WIDTH // fw))
    rows = (len(frames) + cols - 1) // cols
    sheet = Image.new('RGBA', (cols * fw, rows * fh), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        cell = f.crop((x0, y0, x1, y1)).resize((fw, fh), Image.LANCZOS)
        sheet.paste(cell, ((i % cols) * fw, (i // cols) * fh))
    out = name + '.png'
    sheet.save(os.path.join(OUT, out), optimize=True)
    meta = dict(name=name, file=out, fw=fw, fh=fh, cols=cols, count=len(frames), scale=scale,
                cropx=x0, cropy=y0, ax=anchor[0], ay=anchor[1])
    print('sheet %-12s %3d frames %4dx%-4d -> %dx%d' % (name, len(frames), fw, fh, sheet.width, sheet.height))
    return meta


def pack_single(name, file, height):
    im = Image.open(os.path.join(SRC, file)).convert('RGBA')
    im = im.crop(im.split()[3].getbbox())
    w = max(1, round(im.width * height / im.height))
    im = im.resize((w, height), Image.LANCZOS)
    out = name + '.png'
    im.save(os.path.join(OUT, out), optimize=True)
    print('image %-18s %dx%d' % (name, w, height))
    return out


def loudness(path):
    r = subprocess.run(['ffmpeg', '-hide_banner', '-nostats', '-i', path, '-af', 'volumedetect', '-f', 'null', '-'],
                       capture_output=True, text=True)
    return float(re.search(r'mean_volume: (-?[\d.]+)', r.stderr).group(1))


def pack_sound(name, file, target, limit=0.95):
    src = os.path.join(SRC, file)
    gain = target - loudness(src)
    out = name + '.ogg'
    subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-i', src,
                    '-af', 'volume=%.2fdB,alimiter=limit=%.2f:level=disabled' % (gain, limit), '-c:a', 'libvorbis', '-q:a', '5',
                    os.path.join(OUT, out)], check=True)
    print('sound %-18s %+.1f dB -> mean %.1f dB' % (name, gain, loudness(os.path.join(OUT, out))))
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    files = []
    sheets = []
    for name, (folder, scale, anchor) in SHEETS.items():
        m = pack_sheet(name, folder, scale, anchor)
        sheets.append(m)
        files.append((name, m['file']))
    for group, (folder, anims, height) in GROUPS.items():
        present = [a for a, sub in anims.items() if os.path.isdir(os.path.join(SRC, folder, sub)) and frames_of(os.path.join(folder, sub))]
        if not present:
            continue
        boxes = []
        for a in present:
            for f in frames_of(os.path.join(folder, anims[a])):
                b = clean(Image.open(f).convert('RGBA')).split()[3].getbbox()
                if b:
                    boxes.append(b)
        x0, y0 = min(b[0] for b in boxes), min(b[1] for b in boxes)
        x1, y1 = max(b[2] for b in boxes), max(b[3] for b in boxes)
        scale = height / float(y1 - y0)
        for a in present:
            # One size and one middle for all of a character's animations
            # (so switching between them does not jump sideways), but each
            # stands on its own lowest point: a run held off the ground in
            # every frame would otherwise float above the lawn.
            bottom = 0
            for f in frames_of(os.path.join(folder, anims[a])):
                b = clean(Image.open(f).convert('RGBA')).split()[3].getbbox()
                if b:
                    bottom = max(bottom, b[3])
            anchor = ((x0 + x1) // 2, bottom)
            m = pack_sheet('%s_%s' % (group, a), os.path.join(folder, anims[a]), scale, anchor, ground_each_frame=(a == 'walk'))
            sheets.append(m)
            files.append((m['name'], m['file']))
    for name, (file, height) in SINGLES.items():
        if not os.path.exists(os.path.join(SRC, file)):
            continue
        files.append((name, pack_single(name, file, height)))
    for name, (file, target, _, *limit) in SOUNDS.items():
        if not os.path.exists(os.path.join(SRC, file)):
            continue
        files.append((name, pack_sound(name, file, target, *limit)))

    lines = ['// Generated by tools/zombie-art/pack_coop_assets.py. Do not edit.',
             '#include "CoopPacked.h"', '']
    for i, (name, file) in enumerate(files):
        lines += ['static const unsigned char gPacked%d[] = {' % i,
                  '#embed "../../assets/coop/packed/%s"' % file, '};']
    lines += ['', 'const CoopPackedFile gCoopPackedFiles[] = {']
    for i, (name, file) in enumerate(files):
        lines.append('\t{ "%s", gPacked%d, sizeof(gPacked%d) },' % (name, i, i))
    lines += ['\t{ nullptr, nullptr, 0 },', '};', '', 'const CoopSheetMeta gCoopSheetMetas[] = {']
    for m in sheets:
        lines.append('\t{ "%(name)s", %(fw)d, %(fh)d, %(cols)d, %(count)d, %(scale).4ff, %(cropx)d, %(cropy)d, %(ax)d, %(ay)d },' % m)
    lines += ['\t{ nullptr, 0, 0, 0, 0, 0.0f, 0, 0, 0, 0 },', '};', '']
    open(GEN, 'w', encoding='utf-8', newline='\n').write('\n'.join(lines))
    print('wrote', os.path.relpath(GEN, ROOT))


if __name__ == '__main__':
    main()
