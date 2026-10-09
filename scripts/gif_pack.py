# The GIF pack: all the GIFs of the MatrixPortal S3 (none is built into the
# firmware since the flash layout of 2026-10-09), in the board's ffat
# partition (4544 KB), which the clock uses for nothing else. The firmware
# reads it at start-up (section "GIF pack" in the sketch).
#
# As a PlatformIO script (extra_scripts of the S3 env) it adds two targets:
#   pio run -e adafruit_matrixportal_s3 -t gifpack
#       builds .pio/build/<env>/gifpack.bin
#   pio run -e adafruit_matrixportal_s3 -t uploadgifs [--upload-port COMx]
#       builds it and writes it into the ffat partition through the ROM
#       bootloader, which esptool resets the board into over its USB-Serial/JTAG
#       (should that fail: hold BOOT, tap RESET, release BOOT). Afterwards the
#       clock starts by itself (a watchdog reset: esptool's reset through RTS
#       leaves an ESP32-S3 on its USB-Serial/JTAG in the ROM bootloader).
#       A firmware upload leaves the pack alone; "pio run -t erase" wipes it.
#   pio run -e adafruit_matrixportal_s3 -t sendgifs
#       builds it and sends it to the clock over the home network (the web
#       app's POST /api/pack), no bootloader and no cable: GIF_PACK_HOST is the
#       clock's address (matrixclock-xxxxxx.local or its IP), GIF_PACK_PASSWORD
#       its password for the home network (continuous network access has to
#       be on). The clock writes the pack as it arrives, then checks every GIF;
#       with GIF_PACK_MAC set only the clock whose /api/state names that MAC
#       gets it.
#   For uploadgifs:
#       Without --upload-port it takes the one ESP32-S3 USB-Serial/JTAG port
#       there is (USB 303A:1001: the clock's console and its ROM bootloader
#       alike). Before it
#       writes, it reads the board's partition table and writes only when the
#       board has the ffat partition this build expects, so another ESP32-S3
#       board on the PC is never written over; with GIF_PACK_MAC set (the
#       board's MAC, as esptool prints it when it connects) only that board.
# The GIFs come from gifs/ (the repository's examples, always taken) and the
# folders listed in gif_pack.local in the project folder (one per line, git
# ignores the file), or from GIF_PACK_DIRS if that is set (folders separated
# by ";" on Windows, ":" elsewhere), with their exclude.txt, and have to fit
# the panel the way the clock stands (custom_gif_panel,
# custom_gif_orientation). A second copy of a GIF is left out. They go in
# smallest first while the partition has room.
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
import re
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
ROM_USB = (0x303A, 0x1001)  # an ESP32-S3's USB-Serial/JTAG, as its ROM bootloader shows up
PART_DATA, PART_FAT = 0x01, 0x81


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
    copies and without those in skip_sha1 (the GIF library passes what a clock
    has elsewhere). Returns (gifs for layout(), number that do not fit, number
    left out as copies)."""
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


def rom_ports(ports):
    """The ESP32-S3 USB-Serial/JTAG ports among ports, [(device, vid, pid)]."""
    return [device for device, vid, pid in ports if (vid, pid) == ROM_USB]


def read_partitions(blob):
    """[(label, type, subtype, offset, size)] of a binary ESP-IDF partition
    table, as it sits in flash at 0x8000."""
    found = []
    for i in range(0, len(blob) - 31, 32):
        if blob[i:i + 2] != b"\xaa\x50":     # the MD5 entry or the empty rest
            break
        kind, sub, offset, size = struct.unpack_from("<BBII", blob, i + 2)
        found.append((blob[i + 12:i + 28].split(b"\0")[0].decode("ascii", "replace"), kind, sub, offset, size))
    return found


def has_ffat(table, offset, size):
    return ("ffat", PART_DATA, PART_FAT, offset, size) in table


def mac_in(esptool_output):
    """The MAC esptool printed when it connected, lower case, or None."""
    m = re.search(r"MAC:\s*([0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5})", esptool_output)
    return m.group(1).lower() if m else None


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


def send_pack(host, password, pack, mac=None, say=print, progress=None):
    """Sends pack (bytes) to the clock at host over its web interface: POST
    /api/pack, after /api/state has shown the clock takes a pack that large
    and, with mac, that it is the board with that MAC. say() gets the progress
    as text, progress(sent, total) after each piece. Returns (ok, what
    happened)."""
    import base64
    import http.client
    import json
    headers = {"Authorization": "Basic " + base64.b64encode(("clock:" + password).encode()).decode()} if password else {}

    def answer(response):
        body = response.read()
        try:
            return response.status, json.loads(body)
        except ValueError:
            return response.status, {"error": body.decode("utf-8", "replace")[:200]}

    try:
        conn = http.client.HTTPConnection(host, timeout=30)
        conn.request("GET", "/api/state", headers=headers)
        status, state = answer(conn.getresponse())
        conn.close()
        if status != 200:
            return False, "the clock at %s answered HTTP %d: %s - nothing sent" % (host, status, state.get("error", ""))
        board = state.get("system", {}).get("mac", "").lower()
        if mac and board != mac.lower():
            return False, "the clock at %s is %s, not %s (GIF_PACK_MAC) - nothing sent" % (host, board, mac.lower())
        room = state.get("gifs", {}).get("packMaxKb", 0) * 1024
        if len(pack) > room:
            return False, "the pack has %d bytes, the clock at %s takes %d - nothing sent" % (len(pack), host, room)
        say("GIF pack: clock %s at %s, sending %d KB" % (board, host, len(pack) // 1024))
        conn = http.client.HTTPConnection(host, timeout=60)
        conn.putrequest("POST", "/api/pack")
        for key, value in dict(headers, **{"Content-Type": "application/octet-stream",
                                            "Content-Length": str(len(pack))}).items():
            conn.putheader(key, value)
        conn.endheaders()
        step, told = 64 * 1024, 0
        for at in range(0, len(pack), step):
            piece = pack[at:at + step]
            conn.send(piece)
            if progress:
                progress(at + len(piece), len(pack))
            percent = (at + len(piece)) * 100 // len(pack)
            if percent >= told + 10 or percent == 100:
                say("GIF pack: sent %d %%" % percent)
                told = percent
        status, result = answer(conn.getresponse())
        conn.close()
    except OSError as err:
        return False, "the clock at %s did not answer: %s" % (host, err)
    if status != 200:
        return False, result.get("error", "HTTP %d" % status)
    text = "%d GIFs, %d KB written in %.1f s" % (result["gifs"], result["kb"], result["ms"] / 1000)
    if result.get("damaged"):
        return False, text + "; %d arrived damaged and do not play" % result["damaged"]
    return True, text


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
        listed = os.environ.get("GIF_PACK_DIRS")
        if listed:
            folders, source = [d for d in listed.split(os.pathsep) if d.strip()], "GIF_PACK_DIRS"
        else:
            folders, source = gif_common.listed_dirs(project, "gif_pack.local"), "gif_pack.local"
        listed = [d.strip() for d in folders if os.path.isdir(d.strip())]
        if not listed:
            print("GIF pack: %s names no folder that exists - the examples in gifs/ only" % source)
        folders = [os.path.join(project, "gifs")] + listed
        offset, capacity = where()
        gifs, too_big, copies = collect(folders, panel, orientation)
        pack, taken, left_out = layout(gifs, capacity)
        os.makedirs(os.path.dirname(out), exist_ok=True)
        with open(out, "wb") as f:
            f.write(pack)
        print("GIF pack: %d GIFs, %d of %d bytes (ffat at 0x%x); %d did not fit the room left, %d do not fit "
              "the panel, %d left out as copies" % (len(taken), len(pack), capacity, offset,
                                                                len(left_out), too_big, copies))
        print("GIF pack: from %s - %s" % (", ".join(folders), out))
        return 0

    def upload_pack(target, source, env):  # noqa: ARG001
        port = env.subst("$UPLOAD_PORT")
        if not port:
            from serial.tools import list_ports
            found = rom_ports([(p.device, p.vid, p.pid) for p in list_ports.comports()])
            if not found:
                sys.stderr.write("GIF pack: no ESP32-S3 on its USB-Serial/JTAG (USB 303A:1001) - is the clock "
                                 "plugged in? If it does not answer: hold BOOT, tap RESET, release BOOT, try again\n")
                return 1
            if len(found) > 1:
                sys.stderr.write("GIF pack: several ESP32-S3 USB ports (%s) - name one with --upload-port\n"
                                 % ", ".join(found))
                return 1
            port = found[0]
            print("GIF pack: ESP32-S3 USB-Serial/JTAG on %s" % port)
        offset, size = where()
        esptool = [env.subst("$UPLOADER").strip('"'), "--chip", env.BoardConfig().get("build.mcu"), "--port", port,
                   "--baud", env.subst("$UPLOAD_SPEED")]
        # Whose board is it: its MAC and its partition table, before anything is written.
        table_file = os.path.join(os.path.dirname(out), "partitions_on_board.bin")
        read = subprocess.run(esptool + ["--before", "default-reset", "--after", "no-reset",
                                         "read-flash", "0x8000", "0xc00", table_file], capture_output=True, text=True)
        sys.stdout.write(read.stdout)
        sys.stderr.write(read.stderr)
        if read.returncode != 0:
            sys.stderr.write("GIF pack: could not read the board's partition table - nothing written\n")
            return read.returncode
        mac, wanted = mac_in(read.stdout), os.environ.get("GIF_PACK_MAC", "").strip().lower()
        problem = None
        if wanted and mac != wanted:
            problem = "the board on %s is %s, not %s (GIF_PACK_MAC)" % (port, mac, wanted)
        elif not has_ffat(read_partitions(open(table_file, "rb").read()), offset, size):
            problem = "the board on %s has no ffat partition at 0x%x of %d bytes" % (port, offset, size)
        if problem:
            sys.stderr.write("GIF pack: %s - nothing written\n" % problem)
            # Out of the ROM bootloader into its firmware again; a reset through RTS would leave it there.
            subprocess.call(esptool + ["--before", "no-reset", "--after", "watchdog-reset", "read-mac"])
            return 1
        print("GIF pack: board %s, ffat at 0x%x as expected - writing" % (mac, offset))
        return subprocess.call(esptool + ["--before", "default-reset", "--after", "watchdog-reset",
                                          "write-flash", "-z", "0x%x" % offset, out])

    def send_to_clock(target, source, env):   # noqa: ARG001
        host = os.environ.get("GIF_PACK_HOST", "").strip()
        if not host:
            sys.stderr.write("GIF pack: GIF_PACK_HOST names no clock - its address on the home network, "
                             "matrixclock-xxxxxx.local or its IP\n")
            return 1
        ok, text = send_pack(host, os.environ.get("GIF_PACK_PASSWORD", ""), open(out, "rb").read(),
                             os.environ.get("GIF_PACK_MAC", "").strip() or None)
        (sys.stdout if ok else sys.stderr).write("GIF pack: %s\n" % text)
        return 0 if ok else 1

    env.AddCustomTarget(name="gifpack", dependencies=None, actions=[build_pack],
                        title="Build GIF pack", description="GIFs of gif_pack.local -> gifpack.bin")
    env.AddCustomTarget(name="uploadgifs", dependencies=None, actions=[build_pack, upload_pack],
                        title="Upload GIF pack", description="gifpack.bin -> ffat partition (ROM bootloader)")
    env.AddCustomTarget(name="sendgifs", dependencies=None, actions=[build_pack, send_to_clock],
                        title="Send GIF pack", description="gifpack.bin -> the clock over the home network")


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
    pack, taken, left_out = layout(gifs, int(opts.get("--size", str(4544 * 1024)), 0))
    with open(args[0], "wb") as f:
        f.write(pack)
    print("%d GIFs, %d bytes; %d did not fit the room left, %d do not fit the panel, %d copies"
          % (len(taken), len(pack), len(left_out), too_big, copies))
elif "Import" in globals():   # run by PlatformIO (SCons) as an extra script
    Import("env")  # noqa: F821 - provided by PlatformIO/SCons
    _pio(env)  # noqa: F821
