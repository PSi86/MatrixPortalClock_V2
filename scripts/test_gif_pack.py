"""Checks of the GIF pack writer (python scripts/test_gif_pack.py). The pack is
read back with a parser of its own here, written from the layout described in
gif_pack.py, the one the firmware reads."""
import glob
import os
import struct
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gif_common  # noqa: E402
import gif_pack  # noqa: E402

PANEL = (64, 32)


def read_pack(pack):
    magic, version, count, index_crc, size, meta_at, meta_size, meta_crc = struct.unpack_from("<8sHHIIIII", pack, 0)
    if meta_size == 0:
        assert pack[20:32] == bytes(12), "header tail not zero in a pack without a meta index"
    index = pack[32:32 + 56 * count]
    entries = []
    for i in range(count):
        name, w, h, codec, pad, offset, stored, gif_bytes, crc = struct.unpack_from("<32sHHB3sIIII", index, 56 * i)
        assert pad == bytes(3)
        entries.append({"name": name.rstrip(b"\0"), "w": w, "h": h, "codec": codec, "offset": offset,
                        "stored": stored, "gif": gif_bytes, "crc": crc})
    meta = pack[meta_at:meta_at + meta_size] if meta_size else b""
    return {"magic": magic, "version": version, "count": count, "index_ok": zlib.crc32(index) == index_crc,
            "size": size, "meta_at": meta_at, "meta": meta, "meta_ok": zlib.crc32(meta) == meta_crc}, entries


def sample_folder(d):
    """Some GIFs of the test's own (the repository brings none since
    2026-10-10): some that deflate, some that do not, several sizes."""
    import random
    rng = random.Random(7)
    for i, (w, h) in enumerate(((64, 32), (32, 32), (32, 64), (64, 32), (16, 16), (64, 32))):
        body = bytes(300 * (i + 1)) if i % 2 else bytes(rng.getrandbits(8) for _ in range(250 * (i + 1)))
        with open(os.path.join(d, "sample-%d.gif" % i), "wb") as f:
            f.write(b"GIF89a" + struct.pack("<HH", w, h) + body)
    return d


def sample_gifs(d):
    gifs, _too_big, _copies = gif_pack.collect([sample_folder(d)], PANEL, "landscape")
    return gifs


def test_pack_reads_back_byte_for_byte():
    with tempfile.TemporaryDirectory() as d:
        gifs = sample_gifs(d)
        originals = {gif_pack.entry_name(p): open(p, "rb").read() for p in glob.glob(os.path.join(d, "*.gif"))
                     if gif_common.fits(gif_common.gif_size(p), PANEL, "landscape")}
    pack, taken, left_out = gif_pack.layout(gifs, 10 ** 7)
    head, entries = read_pack(pack)
    assert head["magic"] == b"CLKGPACK" and head["version"] == 1 and head["index_ok"], head
    assert head["size"] == len(pack) and head["count"] == len(taken) == len(originals) and not left_out
    end = 32 + 56 * head["count"]
    for e in entries:
        assert e["offset"] == end, "GIFs not one after the other"
        end += e["stored"]
        blob = pack[e["offset"]:e["offset"] + e["stored"]]
        assert zlib.crc32(blob) == e["crc"], e["name"]
        gif = zlib.decompress(blob) if e["codec"] == gif_pack.DEFLATED else blob
        assert e["codec"] in (gif_pack.STORED, gif_pack.DEFLATED)
        assert gif == originals[e["name"]] and e["gif"] == len(gif), e["name"]
        assert (e["w"], e["h"]) == struct.unpack_from("<HH", gif, 6), e["name"]
    assert end == len(pack)


def test_each_gif_is_stored_the_smaller_way():
    noise = os.urandom(3000)                      # does not deflate
    codec, stored = gif_pack.payload(noise)
    assert codec == gif_pack.STORED and stored == noise
    flat = b"GIF89a" + bytes(3000)                # deflates well
    codec, stored = gif_pack.payload(flat)
    assert codec == gif_pack.DEFLATED and len(stored) < len(flat) and zlib.decompress(stored) == flat


def test_room_counts_header_and_index():
    with tempfile.TemporaryDirectory() as d:
        gifs = sample_gifs(d)
    assert len(gifs) == 5, len(gifs)
    whole, taken, _ = gif_pack.layout(gifs, 10 ** 7)
    exact, taken_exact, out_exact = gif_pack.layout(gifs, len(whole))
    assert taken_exact == taken and not out_exact
    _short, taken_short, out_short = gif_pack.layout(gifs, len(whole) - 1)
    assert len(taken_short) == len(taken) - 1 and len(out_short) == 1, (taken_short, out_short)


def fake_gif(folder, name, w, h, fill=0):
    path = os.path.join(folder, name + ".gif")
    with open(path, "wb") as f:
        f.write(b"GIF89a" + struct.pack("<HH", w, h) + bytes([fill]) * 200)
    return path


def test_orientation_picks_what_fits_the_way_the_clock_stands():
    with tempfile.TemporaryDirectory() as d:
        fake_gif(d, "wide", 64, 32, 1)
        fake_gif(d, "tall", 32, 64, 2)
        fake_gif(d, "square", 32, 32, 3)
        names = lambda o: {g[0] for g in gif_pack.collect([d], PANEL, o)[0]}
        assert names("landscape") == {b"wide", b"square"}, names("landscape")
        assert names("portrait") == {b"tall", b"square"}, names("portrait")
        assert names("both") == {b"wide", b"tall", b"square"}, names("both")


def test_copies_and_gifs_to_skip_are_left_out():
    import hashlib
    with tempfile.TemporaryDirectory() as d:
        a = fake_gif(d, "a", 32, 32, 7)
        fake_gif(d, "a-again", 32, 32, 7)          # same bytes
        fake_gif(d, "b", 32, 32, 8)
        gifs, _too_big, copies = gif_pack.collect([d], PANEL, "landscape")
        assert len(gifs) == 2 and copies == 1, (gifs, copies)
        # skip_sha1: the GIF library's clocks.py passes what a clock has elsewhere.
        skip = [hashlib.sha1(open(a, "rb").read()).hexdigest()]
        gifs, _too_big, copies = gif_pack.collect([d], PANEL, "landscape", skip)
        assert [g[0] for g in gifs] == [b"b"] and copies == 2, ([g[0] for g in gifs], copies)


# --- the meta index (GIFs on request) ------------------------------------------

def write_meta(folder, lines, end=True):
    with open(os.path.join(folder, gif_pack.META_FILE), "w", encoding="utf-8", newline="\n") as f:
        f.write(gif_pack.META_HEAD + "\n" + "".join(l + "\n" for l in lines) + ("END %d\n" % len(lines) if end else ""))


META_LINES = ["sample-1.gif\tA sample\tSuper Mario\tgame\t1980s 80s\tpixel-art\tMario jumps",
              "sample-3.gif\tPokémon sample\tPokemon\tcartoon-anime\t\t\ta short one",
              "not-in-the-folder.gif\tGone\t\t\t\t\t"]


def test_a_folder_without_a_meta_index_gives_the_pack_as_before():
    with tempfile.TemporaryDirectory() as d:
        pack, taken, _ = gif_pack.layout(sample_gifs(d), 10 ** 7)
    head, entries = read_pack(pack)
    assert head["meta"] == b"" and head["meta_at"] == 0 and pack[20:32] == bytes(12)
    assert head["size"] == len(pack) == entries[-1]["offset"] + entries[-1]["stored"]


def test_the_meta_index_follows_the_gifs_a_line_per_gif_in_index_order():
    with tempfile.TemporaryDirectory() as d:
        sample_folder(d)
        write_meta(d, META_LINES)
        gifs, _too_big, _copies = gif_pack.collect([d], PANEL, "landscape")
    pack, taken, _ = gif_pack.layout(gifs, 10 ** 7)
    head, entries = read_pack(pack)
    assert head["meta_ok"] and head["meta_at"] == entries[-1]["offset"] + entries[-1]["stored"], head
    assert head["size"] == len(pack) == head["meta_at"] + len(head["meta"])
    text = head["meta"].decode("utf-8").split("\n")
    assert text[0] == gif_pack.META_HEAD and text[-2] == "END %d" % len(entries) and text[-1] == "", text
    lines = text[1:-2]
    assert len(lines) == len(entries)
    for e, line in zip(entries, lines):
        name = line.split("\t")[0]
        assert name == e["name"].decode() + ".gif", (name, e["name"])
        assert len(line.split("\t")) == gif_pack.META_FIELDS, line
        if name == "sample-1.gif":
            assert line == META_LINES[0], line
        elif name == "sample-3.gif":
            assert line == META_LINES[1], line
        else:
            assert line == name + "\t" * 6, "a GIF without a line gets its name alone: %r" % line
    assert "not-in-the-folder" not in head["meta"].decode()


def test_room_counts_the_meta_index():
    with tempfile.TemporaryDirectory() as d:
        sample_folder(d)
        write_meta(d, META_LINES)
        gifs, _too_big, _copies = gif_pack.collect([d], PANEL, "landscape")
    whole, taken, _ = gif_pack.layout(gifs, 10 ** 7)
    exact, taken_exact, out_exact = gif_pack.layout(gifs, len(whole))
    assert exact == whole and taken_exact == taken and not out_exact
    short, taken_short, out_short = gif_pack.layout(gifs, len(whole) - 1)
    assert len(taken_short) == len(taken) - 1 and len(out_short) == 1, (taken_short, out_short)
    assert read_pack(short)[0]["meta_ok"] and len(short) <= len(whole) - 1


def test_a_meta_index_cut_off_or_of_another_kind_is_left_out():
    with tempfile.TemporaryDirectory() as d:
        write_meta(d, META_LINES[:2], end=False)
        assert gif_pack.read_meta(d) == {}
        with open(os.path.join(d, gif_pack.META_FILE), "w") as f:
            f.write("SOMETHING ELSE\nEND 0\n")
        assert gif_pack.read_meta(d) == {}
        write_meta(d, META_LINES[:2])
        assert sorted(gif_pack.read_meta(d)) == ["sample-1.gif", "sample-3.gif"]
        write_meta(d, ["short.gif\tonly a title"])
        assert gif_pack.read_meta(d)["short.gif"] == "short.gif\tonly a title" + "\t" * 5, "missing fields filled"


def old_clock_takes(pack, partition=4544 * 1024):
    """What a clock from before the meta index (main 760dece) checks: POST
    /api/pack in web_api.cpp, then gifPackLoad() in gif_player.cpp."""
    magic, version, count, index_crc, size = struct.unpack_from("<8sHHII", pack, 0)
    index_bytes = count * 56
    if not (32 <= len(pack) <= partition and magic == b"CLKGPACK" and version == 1 and 0 < count <= 4000
            and size == len(pack) and 32 + index_bytes <= size):
        return False
    data_from = 32 + index_bytes
    if size > partition or data_from > size or zlib.crc32(pack[32:data_from]) != index_crc:
        return False
    for i in range(count):
        _name, _w, _h, codec, offset, stored, gif_bytes, crc = struct.unpack_from("<32sHHB3xIIII", pack, 32 + 56 * i)
        codec_ok = codec == 1 or (codec == 0 and gif_bytes == stored)
        if not codec_ok or stored == 0 or offset < data_from or offset > size or stored > size - offset or gif_bytes > 614400:
            return False
        if zlib.crc32(pack[offset:offset + stored]) != crc:
            return False
    return True


def test_a_clock_from_before_the_meta_index_takes_a_pack_with_one():
    with tempfile.TemporaryDirectory() as d:
        sample_folder(d)
        write_meta(d, META_LINES)
        gifs, _too_big, _copies = gif_pack.collect([d], PANEL, "landscape")
    pack, _taken, _ = gif_pack.layout(gifs, 10 ** 7)
    assert read_pack(pack)[0]["meta"], "no meta index to try"
    assert old_clock_takes(pack)
    bad = bytearray(pack)
    bad[32 + 44] ^= 1                    # the first entry's offset: the old checks see that
    assert not old_clock_takes(bytes(bad)), "the stand-in for the old clock checks nothing"


def table_entry(label, kind, sub, offset, size):
    return b"\xaa\x50" + struct.pack("<BBII", kind, sub, offset, size) + label.encode().ljust(16, b"\0") + bytes(4)


CLOCK_8MB = (table_entry("nvs", 1, 0x02, 0x9000, 0x5000) + table_entry("otadata", 1, 0x00, 0xE000, 0x2000)
             + table_entry("app0", 0, 0x10, 0x10000, 0x1C0000) + table_entry("app1", 0, 0x11, 0x1D0000, 0x1C0000)
             + table_entry("ffat", 1, 0x81, 0x390000, 0x470000)
             + b"\xeb\xeb" + bytes(30) + b"\xff" * 2912)
TINYUF2_8MB = (table_entry("nvs", 1, 0x02, 0x9000, 0x5000) + table_entry("otadata", 1, 0x00, 0xE000, 0x2000)
               + table_entry("ota_0", 0, 0x10, 0x10000, 0x200000) + table_entry("ota_1", 0, 0x11, 0x210000, 0x200000)
               + table_entry("uf2", 0, 0x00, 0x410000, 0x40000) + table_entry("ffat", 1, 0x81, 0x450000, 0x3B0000)
               + b"\xeb\xeb" + bytes(30) + b"\xff" * 2880)


def test_only_a_board_with_this_ffat_partition_is_written():
    table = gif_pack.read_partitions(CLOCK_8MB)
    assert len(table) == 5 and table[-1] == ("ffat", 1, 0x81, 0x390000, 0x470000), table
    assert gif_pack.has_ffat(table, 0x390000, 0x470000)
    assert not gif_pack.has_ffat(table, 0x390000, 0x460000), "a smaller ffat passed"
    assert not gif_pack.has_ffat(table, 0x3A0000, 0x470000), "another offset passed"
    # A board still on TinyUF2's layout: its ffat starts where the new one does not.
    assert not gif_pack.has_ffat(gif_pack.read_partitions(TINYUF2_8MB), 0x390000, 0x470000), "the old layout passed"
    # Another board: a default 8 MB Arduino table, app partitions where the pack would go.
    other = (table_entry("nvs", 1, 0x02, 0x9000, 0x5000) + table_entry("app0", 0, 0x10, 0x10000, 0x330000)
             + table_entry("app1", 0, 0x11, 0x340000, 0x330000) + table_entry("spiffs", 1, 0x82, 0x670000, 0x180000))
    assert not gif_pack.has_ffat(gif_pack.read_partitions(other), 0x390000, 0x470000)
    assert not gif_pack.has_ffat(gif_pack.read_partitions(b"\xff" * 3072), 0x390000, 0x470000), "erased flash passed"


def test_the_built_partition_table_has_the_ffat_of_the_csv():
    project = os.path.dirname(HERE)
    built = os.path.join(project, ".pio", "build", "adafruit_matrixportal_s3", "partitions.bin")
    if not os.path.isfile(built):
        print("     (skipped: no S3 build yet)")
        return
    offset, size = gif_pack.partition(os.path.join(project, "partitions_clock_s3.csv"))
    assert (offset, size) == (0x390000, 0x470000), (hex(offset), hex(size))
    assert gif_pack.has_ffat(gif_pack.read_partitions(open(built, "rb").read()), offset, size)


def test_the_port_is_an_esp32s3_usb_serial_jtag_one():
    # COM9: another ESP32-S3 running an app with a TinyUSB port (Espressif VID, other PID).
    ports = [("COM5", 0x303A, 0x1001), ("COM6", 0x239A, 0x8125), ("COM7", 0x1395, 0x009A), ("COM8", None, None),
             ("COM9", 0x303A, 0x0002)]
    assert gif_pack.rom_ports(ports) == ["COM5"], gif_pack.rom_ports(ports)
    assert gif_pack.rom_ports(ports[1:]) == []


def test_the_mac_esptool_prints_is_read():
    out = "Chip type:          ESP32-S3 (QFN56) (revision v0.2)\nUSB mode:           USB-Serial/JTAG\n" \
          "MAC:                7c:4f:ad:06:c1:6c\nUploading stub flasher...\n"
    assert gif_pack.mac_in(out) == "7c:4f:ad:06:c1:6c"
    assert gif_pack.mac_in(out.upper()) == "7c:4f:ad:06:c1:6c"
    assert gif_pack.mac_in("Connecting...\nA fatal error occurred") is None


def test_a_table_with_sizes_in_k_is_read():
    # TinyUF2's table, as the framework has it, writes its sizes as "3776K".
    csv = os.path.expanduser("~/.platformio-pioarduino/packages/framework-arduinoespressif32/tools/partitions/"
                             "tinyuf2-partitions-8MB.csv")
    if not os.path.isfile(csv):
        print("     (skipped: %s not installed)" % csv)
        return
    assert gif_pack.partition(csv) == (0x450000, 3776 * 1024), gif_pack.partition(csv)


def test_the_pack_goes_whole_and_only_to_the_named_clock():
    import base64
    import http.server
    import json
    import threading
    got = {}

    class Clock(http.server.BaseHTTPRequestHandler):   # what the clock's /api/state and /api/pack answer
        def log_message(self, *args):
            pass

        def reply(self, obj):
            body = json.dumps(obj).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            got["auth"] = self.headers.get("Authorization")
            self.reply({"system": {"mac": "7C:4F:AD:06:C1:6C"}, "gifs": {"packMaxKb": 3776}})

        def do_POST(self):
            got["body"] = self.rfile.read(int(self.headers["Content-Length"]))
            self.reply({"ok": True, "gifs": 2, "damaged": 0, "kb": len(got["body"]) // 1024, "ms": 1234})

    server = http.server.HTTPServer(("127.0.0.1", 0), Clock)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    host, pack, quiet = "127.0.0.1:%d" % server.server_port, bytes(range(256)) * 1000, lambda text: None
    try:
        ok, text = gif_pack.send_pack(host, "secret", pack, "7c:4f:ad:06:c1:00", say=quiet)
        assert not ok and "nothing sent" in text and "body" not in got, text
        ok, text = gif_pack.send_pack(host, "secret", bytes(3776 * 1024 + 1), None, say=quiet)
        assert not ok and "nothing sent" in text and "body" not in got, text
        steps = []
        ok, text = gif_pack.send_pack(host, "secret", pack, "7c:4f:ad:06:c1:6c", say=quiet,
                                      progress=lambda sent, total: steps.append((sent, total)))
        assert ok and got["body"] == pack, text
        assert steps[-1] == (len(pack), len(pack)) and steps == sorted(steps), steps
        assert got["auth"] == "Basic " + base64.b64encode(b"clock:secret").decode(), got["auth"]
    finally:
        server.shutdown()


if __name__ == "__main__":
    failed = 0
    for name, fn in sorted(globals().items()):
        if name.startswith("test_"):
            try:
                fn()
                print("ok  ", name)
            except AssertionError as err:
                failed += 1
                print("FAIL", name, "-", err)
    raise SystemExit(1 if failed else 0)
