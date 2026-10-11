/*
 * SM50 emulator: minimal dependency-free PNG writer (RGB8, zlib "stored"
 * blocks, i.e. no compression).  Original code for the SM50 project.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sm50.h"

static u32 crc_table[256];
static int crc_ready;

u32 sm50_crc32(u32 crc, const void *data, size_t len) {
  if (!crc_ready) {
    for (u32 n = 0; n < 256; n++) {
      u32 c = n;
      for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
      crc_table[n] = c;
    }
    crc_ready = 1;
  }
  const u8 *p = data;
  crc ^= 0xffffffffu;
  for (size_t i = 0; i < len; i++) crc = crc_table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
  return crc ^ 0xffffffffu;
}

static void put32(u8 *p, u32 v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static void chunk(FILE *f, const char *type, const u8 *data, u32 len) {
  u8 hdr[8];
  put32(hdr, len);
  memcpy(hdr + 4, type, 4);
  fwrite(hdr, 1, 8, f);
  if (len) fwrite(data, 1, len, f);
  u32 c = sm50_crc32(0, type, 4);
  /* continue crc over data: crc32 is chainable */
  c = sm50_crc32(c, data, len);
  u8 t[4];
  put32(t, c);
  fwrite(t, 1, 4, f);
}

int sm50_write_png(const char *path, const u16 *px, int w, int h) {
  size_t rowlen = (size_t)w * 3 + 1;
  size_t rawlen = rowlen * (size_t)h;
  u8 *raw = malloc(rawlen);
  for (int y = 0; y < h; y++) {
    u8 *r = raw + rowlen * y;
    r[0] = 0; /* filter: none */
    for (int x = 0; x < w; x++) {
      u16 c = px[y * w + x];
      u8 R = (c >> 11) & 31, G = (c >> 5) & 63, B = c & 31;
      r[1 + 3 * x] = (u8)((R << 3) | (R >> 2));
      r[2 + 3 * x] = (u8)((G << 2) | (G >> 4));
      r[3 + 3 * x] = (u8)((B << 3) | (B >> 2));
    }
  }
  size_t nblocks = (rawlen + 65534) / 65535;
  size_t zlen = 2 + rawlen + nblocks * 5 + 4;
  u8 *z = malloc(zlen), *q = z;
  *q++ = 0x78; *q++ = 0x01;
  for (size_t off = 0; off < rawlen;) {
    size_t n = rawlen - off > 65535 ? 65535 : rawlen - off;
    *q++ = (off + n == rawlen) ? 1 : 0;
    *q++ = n & 0xff; *q++ = n >> 8;
    *q++ = ~n & 0xff; *q++ = (~n >> 8) & 0xff;
    memcpy(q, raw + off, n);
    q += n;
    off += n;
  }
  u32 a = 1, b = 0;
  for (size_t i = 0; i < rawlen; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
  put32(q, (b << 16) | a);
  q += 4;

  FILE *f = fopen(path, "wb");
  if (!f) { free(raw); free(z); return -1; }
  static const u8 sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
  fwrite(sig, 1, 8, f);
  u8 ihdr[13];
  put32(ihdr, (u32)w);
  put32(ihdr + 4, (u32)h);
  ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  chunk(f, "IHDR", ihdr, 13);
  chunk(f, "IDAT", z, (u32)(q - z));
  chunk(f, "IEND", NULL, 0);
  fclose(f);
  free(raw);
  free(z);
  return 0;
}
