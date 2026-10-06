# The GIF pack: more GIFs for the MatrixPortal S3 than fit into the firmware,
# in the board's ffat partition (3776 KB), which the clock uses for nothing
# else. The firmware reads it at start-up (section "GIF pack" in the sketch).
#
# As a PlatformIO script (extra_scripts of the S3 env) it adds two targets:
#   pio run -e adafruit_matrixportal_s3 -t gifpack
#       builds .pio/build/<env>/gifpack.bin
#   pio run -e adafruit_matrixportal_s3 -t uploadgifs --upload-port COMx
#       builds it and writes it into the ffat partition. Only the ROM bootloader
#       takes it: hold BOOT, tap RESET, release BOOT; press RESET afterwards.
#       A UF2 copy cannot write it (TinyUF2 writes the app partition only), so
#       uploading the firmware by UF2 leaves the pack alone; "pio run -t erase"
#       wipes it.
# The GIFs come from the folders listed in gif_pack.local in the project folder
# (one per line, git ignores the file), with their exclude.txt, and have to fit
# the panel the way the clock stands (custom_gif_panel, custom_gif_orientation,
# as for the GIFs built in). A GIF the firmware has built in anyway is left
# out, and so is a second copy. They go in smallest first while the partition
# has room.
#
# Each GIF is kept as it is or deflated (zlib, level 9), whichever is smaller;
# the clock inflates a deflated one into PSRAM with the inflate in the chip's
# ROM before it plays.
#
# Layout (little-endian), the one the firmware reads:
#   header, 32 bytes: magic "CLKGPACK", version (u16) = 1, count (u16),
#     CRC-32 of the index (u32), pack size in bytes (u32), 12 bytes zero
#   index, count x 56 bytes: name (32 bytes, NUL-terminated), width (u16),
#     height (u16), codec (u8: 0 stored, 1 deflated), 3 bytes zero, offset
#     from the start of the pack (u32), bytes stored (u32), bytes of the GIF
#     itself (u32), CRC-32 of the bytes stored (u32)
#   then the GIFs' bytes, one after the other.
#
# Without PlatformIO, for a test or an image of one's own:
#   python scripts/gif_pack.py OUT.bin FOLDER... [--panel 64x32]
#       [--orientation landscape] [--size BYTES]

import hashlib
import os
import struct
import subprocess
import sys
import zlib

MAGIC = b"CLKGPACK"
VERSION = 1
HEADER = struct.Struct("<8sHHII12x")
ENTRY = struct.Struct("<32sHHB3xIIII")
STORED, DEFLATED = 0, 1
COUNT_MAX = 4000            # the firmware refuses a pack with more
GIF_MAX = 614400            # nor a GIF larger than this once inflated


def payload(data):
    """(codec, bytes to store) for one GIF: deflated when that is smaller."""
    packed = zlib.compress(data, 9)
    return (DEFLATED, packed) if len(packed) < len(data) else (STORED, data)


def entry_name(path):
    stem = os.path.splitext(os.path.basename(path))[0]
    return stem.encode("ascii", "replace")[:31]


def layout(gifs, capacity):
    """gifs: [(name bytes, width, height, data)] in the order to take them.
    Takes each that still fits into capacity bytes (header and index
    included) and returns (pack bytes, taken, left out), the last two as lists
    of names."""
    chosen, left_out, used = [], [], HEADER.size
    for name, w, h, data in gifs:
        codec, stored = payload(data)
        cost = ENTRY.size + len(stored)
        if len(chosen) >= COUNT_MAX or len(data) > GIF_MAX or used + cost > capacity:
            left_out.append(name)
            continue
        chosen.append((name, w, h, codec, stored, len(data)))
        used += cost
    offset = HEADER.size + ENTRY.size * len(chosen)
    index, blobs = b"", []
    for name, w, h, codec, stored, gif_bytes in chosen:
        index += ENTRY.pack(name, w, h, codec, offset, len(stored), gif_bytes, zlib.crc32(stored))
        blobs.append(stored)
        offset += len(stored)
    pack = HEADER.pack(MAGIC, VERSION, len(chosen), zlib.crc32(index), offset) + index + b"".join(blobs)
    assert len(pack) == used
    return pack, [c[0] for c in chosen], left_out


def collect(folders, panel, orientation, skip_sha1=()):
    """The GIFs of the folders that fit, smallest stored size first, without
    copies and without those in skip_sha1. Returns (gifs for layout(), number
    that do not fit, number left out as copies)."""
    import gif_common
    seen, found, too_big, copies = set(skip_sha1), [], 0, 0
    for folder in folders:
        skip = gif_common.exclusions(folder)
        for path in gif_common.gifs_in(folder):
            if gif_common.excluded(path, skip):
                continue
            size = gif_common.gif_size(path)
            if not size:
                continue
            if not gif_common.fits(size, panel, orientation):
                too_big += 1
                continue
            data = open(path, "rb").read()
            digest = hashlib.sha1(data).hexdigest()
            if digest in seen:
                copies += 1
                continue
            seen.add(digest)
            found.append((len(payload(data)[1]), path, size, data))
    found.sort(key=lambda f: (f[0], f[1]))
    return [(entry_name(p), s[0], s[1], d) for _n, p, s, d in found], too_big, copies


def partition(csv_path, name="ffat"):
    """(offset, size) of a partition in an ESP-IDF partition table CSV."""
    def number(text):
        text = text.strip()
        if text.upper().endswith("K"):
            return int(text[:-1], 0) * 1024
        if text.upper().endswith("M"):
            return int(text[:-1], 0) * 1024 * 1024
        return int(text, 0)
    with open(csv_path, encoding="utf-8") as f:
        for line in f:
            cols = [c.strip() for c in line.split("#")[0].split(",")]
            if len(cols) >= 5 and cols[0] == name:
                return number(cols[3]), number(cols[4])
    raise ValueError("no partition %s in %s" % (name, csv_path))


def _pio(env):
    project = env.subst("$PROJECT_DIR")
    sys.path.insert(0, os.path.join(project, "scripts"))
    import gif_common
    out = os.path.join(env.subst("$BUILD_DIR"), "gifpack.bin")

    def where():
        return partition(env.subst("$PARTITIONS_TABLE_CSV"))

    def build_pack(target, source, env):   # noqa: ARG001 - SCons passes env by name
        panel = gif_common.panel_size(env.GetProjectOption("custom_gif_panel", "64x32"))
        orientation = gif_common.check_orientation(env.GetProjectOption("custom_gif_orientation", "landscape"))
        folders = [d for d in gif_common.listed_dirs(project, "gif_pack.local") if os.path.isdir(d)]
        if not folders:
            sys.stderr.write("GIF pack: gif_pack.local lists no folder that exists\n")
            return 1
        offset, capacity = where()
        built_in = env.get("GIF_BUILT_IN_SHA1", "").split()
        gifs, too_big, copies = collect(folders, panel, orientation, built_in)
        pack, taken, left_out = layout(gifs, capacity)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, "wb") as f:
            f.write(pack)
        print("GIF pack: %d GIFs, %d of %d bytes (ffat at 0x%x); %d did not fit the room left, %d do not fit "
              "the panel, %d left out as copies or built in" % (len(taken), len(pack), capacity, offset,
                                                                len(left_out), too_big, copies))
        print("GIF pack: from %s - %s" % (", ".join(folders), out))
        return 0

    def upload_pack(target, source, env):  # noqa: ARG001
        port = env.subst("$UPLOAD_PORT")
        if not port:
            sys.stderr.write("GIF pack: give the ROM bootloader's port with --upload-port\n")
            return 1
        offset, _capacity = where()
        cmd = [env.subst("$UPLOADER").strip('"'), "--chip", env.BoardConfig().get("build.mcu"), "--port", port,
               "--baud", env.subst("$UPLOAD_SPEED"), "--before", "default-reset", "--after", "hard-reset",
               "write-flash", "-z", "0x%x" % offset, out]
        print(" ".join(cmd))
        return subprocess.call(cmd)

    env.AddCustomTarget(name="gifpack", dependencies=None, actions=[build_pack],
                        title="Build GIF pack", description="GIFs of gif_pack.local -> gifpack.bin")
    env.AddCustomTarget(name="uploadgifs", dependencies=None, actions=[build_pack, upload_pack],
                        title="Upload GIF pack", description="gifpack.bin -> ffat partition (ROM bootloader)")


if __name__ == "__main__" and "Import" not in globals():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import gif_common
    args = sys.argv[1:]
    opts = {}
    for key in ("--panel", "--orientation", "--size"):
        if key in args:
            i = args.index(key)
            opts[key] = args[i + 1]
            del args[i:i + 2]
    if len(args) < 2:
        sys.exit(__doc__ or "usage: gif_pack.py OUT.bin FOLDER... [--panel 64x32] [--orientation landscape] [--size BYTES]")
    gifs, too_big, copies = collect(args[1:], gif_common.panel_size(opts.get("--panel", "64x32")),
                                    gif_common.check_orientation(opts.get("--orientation", "landscape")))
    pack, taken, left_out = layout(gifs, int(opts.get("--size", str(3776 * 1024)), 0))
    with open(args[0], "wb") as f:
        f.write(pack)
    print("%d GIFs, %d bytes; %d did not fit the room left, %d do not fit the panel, %d copies"
          % (len(taken), len(pack), len(left_out), too_big, copies))
elif "Import" in globals():   # run by PlatformIO (SCons) as an extra script
    Import("env")  # noqa: F821 - provided by PlatformIO/SCons
    _pio(env)  # noqa: F821
