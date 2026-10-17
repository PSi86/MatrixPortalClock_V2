"""Benchmarks the wall clock spike (branch feat/wallclock-spike, src/spike/wallclock_spike.cpp) on the
Waveshare ESP32-S3-RGB-Matrix over its USB console. Sets the layout and the panel placement once,
then for each driver configuration lets the spike restart, and reads back what it measured: the
refresh on address line E, the bus clock, memory, and the time a full frame takes to draw through
the panel placement (screen 4, Motion). Every console line goes to logs/bench_<label>.log, a
summary to the screen.

    ~/.platformio/penv/Scripts/python.exe bench.py <label> [depth:double:clk[:rate] ...]   e.g. 8:1:8 6:0:16:90
    ~/.platformio/penv/Scripts/python.exe bench.py <label> --cmd "<console command>" ...   just send

Only the board with USB serial 38:44:BE:CC:64:14 is opened, with DTR and RTS low (raised, RTS
resets the chip and the pair can put it into the ROM bootloader).
"""
import os, re, sys, time

import serial
from serial.tools import list_ports

SERIAL_NO = "38:44:BE:CC:64:14"
HERE = os.path.dirname(os.path.abspath(__file__))
label = sys.argv[1]
os.makedirs(os.path.join(HERE, "logs"), exist_ok=True)
log = open(os.path.join(HERE, "logs", "bench_%s.log" % label), "a", encoding="utf-8", buffering=1)

# The wall clock L as Peter cabled it (2026-10-16): the feed bottom right, the chain left along the
# bottom row, up at the third panel and right along the top row, the top row turned upside down.
# The driver's panel 1 (x 0-63) is the far end of the chain, the panel at the board its last.
PLACEMENT = ["panel 1 2 0 2", "panel 2 1 0 2", "panel 3 0 0 2", "panel 4 0 1 0", "panel 5 1 1 0", "panel 6 2 1 0"]


def find():
    for p in list_ports.comports():
        if (p.vid, p.pid) == (0x303A, 0x1001) and (p.serial_number or "").upper() == SERIAL_NO:
            return p.device
    return None


class Console:
    def __init__(self):
        self.s = None
        self.pending = b""
        self.planes = None   # the driver's lsbMsbTransitionBit at its last start

    def open(self, wait=30):
        end = time.time() + wait
        while time.time() < end:
            port = find()
            if port:
                s = serial.Serial()
                s.port, s.baudrate, s.timeout = port, 115200, 0.1
                s.dtr = False
                s.rts = False
                try:
                    s.open()
                    self.s = s
                    log.write(time.strftime("%H:%M:%S ") + "-- opened %s\n" % port)
                    return
                except serial.SerialException:
                    pass
            time.sleep(0.3)
        raise SystemExit("board %s not found" % SERIAL_NO)

    def close(self):
        if self.s:
            try:
                self.s.close()
            except serial.SerialException:
                pass
        self.s = None

    def read(self, seconds):
        lines, end = [], time.time() + seconds
        while time.time() < end:
            try:
                chunk = self.s.read(512)
            except serial.SerialException:
                self.close()
                return lines, True       # the port went away: a restart
            self.pending += chunk
            while b"\n" in self.pending:
                line, self.pending = self.pending.split(b"\n", 1)
                text = line.rstrip(b"\r").decode("utf-8", "replace")
                log.write(time.strftime("%H:%M:%S ") + text + "\n")
                lines.append(text)
                m = re.search(r"lsbMsbTransitionBit of (\d+) used", text)
                if m:
                    self.planes = int(m.group(1))
        return lines, False

    def cmd(self, line, seconds=1.5):
        self.s.write((line + "\n").encode())
        return self.read(seconds)

    def restarting(self, line):
        """Sends a command that restarts the board and waits until it is back and settled."""
        # The USB-Serial-JTAG port stays through a software restart, so the start is read on the same
        # handle; only if Windows drops the port it is opened again.
        self.s.write((line + "\n").encode())
        lines, gone = self.read(18)       # the start, then running past the refresh's first seconds
        if gone:
            time.sleep(1.0)
            self.open()
            more, _ = self.read(12)
            lines += more
        return lines


def grab(pattern, lines):
    for l in reversed(lines):
        m = re.search(pattern, l)
        if m:
            return m
    return None


def measure(con):
    lines, _ = con.cmd("show", 2)
    started = grab(r"Panel: not running", lines) is None
    r = grab(r"Refresh: (\d+) Hz measured on line E, driver's estimate (-?\d+) Hz", lines)
    bus = grab(r"/ ([\d.]+) MHz on the bus", lines)
    mem = grab(r"Memory: internal free (\d+) KB \(largest block (\d+) KB\), PSRAM free (\d+) KB; frame buffer (\d+) KB x (\d)", lines)
    con.cmd("screen 4", 0.5)
    mot, _ = con.read(7)
    frames = [int(m.group(1)) for m in (re.search(r"Motion: full frame (\d+) us", l) for l in mot) if m]
    con.cmd("screen 6", 0.5)
    return {
        "started": started,
        "hz": int(r.group(1)) if r else None, "est": int(r.group(2)) if r else None,
        "bus": float(bus.group(1)) if bus else None,
        "free": int(mem.group(1)) if mem else None, "block": int(mem.group(2)) if mem else None,
        "psram": int(mem.group(3)) if mem else None, "fb": int(mem.group(4)) if mem else None,
        "frame_us": frames[-1] if frames else None,
    "planes": con.planes,
    }


con = Console()
con.open()
if len(sys.argv) > 3 and sys.argv[2] == "--read":
    # Only reads (and logs) for that many seconds: the spike prints over USB, and with nobody
    # reading, a full send buffer stalls it at every line (seen 2026-10-17: the Motion screen
    # stopping for about 3 s at a time).
    con.read(float(sys.argv[3]))
    sys.exit(0)
if len(sys.argv) > 2 and sys.argv[2] == "--wifi":
    # The spike's WiFi test (needs src/arduino_secrets.h in its build): after a restart 30 TCP
    # connects to the router before the panel starts, then 30 at each drive strength 0..3.
    lines = con.restarting("wifi")
    more, _ = con.read(150)
    for l in lines + more:
        if "WiFi" in l or "connects" in l or "drive" in l:
            print(l)
    sys.exit(0)
if len(sys.argv) > 2 and sys.argv[2] == "--cmd":
    for c in sys.argv[3:]:
        lines, gone = con.cmd(c, 3)
        print("\n".join(lines))
        if gone:
            print("-- the board restarted")
            con.open()
            print("\n".join(con.read(12)[0]))
    sys.exit(0)

lines, _ = con.cmd("show", 2)
if not grab(r"Layout 3x2", lines):
    con.restarting("layout 3x2")
for c in PLACEMENT:
    con.cmd(c, 1)
lines, _ = con.cmd("show", 2)
print(grab(r"Layout .*", lines).group(0) if grab(r"Layout .*", lines) else "layout ?")

print("%-14s %-7s %8s %8s %9s %8s %8s %8s %10s %7s" % ("depth:dbl:clk", "start", "refresh", "driver", "bus MHz", "fb KB", "int free", "PSRAM", "frame us", "OE-only"))
for conf in sys.argv[2:]:
    parts = conf.split(":")
    depth, dbl, clk = parts[:3]
    rate = parts[3] if len(parts) > 3 else "120"
    con.planes = None
    # the driver settings one by one, each a restart; only those that differ
    lines, _ = con.cmd("show", 2)
    drv = grab(r"Driver: (\d+) bit, clock (\d+) MHz asked .* at least (\d+) Hz asked, .* (double|single) buffer", lines)
    if drv and drv.group(1) != depth:
        con.restarting("depth " + depth)
    if drv and drv.group(2) != clk:
        con.restarting("clk " + clk)
    if drv and drv.group(3) != rate:
        con.restarting("rate " + rate)
    if drv and (drv.group(4) == "double") != (dbl == "1"):
        con.restarting("double " + dbl)
    if not drv:
        for c in ("depth " + depth, "clk " + clk, "rate " + rate, "double " + dbl):
            con.restarting(c)
    if con.planes is None:                # nothing changed: a restart, for the driver's own report
        con.restarting("restart")
    con.read(4)                           # a few seconds of running: the refresh counted over whole seconds
    m = measure(con)
    log.write("RESULT %s %s\n" % (conf, m))
    print("%-14s %-7s %8s %8s %9s %8s %8s %8s %10s %7s" % (conf, "ok" if m["started"] else "FAILED", m["hz"], m["est"], m["bus"],
                                                        m["fb"], m["free"], m["psram"], m["frame_us"],
                                                        "%s of %s" % (m["planes"], depth) if m["planes"] is not None else "?"))
con.close()
