#!/usr/bin/env python3
"""Generate src/tetris_digits.h - the block tables for the Tetris watchface.

Each digit is a seven-segment glyph on a 6x10 cell grid with two-cell-thick
strokes. Every glyph has a cell count divisible by four, so it can be tiled
exactly by tetrominoes. This script searches many different tilings per digit,
puts each one into an order that can be built by dropping pieces from above,
and colours it so that no two touching pieces share a colour.

The firmware then only picks a variant at random and maps the colour classes
onto a shuffled palette, which is why the same digit never looks the same twice.

Run from the project root:

    python scripts/gen_tetris_digits.py

The seed is fixed, so a re-run reproduces the same header byte for byte. Pass
--preview FILE to also write a contact sheet of every variant as a PNG.
"""

import argparse
import random
import sys
import zlib
import struct
from collections import defaultdict

# --- the glyphs ------------------------------------------------------------
# Seven-segment digits, two cells thick, in a 6 wide x 10 tall grid. Row 0 is
# the top row. These are the shapes the watchface has always drawn.
GLYPHS = [
    ("######", "######", "##..##", "##..##", "##..##", "##..##", "##..##", "##..##", "######", "######"),  # 0
    ("....##", "....##", "....##", "....##", "....##", "....##", "....##", "....##", "....##", "....##"),  # 1
    ("######", "######", "....##", "....##", "######", "######", "##....", "##....", "######", "######"),  # 2
    ("######", "######", "....##", "....##", "######", "######", "....##", "....##", "######", "######"),  # 3
    ("##..##", "##..##", "##..##", "##..##", "######", "######", "....##", "....##", "....##", "....##"),  # 4
    ("######", "######", "##....", "##....", "######", "######", "....##", "....##", "######", "######"),  # 5
    ("######", "######", "##....", "##....", "######", "######", "##..##", "##..##", "######", "######"),  # 6
    ("######", "######", "....##", "....##", "....##", "....##", "....##", "....##", "....##", "....##"),  # 7
    ("######", "######", "##..##", "##..##", "######", "######", "##..##", "##..##", "######", "######"),  # 8
    ("######", "######", "##..##", "##..##", "######", "######", "....##", "....##", "######", "######"),  # 9
]

GRID_W = 6
GRID_H = 10
VARIANTS = 32          # variants generated per digit
MAX_COLOUR_CLASSES = 6  # the firmware palette has six well-separated hues
SEED = 20260920

# --- tetrominoes -----------------------------------------------------------
# Name -> cells of the spawn orientation. Rotations are derived, duplicates
# dropped, and the result sorted so the emitted table is stable across runs.
TETROMINOES = {
    "I": [(0, 0), (1, 0), (2, 0), (3, 0)],
    "J": [(0, 0), (0, 1), (1, 1), (2, 1)],
    "L": [(2, 0), (0, 1), (1, 1), (2, 1)],
    "O": [(0, 0), (1, 0), (0, 1), (1, 1)],
    "S": [(1, 0), (2, 0), (0, 1), (1, 1)],
    "T": [(1, 0), (0, 1), (1, 1), (2, 1)],
    "Z": [(0, 0), (1, 0), (1, 1), (2, 1)],
}


def normalise(cells):
    """Shift a cell set so its top-left corner is at (0, 0), and sort it."""
    mx = min(c[0] for c in cells)
    my = min(c[1] for c in cells)
    return tuple(sorted((x - mx, y - my) for (x, y) in cells))


def build_orientations():
    seen = {}
    for name in sorted(TETROMINOES):
        cur = TETROMINOES[name]
        for _ in range(4):
            cur = [(-y, x) for (x, y) in cur]   # rotate 90 degrees
            key = normalise(cur)
            if key not in seen:
                seen[key] = name
    return [(cells, seen[cells]) for cells in sorted(seen)]


ORIENTATIONS = build_orientations()


def glyph_cells(rows):
    return frozenset((x, y) for y, row in enumerate(rows)
                     for x, c in enumerate(row) if c == "#")


# --- tiling ----------------------------------------------------------------
def find_tiling(cells, rng):
    """Backtracking exact cover of `cells` by tetrominoes. Placement order is
    randomised, so repeated calls return different tilings. Returns a list of
    (orientation index, cell set) or None."""
    free = set(cells)
    placed = []

    def step():
        if not free:
            return True
        target = min(free, key=lambda c: (c[1], c[0]))   # topmost, then leftmost
        options = []
        for oi, (shape, _name) in enumerate(ORIENTATIONS):
            for anchor in shape:
                off = (target[0] - anchor[0], target[1] - anchor[1])
                abs_cells = frozenset((x + off[0], y + off[1]) for (x, y) in shape)
                if abs_cells <= free:
                    options.append((oi, abs_cells))
        rng.shuffle(options)
        for oi, abs_cells in options:
            free.difference_update(abs_cells)
            placed.append((oi, abs_cells))
            if step():
                return True
            placed.pop()
            free.update(abs_cells)
        return False

    return placed if step() else None


def drop_order(pieces):
    """Order the pieces so each can fall straight down into place without
    passing through one already lying there. Returns indices, or None when the
    tiling cannot be built from above (pieces interlock across columns)."""
    tops, bottoms = [], []
    for _oi, cells in pieces:
        t, b = {}, {}
        for (x, y) in cells:
            t[x] = min(t.get(x, GRID_H), y)
            b[x] = max(b.get(x, -1), y)
        tops.append(t)
        bottoms.append(b)

    n = len(pieces)
    after = defaultdict(set)   # i -> pieces that must be placed after i
    indeg = [0] * n
    for i in range(n):
        for j in range(n):
            if i == j:
                continue
            # j has a cell above i in a shared column -> i must go first
            if any(tops[j][c] < tops[i][c] for c in set(tops[i]) & set(tops[j])):
                if j not in after[i]:
                    after[i].add(j)
                    indeg[j] += 1

    # Lowest piece first among those that are ready, so the digit visibly
    # builds from the bottom up.
    ready = [i for i in range(n) if indeg[i] == 0]
    order = []
    while ready:
        ready.sort(key=lambda i: (-max(bottoms[i].values()),
                                  min(tops[i].keys())))
        i = ready.pop(0)
        order.append(i)
        for j in sorted(after[i]):
            indeg[j] -= 1
            if indeg[j] == 0:
                ready.append(j)
    return order if len(order) == n else None


def adjacency(pieces):
    n = len(pieces)
    adj = defaultdict(set)
    for i in range(n):
        for j in range(i + 1, n):
            if any((x + dx, y + dy) in pieces[j][1]
                   for (x, y) in pieces[i][1]
                   for (dx, dy) in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                adj[i].add(j)
                adj[j].add(i)
    return adj


def colour_exact(pieces, adj, rng, k):
    """Proper colouring with exactly k classes available, by backtracking. The
    graphs have at most 13 nodes, so this is instant."""
    n = len(pieces)
    order = sorted(range(n), key=lambda i: (-len(adj[i]), i))
    result = [-1] * n

    def step(pos):
        if pos == n:
            return True
        i = order[pos]
        used = {result[j] for j in adj[i] if result[j] >= 0}
        choices = [c for c in range(k) if c not in used]
        rng.shuffle(choices)
        for c in choices:
            result[i] = c
            if step(pos + 1):
                return True
            result[i] = -1
        return False

    return result if step(0) else None


def colour(pieces, adj, rng):
    """Colour the pieces using as many of the palette's classes as the digit has
    room for, and spread the pieces evenly over them.

    Using FEW classes would be the usual goal, but here it is the wrong one: the
    palette's six hues are 60 degrees apart, so any injective class -> hue map is
    equally contrasty no matter how many classes are used. More classes simply
    means more of the palette visible in one digit, which is the whole point.
    """
    n = len(pieces)
    target = min(n, MAX_COLOUR_CLASSES)
    # Greedy over a random order, always picking the least-used allowed class,
    # which both spreads the load and tends to reach every class.
    for _ in range(40):
        order = list(range(n))
        rng.shuffle(order)
        result = [-1] * n
        used_count = [0] * target
        ok = True
        for i in order:
            taken = {result[j] for j in adj[i] if result[j] >= 0}
            choices = [c for c in range(target) if c not in taken]
            if not choices:
                ok = False
                break
            least = min(used_count[c] for c in choices)
            result[i] = rng.choice([c for c in choices if used_count[c] == least])
            used_count[result[i]] += 1
        if ok and all(c > 0 for c in used_count):
            return result
    # Dense digit: fall back to whatever number of classes is achievable.
    for k in range(target, 1, -1):
        result = colour_exact(pieces, adj, rng, k)
        if result is not None:
            return result
    return None


def generate(digit, rng):
    cells = glyph_cells(GLYPHS[digit])
    if len(cells) % 4:
        sys.exit(f"digit {digit}: {len(cells)} cells is not a multiple of 4")
    variants = {}
    attempts = 0
    while len(variants) < VARIANTS and attempts < VARIANTS * 500:
        attempts += 1
        pieces = find_tiling(cells, rng)
        if pieces is None:
            continue
        key = frozenset(c for _oi, c in pieces)
        if key in variants:
            continue
        order = drop_order(pieces)
        if order is None:
            continue           # interlocks, cannot be dropped from above
        pieces = [pieces[i] for i in order]
        adj = adjacency(pieces)
        cols = colour(pieces, adj, rng)
        if cols is None:
            continue           # needs more classes than the palette has
        variants[key] = (pieces, cols)
    return list(variants.values())


# --- checks ----------------------------------------------------------------
def check(digit, variants):
    """Verify every property the firmware relies on. Any failure aborts the
    run: a wrong table would show up as a broken digit on the panel, which is
    exactly what must not reach the device."""
    cells = glyph_cells(GLYPHS[digit])
    expected_pieces = len(cells) // 4
    for vi, (pieces, cols) in enumerate(variants):
        where = f"digit {digit} variant {vi}"
        if len(pieces) != expected_pieces:
            sys.exit(f"{where}: {len(pieces)} pieces, expected {expected_pieces}")

        covered = set()
        for oi, pcs in pieces:
            if len(pcs) != 4:
                sys.exit(f"{where}: piece is not four cells")
            if covered & pcs:
                sys.exit(f"{where}: pieces overlap")
            covered |= pcs
            # the emitted anchor must reproduce the cells
            top = min(y for (_x, y) in pcs)
            left = min(x for (x, _y) in pcs)
            rebuilt = {(left + dx, top + dy) for (dx, dy) in ORIENTATIONS[oi][0]}
            if rebuilt != set(pcs):
                sys.exit(f"{where}: orientation {oi} does not reproduce the piece")
            if not (0 <= left <= GRID_W - 1 and 0 <= top <= GRID_H - 1):
                sys.exit(f"{where}: anchor out of range")
        if covered != cells:
            sys.exit(f"{where}: does not cover the glyph exactly")

        # droppable in the emitted order
        occupied = set()
        for oi, pcs in pieces:
            for (x, y) in pcs:
                if any((x, r) in occupied for r in range(0, y)):
                    sys.exit(f"{where}: a piece falls through one already placed")
            occupied |= set(pcs)

        # proper colouring, within the palette
        adj = adjacency(pieces)
        if max(cols) >= MAX_COLOUR_CLASSES:
            sys.exit(f"{where}: needs more than {MAX_COLOUR_CLASSES} colour classes")
        for i, neighbours in adj.items():
            for j in neighbours:
                if cols[i] == cols[j]:
                    sys.exit(f"{where}: touching pieces {i} and {j} share a colour")


# --- output ----------------------------------------------------------------
def pack(oi, left, top, cls):
    if not (0 <= oi < 32 and 0 <= left < 8 and 0 <= top < 16 and 0 <= cls < 8):
        sys.exit("field out of range while packing")
    return oi | (left << 5) | (top << 8) | (cls << 12)


def emit(all_variants, path):
    out = []
    w = out.append
    w("// GENERATED FILE - do not edit by hand.")
    w("// Produced by scripts/gen_tetris_digits.py; re-run that script to change it.")
    w("//")
    w("// Block tables for the Tetris watchface. Each digit is a 6x10 cell glyph")
    w("// tiled exactly by tetrominoes, in several variants, each already ordered so")
    w("// the pieces can be dropped in from above and coloured so that no two")
    w("// touching pieces share a colour class. The firmware picks a variant at")
    w("// random and maps the colour classes onto a shuffled palette.")
    w("")
    w("#ifndef TETRIS_DIGITS_H")
    w("#define TETRIS_DIGITS_H")
    w("")
    w("#include <stdint.h>")
    w("")
    w(f"#define TETRIS_GRID_W {GRID_W}")
    w(f"#define TETRIS_GRID_H {GRID_H}")
    w(f"#define TETRIS_VARIANTS {VARIANTS}")
    w(f"#define TETRIS_COLOUR_CLASSES {MAX_COLOUR_CLASSES}")
    w("")
    w("// The distinct tetromino orientations. Four cells each, relative to the")
    w("// piece's top-left corner, packed as (x << 4) | y.")
    w(f"const uint8_t TETRIS_SHAPE[{len(ORIENTATIONS)}][4] = {{")
    for cells, name in ORIENTATIONS:
        body = ", ".join(f"0x{(x << 4) | y:02X}" for (x, y) in cells)
        w(f"  {{ {body} }},   // {name}")
    w("};")
    w("")
    w("// Bounding box of each orientation, packed as (width << 4) | height. The")
    w("// renderer needs it to keep a piece inside its digit while it spins.")
    sizes = []
    for cells, _name in ORIENTATIONS:
        bw = max(x for (x, _y) in cells) + 1
        bh = max(y for (_x, y) in cells) + 1
        sizes.append((bw << 4) | bh)
    w(f"const uint8_t TETRIS_SIZE[{len(ORIENTATIONS)}] = {{")
    w("  " + ", ".join(f"0x{s:02X}" for s in sizes))
    w("};")
    w("")
    w("// One quarter turn: the orientation a piece takes after rotating once.")
    w("// Square pieces map to themselves, two-state pieces alternate.")
    index = {cells: i for i, (cells, _n) in enumerate(ORIENTATIONS)}
    turns = []
    for cells, _name in ORIENTATIONS:
        turned = normalise([(-y, x) for (x, y) in cells])
        if turned not in index:
            sys.exit("a rotated orientation is missing from the table")
        turns.append(index[turned])
    w(f"const uint8_t TETRIS_TURN[{len(ORIENTATIONS)}] = {{")
    w("  " + ", ".join(str(t) for t in turns))
    w("};")
    w("")
    w("// One piece: orientation 0..4, x 5..7, top row 8..11, colour class 12..14.")
    w("#define TETRIS_PIECE_ORIENT(p) ((p) & 0x1F)")
    w("#define TETRIS_PIECE_X(p)      (((p) >> 5) & 0x07)")
    w("#define TETRIS_PIECE_TOP(p)    (((p) >> 8) & 0x0F)")
    w("#define TETRIS_PIECE_CLASS(p)  (((p) >> 12) & 0x07)")
    w("")
    counts = []
    for d, variants in enumerate(all_variants):
        n = len(variants[0][0])
        counts.append(n)
        w(f"// digit {d}: {len(variants)} variants of {n} pieces")
        w(f"const uint16_t TETRIS_VAR_{d}[{len(variants) * n}] = {{")
        for pieces, cols in variants:
            vals = []
            for (oi, pcs), cls in zip(pieces, cols):
                left = min(x for (x, _y) in pcs)
                top = min(y for (_x, y) in pcs)
                vals.append(f"0x{pack(oi, left, top, cls):04X}")
            w("  " + ", ".join(vals) + ",")
        w("};")
        w("")
    w("const uint16_t *const TETRIS_VAR[10] = {")
    w("  " + ", ".join(f"TETRIS_VAR_{d}" for d in range(10)))
    w("};")
    w("")
    w("// Pieces per variant, one entry per digit (glyph cells / 4).")
    w("const uint8_t TETRIS_PIECES_PER_DIGIT[10] = { "
      + ", ".join(str(c) for c in counts) + " };")
    w("")
    w("#endif  // TETRIS_DIGITS_H")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    return counts


# --- preview ---------------------------------------------------------------
PREVIEW_RGB = [(255, 0, 0), (255, 255, 0), (0, 255, 0),
               (0, 255, 255), (0, 80, 255), (255, 0, 255)]


def write_png(path, pixels):
    h = len(pixels)
    w = len(pixels[0])
    raw = b"".join(b"\x00" + bytes(v for px in row for v in px) for row in pixels)

    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def preview(all_variants, path, rng, scale=3, gap=2):
    """Contact sheet of every variant. The class -> hue mapping is shuffled per
    variant exactly as the firmware does it, so the sheet shows what the panel
    will really look like; mapping class 0 always to the same hue would make the
    palette look far more lopsided than it is."""
    cols = VARIANTS
    cw = GRID_W * scale + gap
    ch = GRID_H * scale + gap
    w = cols * cw + gap
    h = len(all_variants) * ch + gap
    img = [[(24, 24, 24) for _ in range(w)] for _ in range(h)]
    for d, variants in enumerate(all_variants):
        for vi, (pieces, colours) in enumerate(variants):
            hue = list(range(MAX_COLOUR_CLASSES))
            rng.shuffle(hue)
            ox = gap + vi * cw
            oy = gap + d * ch
            for (oi, pcs), cls in zip(pieces, colours):
                rgb = PREVIEW_RGB[hue[cls]]
                for (x, y) in pcs:
                    for sy in range(scale):
                        for sx in range(scale):
                            img[oy + y * scale + sy][ox + x * scale + sx] = rgb
    write_png(path, img)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="src/tetris_digits.h")
    ap.add_argument("--preview", help="also write a PNG contact sheet here")
    args = ap.parse_args()

    rng = random.Random(SEED)
    all_variants = []
    for d in range(10):
        variants = generate(d, rng)
        if not variants:
            sys.exit(f"digit {d}: no usable tiling found")
        check(d, variants)
        all_variants.append(variants)
        pieces = len(variants[0][0])
        used = [len({c for c in cols}) for _p, cols in variants]
        spread = defaultdict(int)
        for _p, cols in variants:
            for c in cols:
                spread[c] += 1
        share = " ".join(f"{100.0 * spread[c] / sum(spread.values()):.0f}%"
                         for c in range(MAX_COLOUR_CLASSES))
        print(f"digit {d}: {len(variants):3d} variants, {pieces:2d} pieces, "
              f"{min(used)}-{max(used)} classes used, share per class: {share}")

    counts = emit(all_variants, args.out)
    total = sum(len(v) * c for v, c in zip(all_variants, counts))
    print(f"\nwrote {args.out}: {total} pieces, {total * 2} bytes of tables")
    if args.preview:
        preview(all_variants, args.preview, rng)
        print(f"wrote {args.preview}")


if __name__ == "__main__":
    main()
