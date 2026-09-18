# PlatformIO post-build script: wraps firmware.bin into firmware.uf2 for the
# TinyUF2 bootloader of the MatrixPortal S3.
#
# Double-tap RESET and the board shows up as the USB drive MATRXS3BOOT; copying
# the .uf2 onto it flashes the app. TinyUF2 maps UF2 address 0x0 to the start of
# the ota_0 app partition, so the plain app image (firmware.bin, starting with the
# 0xE9 image magic) goes in from base 0 - no bootloader, no partition table.

import struct

Import("env")  # noqa: F821 - provided by PlatformIO/SCons

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY_ID = 0x00002000
UF2_FAMILY_ESP32S3 = 0xC47E5767
UF2_PAYLOAD = 256          # bytes of image data per 512-byte UF2 block
APP_BASE = 0x0             # relative to ota_0, as TinyUF2 expects


def bin_to_uf2(data):
    num_blocks = (len(data) + UF2_PAYLOAD - 1) // UF2_PAYLOAD
    out = bytearray()
    for block_no in range(num_blocks):
        offset = block_no * UF2_PAYLOAD
        chunk = data[offset:offset + UF2_PAYLOAD].ljust(UF2_PAYLOAD, b"\x00")
        header = struct.pack(
            "<8I",
            UF2_MAGIC_START0, UF2_MAGIC_START1, UF2_FLAG_FAMILY_ID,
            APP_BASE + offset, UF2_PAYLOAD, block_no, num_blocks,
            UF2_FAMILY_ESP32S3,
        )
        block = header + chunk
        block += b"\x00" * (512 - 4 - len(block))
        block += struct.pack("<I", UF2_MAGIC_END)
        out += block
    return bytes(out)


def make_uf2(source, target, env):
    bin_path = str(target[0])
    uf2_path = bin_path[:-len(".bin")] + ".uf2"
    with open(bin_path, "rb") as f:
        data = f.read()
    if not data or data[0] != 0xE9:
        raise ValueError("%s is not an ESP app image (no 0xE9 magic)" % bin_path)
    with open(uf2_path, "wb") as f:
        f.write(bin_to_uf2(data))
    print("Created %s (%d bytes of app image)" % (uf2_path, len(data)))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", make_uf2)  # noqa: F821
