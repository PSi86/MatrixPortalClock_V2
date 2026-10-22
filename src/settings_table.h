/* -------------------------------------------------------------------------
   settings_table.h - a blob's settings as a table.

   Each blob of settings (NVS key, magic, revision, then its fields) has a
   table of its settings: the web app's key, where the value sits in the
   blob, what kind of value it is, its range, what it does when it changes,
   whether the web app tries it live, and what a stored value outside its
   range becomes. One piece of code then checks them all when they are
   loaded, takes them from the web app, live or to be saved, and reports
   them in /api/state, together with what the web app needs to send them
   back. The watchfaces' settings went through such tables first (face.h,
   2026-10-20); since 2026-10-22 the clock's own do too (code review of
   2026-10-14, point 8). The keys, the blobs and the values are the ones
   stored all along, and each check is the one its blob had.

   A setting that does more than its field - a place, the inputs, the sides
   the digits come from - is in the table as PARAM_SPECIAL, so the web app
   learns of it, and its blob's own code takes and reports it.
   ------------------------------------------------------------------------- */
#ifndef SETTINGS_TABLE_H
#define SETTINGS_TABLE_H

#include <Arduino.h>
#include <stddef.h>
#include <string.h>

enum ParamKind : uint8_t {
  PARAM_RANGE,      // a number from lo to hi
  PARAM_CHOICE,     // one of lo..hi, by value
  PARAM_FLAG,       // 0 or 1: the web app's anything-but-0 is 1
  PARAM_CHECKBOX,   // 0 or 1 from a checkbox, "on" when ticked: on a save an
                    // absent one is off, live only one that is sent counts
  PARAM_BIT,        // the same, kept as bit lo of its byte
  PARAM_TENTHS,     // a number with one decimal ("2.5" or "2,5"), kept in tenths
  PARAM_SCALED,     // ms in the web app, kept in tens (rounded); lo..hi in tens
  PARAM_COLOUR,     // "#rrggbb" in the web app, kept as three bytes
  PARAM_SPECIAL,    // taken and reported by its blob's own code
};

// What a stored value outside lo..hi becomes when its blob is checked.
enum ParamLoad : uint8_t {
  LOAD_KEEP,        // it stays: every value fits, or it was never checked
  LOAD_CLAMP,       // it is moved into the range
  LOAD_DEFAULT,     // it becomes the default
  LOAD_MASK,        // only the bits of hi stay (hi = 2^n - 1)
  LOAD_BOOL,        // anything but 0 becomes 1
};

const uint8_t PARAM_REDRAW = 0x01;   // a new value: the face redraws at once (a colour: whenever one is sent)
const uint8_t PARAM_PLAYS  = 0x02;   // a new value tried live: the face plays what it picks
// The knock's sensitivity: to the sensor when it changes. Until 2026-10-20
// whenever the web app sent it, which it does with every live change - the
// wall's QMI8658 was reset each time, blind to a knock meanwhile.
const uint8_t PARAM_KNOCK  = 0x04;
const uint8_t PARAM_LIVE   = 0x08;   // the web app tries it live (/api/live) before it is saved

struct SettingParam {
  const char *key;            // in the web app and in /api/state
  uint8_t offset, size;       // where in the blob: 1 or 2 bytes, little-endian (a colour: 3)
  ParamKind kind;
  uint8_t flags;              // PARAM_REDRAW, PARAM_PLAYS, PARAM_KNOCK, PARAM_LIVE
  uint16_t lo, hi;
  ParamLoad load;
  const char *const *labels;  // a choice's and a flag's names, by value; nullptr otherwise
};

// A blob's settings: the blob in RAM, its defaults (the same struct) and its table.
struct SettingsTable {
  uint8_t *data;
  const uint8_t *defaults;
  const SettingParam *params;
  uint8_t count;
};

// A field of a settings struct, for a table row: where it is and how big.
#define SETTING_FIELD(type, field) (uint8_t)offsetof(type, field), (uint8_t)sizeof(((type *)nullptr)->field)
#define SETTING_TABLE(data, defaults, params) \
  { (uint8_t *)&(data), (const uint8_t *)&(defaults), params, (uint8_t)(sizeof(params) / sizeof(params[0])) }

// ---- The web app's values ------------------------------------------------------

inline uint8_t hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}

// "#rrggbb" (or without the #) into r, g and b; anything shorter leaves them.
inline void parseHexColor(const String &s, uint8_t &r, uint8_t &g, uint8_t &b) {
  String t = s;
  if (t.startsWith("#")) { t = t.substring(1); }
  if (t.length() >= 6) {
    r = (hexVal(t[0]) << 4) | hexVal(t[1]);
    g = (hexVal(t[2]) << 4) | hexVal(t[3]);
    b = (hexVal(t[4]) << 4) | hexVal(t[5]);
  }
}

inline String toHex(uint8_t r, uint8_t g, uint8_t b) {
  char buf[8];
  sprintf(buf, "#%02x%02x%02x", r, g, b);
  return String(buf);
}

// A number such as 2, 2.5 or 2,5 in tenths, without the C library's float
// parser (it would cost 7 KB of flash for this one kind of field).
inline long tenthsOf(const String &v) {
  int dot = v.indexOf('.');
  if (dot < 0) { dot = v.indexOf(','); }
  long tenths = (dot < 0 ? v : v.substring(0, dot)).toInt() * 10;
  if (dot >= 0 && dot + 1 < (int)v.length() && isDigit(v.charAt(dot + 1))) { tenths += v.charAt(dot + 1) - '0'; }
  return tenths;
}

// ---- The table's walks -----------------------------------------------------------

inline uint16_t settingGet(const uint8_t *data, const SettingParam &p) {
  if (p.kind == PARAM_BIT) { return (data[p.offset] >> p.lo) & 1; }
  return p.size == 2 ? (uint16_t)(data[p.offset] | data[p.offset + 1] << 8) : data[p.offset];
}
inline void settingSet(uint8_t *data, const SettingParam &p, uint16_t v) {
  if (p.kind == PARAM_BIT) {
    data[p.offset] = (uint8_t)((data[p.offset] & ~(1 << p.lo)) | ((v & 1) << p.lo));
    return;
  }
  data[p.offset] = (uint8_t)v;
  if (p.size == 2) { data[p.offset + 1] = (uint8_t)(v >> 8); }
}
// Whether a row holds a number checked against lo..hi (a bit's lo is its place).
inline bool settingRanged(const SettingParam &p) {
  return p.kind != PARAM_BIT && p.kind != PARAM_COLOUR && p.kind != PARAM_SPECIAL;
}

// Every stored value checked, each as its row says.
inline void settingsCheck(SettingsTable &t) {
  for (uint8_t i = 0; i < t.count; i++) {
    const SettingParam &p = t.params[i];
    if (!settingRanged(p) || p.load == LOAD_KEEP) { continue; }
    uint16_t v = settingGet(t.data, p);
    switch (p.load) {
      case LOAD_CLAMP:   v = v < p.lo ? p.lo : v > p.hi ? p.hi : v; break;
      case LOAD_DEFAULT: if (v < p.lo || v > p.hi) { v = settingGet(t.defaults, p); } break;
      case LOAD_MASK:    v &= p.hi; break;
      case LOAD_BOOL:    v = v ? 1 : 0; break;
      default:           break;
    }
    settingSet(t.data, p, v);
  }
}

// The web app's values, to be saved (live false) or tried live (true): each
// key that `get(key)` finds is taken, kept within its range; on a save an
// unticked checkbox is off. Returns what came: PARAM_REDRAW, PARAM_PLAYS or
// PARAM_KNOCK of a setting whose value changed - and PARAM_REDRAW for every
// colour sent, changed or not, as the clock always did.
template <typename Get>
uint8_t settingsApply(SettingsTable &t, Get get, bool live) {
  uint8_t came = 0;
  for (uint8_t i = 0; i < t.count; i++) {
    const SettingParam &p = t.params[i];
    if (p.kind == PARAM_SPECIAL || (live && !(p.flags & PARAM_LIVE))) { continue; }
    String v = get(p.key);
    long x;
    switch (p.kind) {
      case PARAM_CHECKBOX:
      case PARAM_BIT:
        if (live && !v.length()) { continue; }
        x = v == "on" ? 1 : 0;
        break;
      case PARAM_COLOUR:
        if (!v.length()) { continue; }
        parseHexColor(v, t.data[p.offset], t.data[p.offset + 1], t.data[p.offset + 2]);
        came |= p.flags & PARAM_REDRAW;
        continue;
      default:
        if (!v.length()) { continue; }
        x = p.kind == PARAM_FLAG   ? (v.toInt() ? 1 : 0)
          : p.kind == PARAM_TENTHS ? constrain(tenthsOf(v), (long)p.lo, (long)p.hi)
          : p.kind == PARAM_SCALED ? constrain((v.toInt() + 5) / 10, (long)p.lo, (long)p.hi)
          :                          constrain(v.toInt(), (long)p.lo, (long)p.hi);
        break;
    }
    if ((uint16_t)x != settingGet(t.data, p)) { came |= p.flags & (PARAM_REDRAW | PARAM_PLAYS | PARAM_KNOCK); }
    settingSet(t.data, p, (uint16_t)x);
  }
  return came;
}

// The settings into /api/state's settings object `o`, by key, as the web app
// reads them: a checkbox as true or false, a scaled one in ms, a colour as
// "#rrggbb", tenths as tenths.
template <typename Obj>
void settingsJson(const SettingsTable &t, Obj o) {
  for (uint8_t i = 0; i < t.count; i++) {
    const SettingParam &p = t.params[i];
    const uint8_t *d = t.data;
    switch (p.kind) {
      case PARAM_SPECIAL:  break;
      case PARAM_CHECKBOX:
      case PARAM_BIT:      o[p.key] = settingGet(d, p) != 0; break;
      case PARAM_SCALED:   o[p.key] = settingGet(d, p) * 10; break;
      case PARAM_COLOUR:   o[p.key] = toHex(d[p.offset], d[p.offset + 1], d[p.offset + 2]); break;
      default:             o[p.key] = settingGet(d, p); break;
    }
  }
}

// The names of the choices, for /api/state's lists: `add(key, labels,
// count)` once per setting that has names.
template <typename Add>
void settingsLists(const SettingsTable &t, Add add) {
  for (uint8_t i = 0; i < t.count; i++) {
    const SettingParam &p = t.params[i];
    if (p.labels) { add(p.key, p.labels, (uint8_t)(p.hi - p.lo + 1)); }
  }
}

// What the web app needs to send each setting back: `add(key, kind, lo, hi,
// live)` once per row, the kind by name, lo and hi as stored (a bit's lo is
// its place, a colour's and a special one's are 0).
inline const char *settingKindName(ParamKind k) {
  switch (k) {
    case PARAM_RANGE:    return "range";
    case PARAM_CHOICE:   return "choice";
    case PARAM_FLAG:     return "flag";
    case PARAM_CHECKBOX: return "checkbox";
    case PARAM_BIT:      return "checkbox";
    case PARAM_TENTHS:   return "tenths";
    case PARAM_SCALED:   return "tens";
    case PARAM_COLOUR:   return "colour";
    default:             return "special";
  }
}
template <typename Add>
void settingsForm(const SettingsTable &t, Add add) {
  for (uint8_t i = 0; i < t.count; i++) {
    const SettingParam &p = t.params[i];
    add(p.key, settingKindName(p.kind), p.lo, p.hi, (p.flags & PARAM_LIVE) != 0);
  }
}

#endif  // SETTINGS_TABLE_H
