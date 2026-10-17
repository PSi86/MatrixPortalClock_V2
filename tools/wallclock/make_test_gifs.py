"""Test GIFs for the wall clock L (192x128) on the spike's GIF screen:

    ramps_scroll.gif   red, green, blue and white bands, each a triangle ramp 0..252..0 in steps of 4
                       (64 levels, 256 colours in all, so the GIF holds them exactly), scrolling 4 LEDs
                       a frame: the colour depth and the driver's steps at multiples of 32, moving
    motion_bars.gif    on black: a white bar crossing left to right, a yellow bar top to bottom and a
                       green line turning round the middle, 40 ms a frame: tearing and smoothness
    bars_slow.gif      one white bar crossing at 1 LED a frame (50 ms): the panels' middle at low speed
    steps_16.gif       red, green, blue, white in 16 patches 0..255 each: the channels' brightness curves
    steps_dark.gif     the same in 16 patches 0..60 (step 4): the dark end through the gamma modes
    colour_chart.gif   12 hues at full, half and quarter value, and 12 greys from 255 down to 4
    raw_steps_dark.gif the dark steps, shown by the spike without gamma (its name starts with raw_)
    raw_grey_match.gif dark greys (raw 2, 4, 8, 16) with 100 % down to 45 % blue: the neutral one per level
    aliens_l_wave.gif  the Aliens face's 22:00 wave three times the size, as the host tool draws it for
                       L (tools.local/aliens/host, "host change B 2159 2200 0 1 <json>"), on a 50 ms grid

    py make_test_gifs.py <out dir> [aliens json]
"""
import json, math, os, sys

from PIL import Image, ImageDraw

W, H = 192, 128
out = sys.argv[1]
os.makedirs(out, exist_ok=True)


def save(name, frames, ms):
    frames[0].save(os.path.join(out, name), save_all=True, append_images=frames[1:], duration=ms, loop=0,
                   optimize=False, disposal=1)
    print(name, len(frames), "frames,", os.path.getsize(os.path.join(out, name)) // 1024, "KB")


# 1 Ramps, scrolling: one palette of 4 x 64 levels, indexed directly.
pal = []
for ch in range(4):
    for lv in range(64):
        v = lv * 4
        pal += [v if ch in (0, 3) else 0, v if ch in (1, 3) else 0, v if ch in (2, 3) else 0]
frames = []
for f in range(32):
    im = Image.new("P", (W, H))
    im.putpalette(pal)
    px = im.load()
    for x in range(W):
        p = (x + f * 4) % 128
        lv = p if p < 64 else 127 - p            # triangle 0..63..0 over 128 LEDs
        for ch in range(4):
            for y in range(ch * 32, ch * 32 + 32):
                px[x, y] = ch * 64 + lv
    frames.append(im)
save("ramps_scroll.gif", frames, 60)

# 2 Moving bars and a turning line.
frames = []
for f in range(48):
    im = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(im)
    bx = f * 4 % W
    d.rectangle([bx, 0, bx + 5, H - 1], fill=(255, 255, 255))
    by = f * 8 // 3 % H
    d.rectangle([0, by, W - 1, by + 3], fill=(255, 200, 0))
    a = 2 * math.pi * f / 48
    d.line([W // 2, H // 2, W // 2 + int(60 * math.cos(a)), H // 2 + int(60 * math.sin(a))], fill=(0, 255, 60), width=3)
    frames.append(im.convert("P", palette=Image.ADAPTIVE, colors=8))
save("motion_bars.gif", frames, 40)

def exact(im):
    """An RGB picture of at most 256 colours as a palette picture holding exactly those colours."""
    cols = sorted(set(im.getdata()))
    assert len(cols) <= 256, len(cols)
    idx = {c: i for i, c in enumerate(cols)}
    p = Image.new("P", im.size)
    p.putpalette([v for c in cols for v in c])
    p.putdata([idx[c] for c in im.getdata()])
    return p


def bands(values_for_band):
    """Four bands of 32 rows - red, green, blue, white - each 16 patches of 12 LEDs."""
    im = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(im)
    for band in range(4):
        for k, v in enumerate(values_for_band(band)):
            c = (v if band in (0, 3) else 0, v if band in (1, 3) else 0, v if band in (2, 3) else 0)
            d.rectangle([k * 12, band * 32, k * 12 + 11, band * 32 + 31], fill=c)
    return exact(im)


# 2b One white bar crossing slowly, 1 LED a frame at 50 ms (20 LEDs a second): the offset at the
# panels' middle when the eyes follow it, against the fast bars above (100 LEDs a second).
frames = []
for f in range(W):
    im = Image.new("RGB", (W, H))
    ImageDraw.Draw(im).rectangle([f, 0, f + 5, H - 1], fill=(255, 255, 255))
    frames.append(exact(im))
save("bars_slow.gif", frames, 50)

# 4 Steps 0..255 (16 a band) and 5 the dark ones 0..30 (step 2), single frames.
save("steps_16.gif", [bands(lambda b: [k * 17 for k in range(16)])], 1000)
save("steps_dark.gif", [bands(lambda b: [k * 4 for k in range(16)])], 1000)   # 0..60: where the gamma modes differ

# 6 A colour chart: 12 hues at full, half and quarter value, and 12 greys.
HUES = [(255, 0, 0), (255, 128, 0), (255, 255, 0), (128, 255, 0), (0, 255, 0), (0, 255, 128),
        (0, 255, 255), (0, 128, 255), (0, 0, 255), (128, 0, 255), (255, 0, 255), (255, 0, 128)]
GREYS = [255, 192, 160, 128, 96, 64, 48, 32, 24, 16, 8, 4]
im = Image.new("RGB", (W, H))
d = ImageDraw.Draw(im)
for k, (r, g, b) in enumerate(HUES):
    for row, scale in enumerate((1.0, 0.5, 0.25)):
        d.rectangle([k * 16, row * 32, k * 16 + 15, row * 32 + 31], fill=(int(r * scale), int(g * scale), int(b * scale)))
    v = GREYS[k]
    d.rectangle([k * 16, 96, k * 16 + 15, 127], fill=(v, v, v))
save("colour_chart.gif", [exact(im)], 1000)

# 7 Raw images (the spike shows a GIF whose name starts with "raw_" without any gamma): the dark
# steps as the panel gets them, and a grey match - rows of dark greys (raw 2, 4, 8, 16), each in 12
# patches with less and less blue (100 % down to 45 %), to find the most neutral one per level.
save("raw_steps_dark.gif", [bands(lambda b: [k * 2 for k in range(16)])], 1000)
im = Image.new("RGB", (W, H))
d = ImageDraw.Draw(im)
for row, level in enumerate((2, 4, 8, 16)):
    for k in range(12):
        blue = int(round(level * (1.0 - 0.05 * k)))
        d.rectangle([k * 16, row * 32, k * 16 + 15, row * 32 + 31], fill=(level, level, blue))
save("raw_grey_match.gif", [exact(im)], 1000)

# 3 The Aliens wave on L, from the host tool's frames.
if len(sys.argv) > 2:
    d = json.load(open(sys.argv[2]))
    assert (d["w"], d["h"]) == (W, H), "needs the host tool's 'change B' output (192x128)"
    shots = [(-1000, d["before"])] + [(fr["ms"], fr["rgb"]) for fr in d["frames"]]
    frames, t, i = [], -1000, 0
    while t <= d["end"] + 1000:
        while i + 1 < len(shots) and shots[i + 1][0] <= t:
            i += 1
        frames.append(Image.frombytes("RGB", (W, H), bytes.fromhex(shots[i][1])).convert("P", palette=Image.ADAPTIVE, colors=16))
        t += 50
    save("aliens_l_wave.gif", frames, 50)
