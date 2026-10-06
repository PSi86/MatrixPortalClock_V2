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

REPO_GIFS = os.path.join(os.path.dirname(HERE), "gifs")
PANEL = (64, 32)


def read_pack(pack):
    magic, version, count, index_crc, size = struct.unpack_from("<8sHHII", pack, 0)
    assert pack[20:32] == bytes(12), "header tail not zero"
    index = pack[32:32 + 56 * count]
    entries = []
    for i in range(count):
        name, w, h, codec, pad, offset, stored, gif_bytes, crc = struct.unpack_from("<32sHHB3sIIII", index, 56 * i)
        assert pad == bytes(3)
        entries.append({"name": name.rstrip(b"\0"), "w": w, "h": h, "codec": codec, "offset": offset,
                        "stored": stored, "gif": gif_bytes, "crc": crc})
    return {"magic": magic, "version": version, "count": count, "index_ok": zlib.crc32(index) == index_crc,
            "size": size}, entries


def repo_gifs():
    gifs, _too_big, _copies = gif_pack.collect([REPO_GIFS], PANEL, "landscape")
    return gifs


def test_pack_reads_back_byte_for_byte():
    originals = {gif_pack.entry_name(p): open(p, "rb").read() for p in glob.glob(os.path.join(REPO_GIFS, "*.gif"))}
    pack, taken, left_out = gif_pack.layout(repo_gifs(), 10 ** 7)
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
    gifs = repo_gifs()
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


def test_copies_and_built_in_gifs_are_left_out():
    import hashlib
    with tempfile.TemporaryDirectory() as d:
        a = fake_gif(d, "a", 32, 32, 7)
        fake_gif(d, "a-again", 32, 32, 7)          # same bytes
        fake_gif(d, "b", 32, 32, 8)
        gifs, _too_big, copies = gif_pack.collect([d], PANEL, "landscape")
        assert len(gifs) == 2 and copies == 1, (gifs, copies)
        built_in = [hashlib.sha1(open(a, "rb").read()).hexdigest()]
        gifs, _too_big, copies = gif_pack.collect([d], PANEL, "landscape", built_in)
        assert [g[0] for g in gifs] == [b"b"] and copies == 2, ([g[0] for g in gifs], copies)


def table_entry(label, kind, sub, offset, size):
    return b"\xaa\x50" + struct.pack("<BBII", kind, sub, offset, size) + label.encode().ljust(16, b"\0") + bytes(4)


TINYUF2_8MB = (table_entry("nvs", 1, 0x02, 0x9000, 0x5000) + table_entry("otadata", 1, 0x00, 0xE000, 0x2000)
               + table_entry("ota_0", 0, 0x10, 0x10000, 0x200000) + table_entry("ota_1", 0, 0x11, 0x210000, 0x200000)
               + table_entry("uf2", 0, 0x00, 0x410000, 0x40000) + table_entry("ffat", 1, 0x81, 0x450000, 0x3B0000)
               + b"\xeb\xeb" + bytes(30) + b"\xff" * 2880)


def test_only_a_board_with_this_ffat_partition_is_written():
    table = gif_pack.read_partitions(TINYUF2_8MB)
    assert len(table) == 6 and table[-1] == ("ffat", 1, 0x81, 0x450000, 0x3B0000), table
    assert gif_pack.has_ffat(table, 0x450000, 0x3B0000)
    assert not gif_pack.has_ffat(table, 0x450000, 0x3A0000), "a smaller ffat passed"
    assert not gif_pack.has_ffat(table, 0x410000, 0x3B0000), "another offset passed"
    # Another board: a default 8 MB Arduino table, app partitions where the pack would go.
    other = (table_entry("nvs", 1, 0x02, 0x9000, 0x5000) + table_entry("app0", 0, 0x10, 0x10000, 0x330000)
             + table_entry("app1", 0, 0x11, 0x340000, 0x330000) + table_entry("spiffs", 1, 0x82, 0x670000, 0x180000))
    assert not gif_pack.has_ffat(gif_pack.read_partitions(other), 0x450000, 0x3B0000)
    assert not gif_pack.has_ffat(gif_pack.read_partitions(b"\xff" * 3072), 0x450000, 0x3B0000), "erased flash passed"


def test_the_built_partition_table_has_the_ffat_of_the_csv():
    built = os.path.join(os.path.dirname(HERE), ".pio", "build", "adafruit_matrixportal_s3", "partitions.bin")
    if not os.path.isfile(built):
        print("     (skipped: no S3 build yet)")
        return
    assert gif_pack.has_ffat(gif_pack.read_partitions(open(built, "rb").read()), 0x450000, 3776 * 1024)


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


def test_ffat_partition_of_the_board_table():
    csv = os.path.expanduser("~/.platformio-pioarduino/packages/framework-arduinoespressif32/tools/partitions/"
                             "tinyuf2-partitions-8MB.csv")
    if not os.path.isfile(csv):
        print("     (skipped: %s not installed)" % csv)
        return
    assert gif_pack.partition(csv) == (0x450000, 3776 * 1024), gif_pack.partition(csv)


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
