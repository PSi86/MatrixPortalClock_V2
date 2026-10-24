#!/usr/bin/env python3
"""Generate src/snake_paths.h - each digit of the Snake watchface as one snake.

A snake's body is a path that passes every lit cell of its digit once (a
Hamiltonian path on the 4-neighbour grid of 2 px cells). The digits come in
families, a setting of the face:

- coils (the default): the Tetris glyphs, 6 x 10 cells, strokes two cells
  thick, the snake coiling through both. Both ends of a path lie on the bottom
  row, side by side, so the head can crawl out into the lane below the digits,
  the next snake can crawl in, and a snake can chase its own tail round its
  digit. Of all such paths the one with the fewest turns is kept: long
  straight runs read as a body, zigzags do not.
- lines, narrow lines (Peter, 2026-10-23: a 2 px snake on the table clock,
  parts of it may touch where it doubles back): thin strokes
  with a cell's gap, so a 2 px body shows as one long snake; where a stroke
  branches the snake doubles back and its two parts lie side by side. Such a
  digit's ends need not lie side by side; both must be open to the outside
  (a way in and out). Of all paths, the one whose head ends on the digit's
  bottom row (the apple, the tongue), then the fewest turns, then head beside
  tail (the tail chase).
- spaced (Peter, 2026-10-24): digits of 4 x 7 cells on a grid of its own,
  cells of 3 px, so that a 2 px snake keeps a 1 px gap everywhere, also where
  it doubles back (the 1, the 3's middle, the 4's foot). Its paths are found
  as the thin strokes'.

Run from the project root:

    python scripts/gen_snake_paths.py [--json FILE] [--families-json FILE]

The searches are exhaustive or bounded the same way every time, so a re-run
writes the same header byte for byte. It also puts the paths into the web
app's picture of the face (webui/index.html, between the SNAKE_PATHS
markers; --webui names another file). --json writes the coils as before for
the PC tools that plan and draw the face; --families-json every family,
keyed by its name.
"""

import argparse
import json
import sys

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
    ("......", "......", "......", "......", "######", "######", "......", "......", "......", "......"),  # the dash
]
W, H = 6, 10
STEPS = ((0, 1), (1, 0), (0, -1), (-1, 0))
TURN_LIMIT = 40

# ---- the families of thin strokes --------------------------------------------------------------
LINES = [
    ["######"] + ["#....#"] * 8 + ["######"],                                                              # 0
    ["....##"] * 10,                                                                                       # 1
    ["######", ".....#", ".....#", ".....#", "######", "#.....", "#.....", "#.....", "#.....", "######"],  # 2
    ["######", ".....#", ".....#", ".....#", ".#####", ".#####", ".....#", ".....#", ".....#", "######"],  # 3
    ["#....#", "#....#", "#....#", "#....#", "#....#", "######", "....##", "....##", "....##", "....##"],  # 4
    ["######", "#.....", "#.....", "#.....", "######", ".....#", ".....#", ".....#", ".....#", "######"],  # 5
    ["######", "#.....", "#.....", "#.....", "######", "#....#", "#....#", "#....#", "#....#", "######"],  # 6
    ["######"] + [".....#"] * 9,                                                                           # 7
    ["######", "#....#", "#....#", "#....#", "######", "#....#", "#....#", "#....#", "#....#", "######"],  # 8
    ["######", "#....#", "#....#", "#....#", "#....#", "######", ".....#", ".....#", ".....#", "######"],  # 9
    ["......"] * 4 + ["######"] + ["......"] * 5,                                                          # the dash
]


def narrow(rows):
    """A 6-wide shape made 5 wide: its second column left out, set a cell in."""
    return ["." + r[0] + r[2:] for r in rows]


NARROW = [narrow(g) for g in LINES]
NARROW[1] = LINES[1]                      # the 1: the same double line at the right
NARROW[3] = narrow(["######", ".....#", ".....#", ".....#", "..####", "..####", ".....#", ".....#", ".....#", "######"])
# (Bold uprights and the 1 and 7 as springs were families of their own until
# 2026-10-24: tried on the clock and left out.)

# Spaced: 4 x 7 cells of 3 px (snake.h's grid of 3 px cells), a 1 px gap everywhere.
SPACED = [
    ["####", "#..#", "#..#", "#..#", "#..#", "#..#", "####"],   # 0
    ["..##"] * 7,                                               # 1
    ["####", "...#", "...#", "####", "#...", "#...", "####"],   # 2
    ["####", "...#", ".###", ".###", "...#", "...#", "####"],   # 3
    ["#..#", "#..#", "#..#", "####", "..##", "..##", "..##"],   # 4
    ["####", "#...", "#...", "####", "...#", "...#", "####"],   # 5
    ["####", "#...", "#...", "####", "#..#", "#..#", "####"],   # 6
    ["####", "...#", "...#", "...#", "...#", "...#", "...#"],   # 7
    ["####", "#..#", "#..#", "####", "#..#", "#..#", "####"],   # 8
    ["####", "#..#", "#..#", "####", "...#", "...#", "####"],   # 9
    ["....", "....", "....", "####", "....", "....", "...."],   # the dash
]

# In the order of the face's setting (snake::Digits).
FAMILIES = [("coils", None), ("lines", LINES), ("narrow", NARROW), ("spaced", SPACED)]
OPEN_BUDGET = 400000      # search steps for a digit of thin strokes


def cells(glyph):
    return {(r, c) for r, row in enumerate(glyph) for c, ch in enumerate(row) if ch == "#"}


def neighbours(cell, free):
    r, c = cell
    for dr, dc in STEPS:
        n = (r + dr, c + dc)
        if n in free:
            yield n


def connected(free, start):
    """Whether the free cells hang together with `start`."""
    if not free:
        return True
    seen, stack = {start}, [start]
    while stack:
        for n in neighbours(stack.pop(), free | {start}):
            if n not in seen:
                seen.add(n)
                stack.append(n)
    return free <= seen


def best_path(lit, start, end, max_turns):
    """The path start -> end through every lit cell with the fewest turns
    (at most max_turns), or (None, None)."""
    best = [None, max_turns + 1]
    total = len(lit)

    def dfs(path, free, turns):
        if turns >= best[1]:
            return
        cur = path[-1]
        if len(path) == total:
            if cur == end:
                best[0], best[1] = list(path), turns
            return
        if cur == end:
            return
        # the rest must hang together with the current cell, and a cell with
        # a single free neighbour left can only be the end
        if not connected(free, cur):
            return
        dead = 0
        for f in free:
            deg = sum(1 for _ in neighbours(f, free | {cur}))
            if deg == 0:
                return
            if deg == 1 and f != end:
                dead += 1
                if dead > 1:
                    return
        prev_dir = (cur[0] - path[-2][0], cur[1] - path[-2][1]) if len(path) > 1 else None
        nxt = list(neighbours(cur, free))
        # straight on first: the fewest turns are found early
        nxt.sort(key=lambda n: 0 if prev_dir == (n[0] - cur[0], n[1] - cur[1]) else 1)
        for n in nxt:
            d = (n[0] - cur[0], n[1] - cur[1])
            t = turns + (1 if prev_dir is not None and d != prev_dir else 0)
            free.discard(n)
            path.append(n)
            dfs(path, free, t)
            path.pop()
            free.add(n)

    dfs([start], set(lit) - {start}, 0)
    return best[0], (best[1] if best[0] else None)


def solve(glyph):
    """The path with the fewest turns whose ends lie side by side on the bottom row."""
    lit = cells(glyph)
    low = max(r for r, _ in lit)
    bottom = sorted(c for c in lit if c[0] == low)
    found = None
    for s in bottom:
        for e in bottom:
            if s == e:
                continue
            if (s[0] + s[1]) % 2 == (e[0] + e[1]) % 2 and len(lit) % 2 == 0:
                continue   # the grid is bipartite: a path of an even count ends on both colours
            p, t = best_path(lit, s, e, found[1] - 1 if found else TURN_LIMIT)
            if p:
                found = (p, t)
    if not found:
        sys.exit("no snake path for a glyph")
    path, turns = found
    (r0, c0), (r1, c1) = path[0], path[-1]
    if abs(r0 - r1) + abs(c0 - c1) != 1:
        sys.exit("a path whose head is not beside its tail")
    return path, turns


def solve_open(glyph):
    """A digit of thin strokes: the path through every lit cell whose two ends
    are open to the outside (not in a bowl), the head on the bottom row if it
    can be, then the fewest turns, then the head beside the tail."""
    lit = cells(glyph)
    h = max(r for r, _ in lit) + 1
    w = max(c for _, c in lit) + 1
    outside, stack = set(), [(-1, -1)]
    while stack:
        c = stack.pop()
        if c in outside or c in lit or not (-1 <= c[0] <= h and -1 <= c[1] <= w):
            continue
        outside.add(c)
        stack += [(c[0] + 1, c[1]), (c[0] - 1, c[1]), (c[0], c[1] + 1), (c[0], c[1] - 1)]

    def open_(c):
        return any((c[0] + dr, c[1] + dc) in outside for dr, dc in ((1, 0), (-1, 0), (0, 1), (0, -1)))

    bottom = h - 1
    total = len(lit)
    best = [None, None]
    steps = [0]

    def key(path, turns):
        head, tail = path[-1], path[0]
        beside = abs(head[0] - tail[0]) + abs(head[1] - tail[1]) == 1
        return (head[0] != bottom, turns, not beside)

    def dfs(path, seen, turns, last):
        steps[0] += 1
        if steps[0] > OPEN_BUDGET:
            return
        if best[0] is not None and turns > best[0][1]:
            return
        if len(path) == total:
            if open_(path[-1]):
                k = key(path, turns)
                if best[0] is None or k < best[0]:
                    best[0], best[1] = k, list(path)
            return
        r, c = path[-1]
        dirs = [(1, 0), (-1, 0), (0, 1), (0, -1)]
        if last in dirs:              # straight on first
            dirs.remove(last)
            dirs.insert(0, last)
        for d in dirs:
            n = (r + d[0], c + d[1])
            if n in lit and n not in seen:
                seen.add(n)
                path.append(n)
                dfs(path, seen, turns + (last is not None and d != last), d)
                path.pop()
                seen.discard(n)

    for s in sorted(c for c in lit if open_(c)):
        dfs([s], {s}, 0, None)
    if best[1] is None:
        sys.exit("no snake path with open ends for a glyph")
    return best[1], best[0][1]


def header(families):
    longest = max(len(p) for _, paths, _ in families for p in paths)
    names = ", ".join("%d %s" % (i, name) for i, (name, _, _) in enumerate(families))
    out = [
        "// GENERATED FILE - do not edit by hand.",
        "// Produced by scripts/gen_snake_paths.py; re-run that script to change it.",
        "//",
        "// Each digit of the Snake watchface as one snake, in each family of digits",
        "// (a setting of the face): the cells of its glyph (6 x 10 cells of 2 px; the",
        "// spaced digits 4 x 7 cells of 3 px) in the order its body lies, tail first,",
        "// each packed as (row << 4) | column. Index 10 is the dash shown while the",
        "// clock has no time. The coils are the Tetris glyphs, head and tail side by",
        "// side on the bottom row; the other families are thin strokes, both ends",
        "// open to the outside.",
        "",
        "#ifndef SNAKE_PATHS_H",
        "#define SNAKE_PATHS_H",
        "",
        "#include <stdint.h>",
        "",
        "#define SNAKE_GLYPHS 11      // 0-9 and the dash",
        "#define SNAKE_FAMILIES %d     // %s" % (len(families), names),
        "#define SNAKE_PATH_MAX %d" % longest,
        "",
        "const uint8_t SNAKE_PATH_LEN[SNAKE_FAMILIES][SNAKE_GLYPHS] = {",
    ]
    for name, paths, _ in families:
        out.append("  { %s },   // %s" % (", ".join(str(len(p)) for p in paths), name))
    out += ["};", "", "const uint8_t SNAKE_PATH[SNAKE_FAMILIES][SNAKE_GLYPHS][SNAKE_PATH_MAX] = {"]
    for name, paths, turns in families:
        out.append("  {   // %s" % name)
        for d, (p, t) in enumerate(zip(paths, turns)):
            packed = ["0x%02X" % ((r << 4) | c) for r, c in p]
            out.append("    {   // %s: %d cells, %d turns" % (str(d) if d < 10 else "-", len(p), t))
            for i in range(0, len(packed), 13):
                out.append("      " + ", ".join(packed[i:i + 13]) + ",")
            out.append("    },")
        out.append("  },")
    out += ["};", "", "#endif  // SNAKE_PATHS_H", ""]
    return "\n".join(out)


def keyed(paths):
    return {(str(d) if d < 10 else "-"): p for d, p in enumerate(paths)}


WEBUI_BEGIN = "  // BEGIN SNAKE_PATHS - written by scripts/gen_snake_paths.py, do not edit by hand\n"
WEBUI_END = "  // END SNAKE_PATHS\n"


def webui_block(families):
    """The paths for the web app's picture of the face: per family (in the
    order of its setting), per digit (the dash last), two hex digits a cell -
    row, column - tail first, as snake_paths.h packs them."""
    out = [WEBUI_BEGIN, "  var SNAKE_PATHS = [\n"]
    for name, paths, _ in families:
        cells = ", ".join("'%s'" % "".join("%02x" % ((r << 4) | c) for r, c in p) for p in paths)
        out.append("    [%s],   // %s\n" % (cells, name))
    out.append("  ];\n")
    # px a cell: the spaced digits on their grid of 3 px cells, the others on 2 px
    out.append("  var SNAKE_PITCH = [%s];\n" % ", ".join("3" if name == "spaced" else "2" for name, _, _ in families))
    out.append(WEBUI_END)
    return "".join(out)


def write_webui(path, families):
    """Puts the paths between the markers in the web app (webui/index.html)."""
    with open(path, encoding="utf-8", newline="") as f:
        text = f.read()
    begin, end = text.find(WEBUI_BEGIN), text.find(WEBUI_END)
    if begin < 0 or end < begin:
        sys.exit("no SNAKE_PATHS markers in " + path)
    text = text[:begin] + webui_block(families) + text[end + len(WEBUI_END):]
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", default="src/snake_paths.h")
    ap.add_argument("--webui", default="webui/index.html", help="the web app whose picture of the face gets the paths")
    ap.add_argument("--json", help="also write the coils as JSON, keyed '0'..'9' and '-'")
    ap.add_argument("--families-json", help="also write every family as JSON, keyed by its name, then the digit")
    args = ap.parse_args()
    families = []
    for name, shapes in FAMILIES:
        paths, turns = [], []
        for d in range(len(GLYPHS)):
            p, t = solve(GLYPHS[d]) if shapes is None else solve_open(shapes[d])
            paths.append(p)
            turns.append(t)
        families.append((name, paths, turns))
        print("%-8s %s" % (name, " ".join("%s:%d/%d" % (str(d) if d < 10 else "-", len(p), t)
                                          for d, (p, t) in enumerate(zip(paths, turns)))))
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(header(families))
    print("written", args.out)
    write_webui(args.webui, families)
    print("written", args.webui, "(SNAKE_PATHS)")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(keyed(families[0][1]), f, indent=0)
        print("written", args.json)
    if args.families_json:
        with open(args.families_json, "w", encoding="utf-8") as f:
            json.dump({name: keyed(paths) for name, paths, _ in families}, f, indent=0)
        print("written", args.families_json)


if __name__ == "__main__":
    main()
