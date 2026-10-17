"""A small web page on this PC that drives the wall clock spike's test firmware over its USB console:
test images, gamma modes, brightness, the bar, screens, refresh and buffer. Peter (2026-10-17): switch
quickly himself, without the black gaps of scripted tests.

    ~/.platformio/penv/Scripts/python.exe panel_remote.py [--port 8766]
    then open http://127.0.0.1:8766

While it runs it holds the board's console (only USB serial 38:44:BE:CC:64:14, DTR and RTS low), so
nothing else can use the port. Brightness above 64 only with a dark view: choosing a bright one sets
32 first (the test firmware does the same on its BOOT button). Console lines go to logs/remote.log.
"""
import argparse, collections, json, os, re, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import serial
from serial.tools import list_ports

SERIAL_NO = "38:44:BE:CC:64:14"
HERE = os.path.dirname(os.path.abspath(__file__))
os.makedirs(os.path.join(HERE, "logs"), exist_ok=True)
LOG = open(os.path.join(HERE, "logs", "remote.log"), "a", encoding="utf-8", buffering=1)
BRIGHT_LIMIT = 64

# The test images come from the board ("gifs" lists them in its own order). Names it knows get a
# German label; the dark ones may be shown above BRIGHT_LIMIT (each estimated below one white panel
# at full brightness). Any other GIF counts as bright.
LABELS = {"raw_grey_match": "Grauabgleich (roh)", "colour_chart": "Farbtafel", "raw_steps_dark": "Dunkle Stufen roh 0-30",
          "steps_16": "Farbstufen 0-255", "steps_dark": "Dunkle Stufen 0-60", "bars_slow": "Balken langsam (GIF)",
          "motion_bars": "Bewegte Balken (GIF)", "ramps_scroll": "Wandernde Rampen", "aliens_l_wave": "Aliens-Welle auf L",
          "campfire_tent_in_cave": "Lagerfeuer", "great_white_shark_underwater": "Hai"}
DARK_NAMES = {"raw_grey_match", "raw_steps_dark", "steps_dark", "bars_slow", "motion_bars", "aliens_l_wave",
              "campfire_tent_in_cave", "great_white_shark_underwater"}
gifs = {}                      # number (from 1) -> name, as the board listed them

state = {"image": None, "screen": None, "gamma": None, "bright": None, "bar": 0, "fill": False,
         "depth": None, "double": None, "rate": None, "refresh": None, "busy": "", "connected": False,
         "images": []}


def dark(n):
    return gifs.get(n) in DARK_NAMES
lines = collections.deque(maxlen=60)
lock = threading.Lock()
port = {"s": None}
panel_up = threading.Event()   # set when the board reports its panel running (or failing) after a start


def find():
    for p in list_ports.comports():
        if (p.vid, p.pid) == (0x303A, 0x1001) and (p.serial_number or "").upper() == SERIAL_NO:
            return p.device
    return None


def parse(text):
    m = re.search(r"GIF list (\d+): (\S+) ", text)
    if m:
        gifs[int(m.group(1))] = m.group(2)
        state["images"] = [[n, LABELS.get(gifs[n], gifs[n]), dark(n)] for n in sorted(gifs)]
        return
    m = re.search(r"GIF (\d+)/\d+: (\S+)", text)
    if m:
        state.update(image=int(m.group(1)), screen=5, fill=False, bar=0)
    m = re.search(r"Screen (\d+): (\w+)", text)
    if m:
        state.update(screen=int(m.group(1)), fill=False, bar=0)
    m = re.search(r"Brightness (\d+)", text)
    if m:
        state["bright"] = int(m.group(1))
    m = re.search(r"GIF gamma (\d)", text)
    if m:
        state["gamma"] = int(m.group(1))
    m = re.search(r"Bar: (\d+) LEDs a second", text)
    if m:
        state.update(bar=int(m.group(1)), fill=False)
    if text.startswith("Fill:"):
        state["fill"] = True
    m = re.search(r"Driver: (\d+) bit, .* at least (\d+) Hz asked, .* (double|single) buffer, .* brightness (\d+)", text)
    if m:
        state.update(depth=int(m.group(1)), rate=int(m.group(2)), double=m.group(3) == "double", bright=int(m.group(4)))
    m = re.search(r"Refresh: (\d+) Hz measured", text)
    if m and int(m.group(1)) > 0:
        state["refresh"] = int(m.group(1))
    if "Reset reason" in text:
        state.update(image=None, refresh=None, gamma=1, bar=0, fill=False)
    if "Panel: running" in text or "did not start" in text:
        panel_up.set()


def reader():
    pending = b""
    while True:
        s = port["s"]
        if s is None:
            dev = find()
            if dev:
                s = serial.Serial()
                s.port, s.baudrate, s.timeout = dev, 115200, 0.1
                s.dtr = False
                s.rts = False
                try:
                    s.open()
                    port["s"] = s
                    state["connected"] = True
                    send("show")
                    send("gifs")
                except serial.SerialException:
                    time.sleep(1)
            else:
                time.sleep(1)
            continue
        try:
            chunk = s.read(512)
        except serial.SerialException:
            port["s"] = None
            state["connected"] = False
            continue
        pending += chunk
        while b"\n" in pending:
            line, pending = pending.split(b"\n", 1)
            text = line.rstrip(b"\r").decode("utf-8", "replace")
            LOG.write(time.strftime("%H:%M:%S ") + text + "\n")
            with lock:
                lines.append(time.strftime("%H:%M:%S ") + text)
                parse(text)


def send(cmd):
    s = port["s"]
    if s is None:
        raise RuntimeError("Board nicht verbunden")
    LOG.write(time.strftime("%H:%M:%S ") + "<< " + cmd + "\n")
    s.write((cmd + "\n").encode())
    time.sleep(0.15)


def bright_view():
    """Whether what is on show must not run above BRIGHT_LIMIT."""
    if state["bar"] or state["fill"]:
        return False
    return not (state["screen"] == 5 and dark(state["image"]))


def guard():
    if (state["bright"] or 0) > BRIGHT_LIMIT:
        send("bright 32")


def restart_job(cmds):
    """Commands that restart the board, one after the other, each once the panel runs again."""
    def run():
        try:
            for c in cmds:
                state["busy"] = "Neustart: " + c
                with lock:
                    lines.append(time.strftime("%H:%M:%S ") + "-- " + c + " (Neustart)")
                panel_up.clear()
                send(c)
                panel_up.wait(25)
                time.sleep(1.5)
            send("show")
            send("gifs")
        finally:
            state["busy"] = ""
    threading.Thread(target=run, daemon=True).start()


def act(action, arg):
    if state["busy"]:
        return "Bitte warten: " + state["busy"]
    if action == "image":
        n = int(arg)
        if not dark(n):
            guard()
        send("gif %d" % n)
    elif action == "gamma":
        send("gamma %d" % int(arg))
    elif action == "bright":
        v = int(arg)
        if v > BRIGHT_LIMIT and bright_view():
            return "Helligkeit über %d nur bei dunklen Bildern oder dem Balken" % BRIGHT_LIMIT
        send("bright %d" % v)
    elif action == "bar":
        if arg == "off":
            send("bar off")
        else:
            if state["double"]:
                return "Der Balken braucht den einfachen Puffer"
            send("bar %d" % int(arg))
    elif action == "screen":
        guard()
        send("screen %d" % int(arg))
    elif action == "black":
        send("fill 5 0 0 0")
    elif action == "flipwait":
        send("flipwait %d" % int(arg))
    elif action == "rate":
        guard()
        restart_job(["rate %d" % int(arg)])
    elif action == "buffer":
        guard()
        if arg == "8single":
            cmds = (["depth 8"] if state["depth"] != 8 else []) + (["double 0"] if state["double"] else [])
        else:
            cmds = (["depth 5"] if state["depth"] != 5 else []) + (["double 1"] if not state["double"] else [])
        if cmds:
            restart_job(cmds)
    elif action == "show":
        send("show")
    elif action == "raw":
        cmd = str(arg).strip()
        m = re.match(r"bright\s+(\d+)", cmd)
        if m and int(m.group(1)) > BRIGHT_LIMIT and bright_view():
            return "Helligkeit über %d nur bei dunklen Bildern oder dem Balken" % BRIGHT_LIMIT
        if cmd:
            send(cmd)
    else:
        return "unbekannt: " + action
    return ""


PAGE = """<!doctype html><html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Wanduhr L - Panel-Test</title><style>
:root{--bg:#111;--fg:#e8e8e8;--mut:#9a9a9a;--btn:#2a2a2a;--on:#2f6f3f;--warn:#a05a1a;--line:#333}
body{background:var(--bg);color:var(--fg);font:15px system-ui,sans-serif;margin:0 auto;padding:12px 16px;max-width:980px}
h1{font-size:19px;margin:4px 0 10px} h2{font-size:14px;color:var(--mut);margin:16px 0 6px;font-weight:600;text-transform:uppercase;letter-spacing:.04em}
.row{display:flex;flex-wrap:wrap;gap:6px} button{background:var(--btn);color:var(--fg);border:1px solid var(--line);border-radius:6px;padding:8px 11px;font:inherit;cursor:pointer}
button.on{background:var(--on);border-color:#4c9a5f} button:disabled{opacity:.35;cursor:not-allowed} .dark::before{content:"\\25CF ";color:#6cf}
#status{background:#1b1b1b;border:1px solid var(--line);border-radius:6px;padding:8px 10px;line-height:1.6}
#msg{color:#f0a050;min-height:1.2em;margin-top:6px} pre{background:#0b0b0b;border:1px solid var(--line);border-radius:6px;padding:8px;height:190px;overflow:auto;font-size:12px;margin:0}
input{background:#0b0b0b;color:var(--fg);border:1px solid var(--line);border-radius:6px;padding:8px;font:inherit;flex:1;min-width:180px}
.note{color:var(--mut);font-size:13px}
</style></head><body>
<h1>Wanduhr L &ndash; Panel-Test</h1>
<div id="status">verbinde&hellip;</div><div id="msg"></div>
<h2>Testbilder</h2><div class="row" id="images"></div>
<div class="note">&#9679; dunkel: auch &uuml;ber Helligkeit 64 erlaubt. Ein helles Bild stellt eine h&ouml;here Helligkeit vorher auf 32.</div>
<h2>Gamma (bei GIFs; &bdquo;roh&ldquo;-Bilder immer ohne)</h2><div class="row" id="gamma"></div>
<h2>Helligkeit</h2><div class="row" id="bright"></div>
<h2>Bewegung</h2><div class="row" id="bar"></div>
<div class="note">Balken: nur die ge&auml;nderten Spalten werden geschrieben (wie die Uhr), 1 LED pro Schritt. Nur mit einfachem Puffer.</div>
<h2>Bildschirme</h2><div class="row" id="screens"></div>
<h2>Panel-Einstellungen (Neustart, ca. 10 s dunkel)</h2><div class="row" id="panel"></div>
<h2>Konsole</h2><pre id="log"></pre>
<div class="row" style="margin-top:6px"><input id="raw" placeholder="Befehl, z.B. show, help, fill 5 ramp"><button onclick="raw()">Senden</button></div>
<script>
let IMAGES=[],imgKey="";
const GAMMA=[[0,"0 roh"],[1,"1 wie die Uhr (2.2)"],[2,"2 Mindestwert"],[3,"3 sRGB"],[4,"4 Dithering"]];
const BRIGHT=[8,16,32,64,128,255];
const BAR=[[20,"Balken 20 LED/s"],[60,"60 LED/s"],[120,"120 LED/s"],[240,"240 LED/s"],["off","Balken aus"]];
const SCREENS=[[1,"Chain"],[2,"Layout"],[3,"Rampen"],[4,"Streifen (alt)"],[5,"Testbilder"],[6,"Info"]];
const PANEL=[["rate",120,"169 Hz (Mindestrate 120)"],["rate",90,"124 Hz (Mindestrate 90)"],["buffer","8single","8 Bit, einfacher Puffer"],["buffer","5double","5 Bit, doppelter Puffer"],["flipwait",1,"Flip-Warten 1"],["flipwait",3,"Flip-Warten 3"]];
let st={};
function b(parent,label,fn,cls){const e=document.createElement("button");e.textContent=label;if(cls)e.className=cls;e.onclick=fn;document.getElementById(parent).appendChild(e);return e}
async function act(action,arg){const r=await fetch("/api",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({action,arg})});const j=await r.json();document.getElementById("msg").textContent=j.msg||"";poll()}
function raw(){const i=document.getElementById("raw");act("raw",i.value);i.value=""}
document.getElementById("raw").addEventListener("keydown",e=>{if(e.key==="Enter")raw()});
const btn={img:{},gam:{},bri:{},bar:{},scr:{}};
function buildImages(list){const key=JSON.stringify(list);if(key===imgKey)return;imgKey=key;IMAGES=list;
 document.getElementById("images").innerHTML="";btn.img={};
 list.forEach(([n,name,dark])=>btn.img[n]=b("images",n+" "+name,()=>act("image",n),dark?"dark":""))}
GAMMA.forEach(([m,l])=>btn.gam[m]=b("gamma",l,()=>act("gamma",m)));
BRIGHT.forEach(v=>btn.bri[v]=b("bright",String(v),()=>act("bright",v)));
BAR.forEach(([v,l])=>btn.bar[v]=b("bar",l,()=>act("bar",v)));
SCREENS.forEach(([n,l])=>btn.scr[n]=b("screens",l,()=>act("screen",n)));
b("screens","Alles schwarz",()=>act("black"));
PANEL.forEach(([a,v,l])=>b("panel",l,()=>act(a,v)));
b("panel","Status (show)",()=>act("show"));
async function poll(){try{const r=await fetch("/state");const j=await r.json();st=j.state;render(j)}catch(e){document.getElementById("status").textContent="keine Verbindung zum PC-Programm"}}
function render(j){const s=j.state;
 buildImages(s.images||[]);
 const img=IMAGES.find(x=>x[0]===s.image);
 const view=s.bar?("Balken "+s.bar+" LED/s"):s.fill?"Schwarz / ein Panel":(s.screen===5?(img?("Testbild "+img[0]+" "+img[1]):"Testbilder"):(SCREENS.find(x=>x[0]===s.screen)||[0,"?"])[1]);
 document.getElementById("status").innerHTML=(s.connected?"":"<b>Board nicht verbunden</b> &middot; ")+"Zu sehen: <b>"+view+"</b> &middot; Gamma "+(s.gamma??"?")+" &middot; Helligkeit <b>"+(s.bright??"?")+"</b><br>"+
  (s.depth??"?")+" Bit, "+(s.double==null?"?":s.double?"doppelter":"einfacher")+" Puffer &middot; Mindestrate "+(s.rate??"?")+" Hz &middot; gemessen "+(s.refresh??"?")+" Hz"+(s.busy?" &middot; <b>"+s.busy+"</b>":"");
 for(const n in btn.img)btn.img[n].classList.toggle("on",s.screen===5&&!s.bar&&!s.fill&&s.image===+n);
 for(const m in btn.gam)btn.gam[m].classList.toggle("on",s.gamma===+m);
 const brightView=!(s.bar||s.fill||(s.screen===5&&IMAGES.some(x=>x[0]===s.image&&x[2])));
 for(const v in btn.bri){btn.bri[v].classList.toggle("on",s.bright===+v);btn.bri[v].disabled=(+v>64&&brightView)}
 for(const v in btn.bar)btn.bar[v].classList.toggle("on",v!=="off"&&s.bar===+v);
 for(const n in btn.scr)btn.scr[n].classList.toggle("on",!s.bar&&!s.fill&&s.screen===+n);
 const log=document.getElementById("log");const atEnd=log.scrollTop+log.clientHeight>=log.scrollHeight-4;log.textContent=j.lines.join("\\n");if(atEnd)log.scrollTop=log.scrollHeight}
poll();setInterval(poll,800);
</script></body></html>"""


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def reply(self, code, body, ctype):
        data = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path == "/":
            self.reply(200, PAGE, "text/html; charset=utf-8")
        elif self.path == "/state":
            with lock:
                body = json.dumps({"state": state, "lines": list(lines)[-40:]})
            self.reply(200, body, "application/json")
        else:
            self.reply(404, "not found", "text/plain")

    def do_POST(self):
        # Only JSON from this page: a form or another site cannot send that without a preflight.
        if self.path != "/api" or not self.headers.get("Content-Type", "").startswith("application/json"):
            self.reply(403, "forbidden", "text/plain")
            return
        try:
            req = json.loads(self.rfile.read(int(self.headers.get("Content-Length", "0"))) or b"{}")
            msg = act(str(req.get("action", "")), req.get("arg"))
        except Exception as e:   # shown on the page
            msg = "Fehler: %s" % e
        self.reply(200, json.dumps({"msg": msg}), "application/json")


ap = argparse.ArgumentParser()
ap.add_argument("--port", type=int, default=8766)
args = ap.parse_args()
threading.Thread(target=reader, daemon=True).start()
print("Panel-Test: http://127.0.0.1:%d" % args.port, flush=True)
ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()
