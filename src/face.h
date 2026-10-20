// What the watchfaces share (concept doc, tab "Watchface animations").
//
// The four faces - classic, Tetris, Muncher, Aliens - are being moved behind
// one interface, step by step, with the panel kept as it was at every step.
// This header starts with the tools that prove it: random numbers a test build
// can repeat, and a checksum of a frame.
//
// It is built on every board, the M4 too, so it keeps to C++11.
#ifndef FACE_H
#define FACE_H

#include <Arduino.h>

// Random numbers for the faces. A release draws them from random(), which on
// the S3 reads the hardware generator (no randomSeed() anywhere: see
// tetrisStartDigit()). A test build (FACE_FRAME_LOG) draws them from a
// generator of its own, set anew from the minute at every minute's start, so
// two runs over the same minutes draw the same numbers and show the same
// frames - also when the code that draws them has moved.
#if defined(FACE_FRAME_LOG)
struct FaceRandom {
  static uint32_t state;
  static void seed(uint32_t s) { state = s ? s : 0x9E3779B9u; }
  static uint32_t next() {   // xorshift32
    uint32_t x = state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return state = x;
  }
};
uint32_t FaceRandom::state = 0x9E3779B9u;
inline long faceRandom(long howbig) { return howbig <= 0 ? 0 : (long)(FaceRandom::next() % (uint32_t)howbig); }
inline long faceRandom(long howsmall, long howbig) {
  return howsmall >= howbig ? howsmall : howsmall + faceRandom(howbig - howsmall);
}
#else
inline long faceRandom(long howbig) { return random(howbig); }
inline long faceRandom(long howsmall, long howbig) { return random(howsmall, howbig); }
#endif

// CRC-32 (the zip polynomial) with a table of 16, a nibble at a time: small
// enough for the M4, and about 0.15 ms for a 64x32 frame on the S3.
inline uint32_t faceCrc32(const uint8_t *p, size_t n) {
  static const uint32_t T[16] = {
    0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
    0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
  };
  uint32_t c = 0xFFFFFFFFu;
  while (n--) {
    c ^= *p++;
    c = (c >> 4) ^ T[c & 15];
    c = (c >> 4) ^ T[c & 15];
  }
  return ~c;
}

#endif  // FACE_H
