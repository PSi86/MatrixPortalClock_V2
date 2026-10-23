/* ----------------------------------------------------------------------
   gif_find.h - GIFs on request: which GIFs a name or a keyword means (plan
   of 2026-10-23). Plain C++ without Arduino, so the PC tests build it as it
   is.

   Every GIF has a search text: its name and the fields of the GIF meta index
   (README, "GIF meta index": title, franchise, category, era, style,
   motif), made once when the index is read (gifSearchText()): lower case,
   the Latin-1 letters folded to their base letters, every run of other
   characters one space, the fields apart by '|'. A request is made the same
   way, without the spaces (gifQueryParse()), and means:
   - the GIFs it is the name of, ".gif", case and separators aside;
   - else, for each part between commas, the GIFs where that part stands as
     whole words (a plural s or es with it): "star wars", "starwars" and
     "Star-Wars" alike. Only when no GIF has it so, the GIFs where it begins
     a word count: "pika" finds Pikachu, while "rain" finds rain but not
     rainbow, and "car" no cartoon.
   ---------------------------------------------------------------------- */
#pragma once

#include <stdint.h>
#include <string.h>

const uint8_t GIF_FIND_KEYS    = 4;    // keywords in one request, apart by commas
const uint8_t GIF_FIND_KEY_MAX = 64;   // bytes of one keyword or name as it is compared, with its NUL

struct GifQuery {
  char    name[GIF_FIND_KEY_MAX];                   // the whole request as a name
  char    key[GIF_FIND_KEYS][GIF_FIND_KEY_MAX];     // its parts between commas
  uint8_t keys;
};

// A GIF as gifFindAll() sees it: its search text, whether its name was cut
// short (the GIF pack keeps 31 characters), so a longer one asked for still
// names it, and the letters its words begin with (gifStartsOf(); all bits:
// not known), so a keyword passes over a GIF without a word that starts
// with its first letter.
struct GifFindItem { const char *text; bool cut; uint32_t starts; };

// The bit of a word's first letter or digit in GifFindItem.starts: a to z
// their own, the ten digits six bits among them.
inline uint32_t gifStartBit(char c) { return 1u << (c >= 'a' ? c - 'a' : 26 + (c - '0') % 6); }

// The first letters of a search text's words, as bits.
inline uint32_t gifStartsOf(const char *text) {
  uint32_t bits = 0;
  for (const char *s = text; *s; s++) {
    if (*s != ' ' && *s != '|' && (s == text || s[-1] == ' ' || s[-1] == '|')) { bits |= gifStartBit(*s); }
  }
  return bits;
}

// ---- The meta index -----------------------------------------------------------
// The GIF library's line per GIF (README, "GIF meta index"), between its
// first line and "END <count>": the fields apart by tabs.
const char *const GIF_META_HEAD = "MATRIXCLOCK GIF META 1";
enum GifMetaField : uint8_t {
  GIF_META_NAME, GIF_META_TITLE, GIF_META_FRANCHISE, GIF_META_CATEGORY, GIF_META_ERA, GIF_META_STYLE, GIF_META_MOTIF,
  GIF_META_FIELDS
};

// A GIF's name and fields, "" where it has none, and its search text.
struct GifMeta {
  const char *field[GIF_META_FIELDS];
  const char *text;
  uint32_t    starts;   // gifStartsOf(text)
  bool        cut;      // the name is the pack's, cut to 31 characters
};

// A meta index (len bytes at text, NUL-terminated) taken apart in place: its
// tabs and line ends become NULs, and line(field) gets each GIF's line as
// GIF_META_FIELDS pointers into text, "" for a field the line lacks. Returns
// the lines handed out; -1, with none handed out and text as it was, when it
// does not start with GIF_META_HEAD or does not end with "END <count>" for
// its lines (cut off, or written by something else).
template <typename Line>
inline long gifMetaLines(char *text, size_t len, Line line) {
  size_t head = strlen(GIF_META_HEAD);
  if (len <= head || memcmp(text, GIF_META_HEAD, head) != 0 || (text[head] != '\n' && text[head] != '\r')) { return -1; }
  char *end = text + len, *first = (char *)memchr(text, '\n', len);
  if (!first) { return -1; }
  first++;
  long lines = 0, told = -1;
  for (char *s = first; s < end; ) {
    char *e = (char *)memchr(s, '\n', (size_t)(end - s));
    if (!e) { break; }   // a last line without its end: cut off
    if (e - s > 4 && memcmp(s, "END ", 4) == 0) {
      told = 0;
      for (char *d = s + 4; d < e && *d >= '0' && *d <= '9'; d++) { told = told * 10 + (*d - '0'); }
      break;
    }
    if (e > s && !(e - s == 1 && *s == '\r')) { lines++; }
    s = e + 1;
  }
  if (told != lines) { return -1; }
  for (char *s = first; s < end; ) {
    char *e = (char *)memchr(s, '\n', (size_t)(end - s));
    if (e - s > 4 && memcmp(s, "END ", 4) == 0) { break; }
    *e = '\0';
    if (e > s && e[-1] == '\r') { e[-1] = '\0'; }
    if (*s) {
      const char *field[GIF_META_FIELDS];
      uint8_t n = 0;
      field[n++] = s;
      for (char *p = s; *p && n < GIF_META_FIELDS; p++) {
        if (*p == '\t') { *p = '\0'; field[n++] = p + 1; }
      }
      for (char *p = (char *)field[n - 1]; *p; p++) { if (*p == '\t') { *p = '\0'; break; } }   // fields past the last
      while (n < GIF_META_FIELDS) { field[n++] = ""; }
      line(field);
    }
    s = e + 1;
  }
  return lines;
}

// The search texts of count GIFs whose fields are set, into one block from
// alloc(bytes), each GIF's text pointing into it; the block, nullptr when
// alloc gave none (the texts are left as they were).
template <typename Alloc>
inline char *gifMetaTexts(GifMeta *meta, uint16_t count, Alloc alloc);

// The base letters of U+00C0 to U+00FF, "" for the two signs among them.
inline const char *const GIF_LATIN1[64] = {
  "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
  "d", "n", "o", "o", "o", "o", "o", "",  "o", "u", "u", "u", "u", "y", "th", "ss",
  "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
  "d", "n", "o", "o", "o", "o", "o", "",  "o", "u", "u", "u", "u", "y", "th", "y",
};

// The letters and digits of the character at p (UTF-8, up to end) in lower
// case, into out: a letter or digit of ASCII itself, a Latin-1 letter its
// base letters, anything else "" - a separator. Returns the bytes it took.
// (No buffer of its own: the card's task folds while the loop does.)
inline size_t gifFoldChar(const unsigned char *p, const unsigned char *end, char out[3]) {
  unsigned char c = *p;
  out[0] = out[1] = out[2] = '\0';
  if (c < 0x80) {
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) { out[0] = (char)c; }
    else if (c >= 'A' && c <= 'Z')                       { out[0] = (char)(c - 'A' + 'a'); }
    return 1;
  }
  size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;   // a stray continuation byte alone
  size_t n = 1;
  while (n < len && p + n < end && (p[n] & 0xC0) == 0x80) { n++; }
  if (c == 0xC3 && n == 2) { strcpy(out, GIF_LATIN1[p[1] - 0x80]); }
  return n;
}

// in (len bytes of UTF-8) folded into out (size bytes, always NUL-terminated):
// its letters and digits, with spaces one space between its words, else
// none. Returns the length the whole would take, so out == nullptr measures.
inline size_t gifNormalise(const char *in, size_t len, char *out, size_t size, bool spaces) {
  const unsigned char *p = (const unsigned char *)in, *end = p + len;
  size_t n = 0;
  bool gap = false;   // a separator since the last letter
  while (p < end) {
    char folded[3];
    p += gifFoldChar(p, end, folded);
    const char *f = folded;
    if (!*f) { gap = true; continue; }
    if (gap && spaces && n > 0) {
      if (out && n + 1 < size) { out[n] = ' '; }
      n++;
    }
    gap = false;
    for (; *f; f++, n++) { if (out && n + 1 < size) { out[n] = *f; } }
  }
  if (out && size) { out[n < size ? n : size - 1] = '\0'; }
  return n;
}

// The length of a name without a ".gif" at its end, of any case.
inline size_t gifStemLength(const char *name, size_t len) {
  if (len >= 4) {
    const char *x = name + len - 4;
    if (x[0] == '.' && (x[1] | 0x20) == 'g' && (x[2] | 0x20) == 'i' && (x[3] | 0x20) == 'f') { return len - 4; }
  }
  return len;
}

// The search text of a GIF out of its fields (UTF-8, nullptr as ""), the
// first its name, without its ".gif" (a request names it either way), into
// out (size bytes); the length it takes, so out == nullptr measures.
inline size_t gifSearchText(const char *const *fields, uint8_t count, char *out, size_t size) {
  size_t n = 0;
  for (uint8_t i = 0; i < count; i++) {
    if (i > 0) {
      if (out && n + 1 < size) { out[n] = '|'; }
      n++;
    }
    const char *f = fields[i] ? fields[i] : "";
    size_t len = strlen(f);
    if (i == 0) { len = gifStemLength(f, len); }
    size_t room = (out && n < size) ? size - n : 0;
    n += gifNormalise(f, len, room ? out + n : nullptr, room, true);
  }
  if (out && size) { out[n < size ? n : size - 1] = '\0'; }
  return n;
}

// The request taken apart: the whole as a name, without a ".gif" at its end,
// and its parts between commas as keywords (the first GIF_FIND_KEYS that are
// not empty). Both empty: a request for any GIF.
inline void gifQueryParse(const char *request, GifQuery &q) {
  size_t len = strlen(request);
  while (len > 0 && (request[len - 1] == ' ' || request[len - 1] == '\t')) { len--; }
  gifNormalise(request, gifStemLength(request, len), q.name, sizeof(q.name), false);
  q.keys = 0;
  const char *s = request, *end = request + len;
  while (s <= end) {
    const char *comma = (const char *)memchr(s, ',', (size_t)(end - s));
    const char *e = comma ? comma : end;
    if (q.keys < GIF_FIND_KEYS) {
      gifNormalise(s, (size_t)(e - s), q.key[q.keys], sizeof(q.key[0]), false);
      if (q.key[q.keys][0]) { q.keys++; }
    }
    if (!comma) { break; }
    s = comma + 1;
  }
}

// How key (letters and digits only) stands in a search text: 2 as whole
// words, 1 at the start of a word only, 0 not at all. The spaces of the
// text count for nothing within it, the fields' '|' end it. It goes from
// word start to word start, and tries one only where its first letter is
// the key's.
inline uint8_t gifMatchLevel(const char *text, const char *key) {
  if (!*key) { return 0; }
  uint8_t best = 0;
  const char *s = text;
  while (*s == ' ' || *s == '|') { s++; }
  while (*s) {
    if (*s == *key) {   // a word's start
      const char *p = s, *k = key;
      while (*k && *p && *p != '|') {
        if (*p == ' ') { p++; continue; }
        if (*p != *k) { break; }
        p++;
        k++;
      }
      if (!*k) {
        const char *r = p;   // what is left of the word the key ends in
        while (*r && *r != ' ' && *r != '|') { r++; }
        size_t rest = (size_t)(r - p);
        if (rest == 0 || (rest == 1 && p[0] == 's') || (rest == 2 && p[0] == 'e' && p[1] == 's')) { return 2; }
        best = 1;
      }
    }
    while (*s && *s != ' ' && *s != '|') { s++; }   // on to the next word
    while (*s == ' ' || *s == '|') { s++; }
  }
  return best;
}

// Whether a search text's first field, the GIF's name, is name (letters and
// digits only); a name cut short also when name goes on past it.
inline bool gifNameIs(const char *text, const char *name, bool cut) {
  const char *k = name;
  for (const char *p = text; *p && *p != '|'; p++) {
    if (*p == ' ') { continue; }
    if (*p != *k) { return false; }
    k++;
  }
  return k != name && (*k == '\0' || cut);
}

// Marks in hit (count bytes, 1 or 0) the GIFs the request means; item(i)
// gives GIF i as a GifFindItem. Returns how many; byName says whether the
// request was the name of them. A request for any GIF marks none.
template <typename Item>
inline uint16_t gifFindAll(const GifQuery &q, uint16_t count, Item item, uint8_t *hit, bool &byName) {
  uint16_t found = 0;
  byName = false;
  if (q.name[0]) {
    for (uint16_t i = 0; i < count; i++) {
      GifFindItem g = item(i);
      hit[i] = gifNameIs(g.text, q.name, g.cut) ? 1 : 0;
      found += hit[i];
    }
    if (found) { byName = true; return found; }
  }
  // Each keyword's levels in one pass: bit k of the low half whole words,
  // of the high half the start of one; then each keyword counts with the
  // GIFs that have it whole, or, when none has, with those it starts in.
  uint8_t whole = 0;
  uint32_t first[GIF_FIND_KEYS];
  for (uint8_t k = 0; k < q.keys; k++) { first[k] = gifStartBit(q.key[k][0]); }
  for (uint16_t i = 0; i < count; i++) {
    GifFindItem g = item(i);
    uint8_t m = 0;
    for (uint8_t k = 0; k < q.keys; k++) {
      if (!(g.starts & first[k])) { continue; }   // no word of it starts as the key does
      uint8_t level = gifMatchLevel(g.text, q.key[k]);
      if (level == 2) { m |= (uint8_t)(1 << k); } else if (level == 1) { m |= (uint8_t)(0x10 << k); }
    }
    hit[i] = m;
    whole |= m & 0x0F;
  }
  for (uint16_t i = 0; i < count; i++) {
    hit[i] = ((hit[i] & whole) | ((hit[i] >> 4) & (uint8_t)~whole & 0x0F)) ? 1 : 0;
    found += hit[i];
  }
  return found;
}

template <typename Alloc>
inline char *gifMetaTexts(GifMeta *meta, uint16_t count, Alloc alloc) {
  size_t total = 0;
  for (uint16_t i = 0; i < count; i++) { total += gifSearchText(meta[i].field, GIF_META_FIELDS, nullptr, 0) + 1; }
  char *block = (char *)alloc(total ? total : 1);
  if (!block) { return nullptr; }
  size_t at = 0;
  for (uint16_t i = 0; i < count; i++) {
    meta[i].text = block + at;
    at += gifSearchText(meta[i].field, GIF_META_FIELDS, block + at, total - at) + 1;
    meta[i].starts = gifStartsOf(meta[i].text);
  }
  return block;
}
