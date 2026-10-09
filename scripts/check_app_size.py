# PlatformIO post-build script: stops the build when firmware.bin does not fit
# its app slot.
#
# PlatformIO's "Flash:" line counts the sections only and reads about 26 KB
# below the size of firmware.bin (headers, padding, checksum). On 2026-10-09
# an image at "99.0 %" was 6,720 bytes too large: TinyUF2, the bootloader the
# board had then, wrote only what fit, and the clock did not start.

import os
import struct

Import("env")  # noqa: F821 - provided by PlatformIO/SCons


def app_slot_size(partitions_bin):
    """Size of the ota_0 app slot in a binary partition table, or None."""
    try:
        with open(partitions_bin, "rb") as f:
            table = f.read()
    except OSError:
        return None
    for pos in range(0, len(table) - 31, 32):
        magic, ptype, subtype, _offset, size = struct.unpack_from("<HBBII", table, pos)
        if magic != 0x50AA:      # 0xAA 0x50: an entry; anything else ends the table
            break
        if ptype == 0x00 and subtype == 0x10:   # app, ota_0
            return size
    return None


def check_size(source, target, env):
    bin_path = str(target[0])
    size = os.path.getsize(bin_path)
    room = app_slot_size(env.subst("$BUILD_DIR/partitions.bin"))
    if room is None:
        print("App image: %d bytes; the app slot's size was not found" % size)
        return
    if size > room:
        raise ValueError("firmware.bin has %d bytes, %d more than the %d of the app slot" % (size, size - room, room))
    print("App image: %d bytes, %d of the app slot's %d free" % (size, room - size, room))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_size)  # noqa: F821
