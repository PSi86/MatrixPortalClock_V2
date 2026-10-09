# Helpers for scripts/gif_pack.py (the GIF pack in the ffat partition).

import os

# How the clock stands, for which GIFs it gets (custom_gif_orientation):
# landscape takes GIFs up to the panel's size (64x32), portrait up to the panel
# turned upright (32x64), both those that fit one way or the other. The clock
# plays a GIF only while it fits the panel as it is held.
ORIENTATIONS = ("landscape", "portrait", "both")


def gif_size(path):
    """(width, height) from the GIF header, or None if it is not a GIF."""
    with open(path, "rb") as f:
        head = f.read(10)
    if len(head) < 10 or head[:4] != b"GIF8":
        return None
    return head[6] | (head[7] << 8), head[8] | (head[9] << 8)


def gifs_in(folder):
    found = []
    for root, _dirs, files in os.walk(folder):
        for name in sorted(files):
            if name.lower().endswith(".gif"):
                found.append(os.path.join(root, name))
    return sorted(found)


def exclusions(folder):
    """Lines of exclude.txt in the folder or its parent: files to leave out."""
    lines = []
    for where in (folder, os.path.dirname(os.path.abspath(folder))):
        path = os.path.join(where, "exclude.txt")
        if os.path.isfile(path):
            with open(path, encoding="utf-8") as f:
                lines += [l.strip().replace("\\", "/") for l in f if l.strip() and not l.startswith("#")]
    return lines


def excluded(path, skip):
    rel = path.replace("\\", "/")
    return any(rel.endswith(s) for s in skip)


def listed_dirs(project_dir, list_file):
    """The folders named in list_file (one per line, # comments), relative ones
    taken from the project folder; [] when the file does not exist."""
    path = os.path.join(project_dir, list_file)
    if not os.path.isfile(path):
        return []
    dirs = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                dirs.append(line if os.path.isabs(line) else os.path.join(project_dir, line))
    return dirs


def panel_size(text):
    """'64x32' -> (64, 32), the panel's size in landscape."""
    a, b = (int(v) for v in text.lower().split("x"))
    return max(a, b), min(a, b)


def check_orientation(orientation):
    if orientation not in ORIENTATIONS:
        raise ValueError("custom_gif_orientation must be one of %s, not %r" % (", ".join(ORIENTATIONS), orientation))
    return orientation


def fits(size, panel, orientation):
    """Whether a GIF of size (w, h) fits the panel (w, h in landscape) the way
    the clock stands."""
    w, h = size
    pw, ph = panel
    landscape = w <= pw and h <= ph
    portrait = w <= ph and h <= pw
    if orientation == "landscape":
        return landscape
    if orientation == "portrait":
        return portrait
    return landscape or portrait
