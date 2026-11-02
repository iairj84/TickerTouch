// Replicates Slideshow::drawCb exactly (clipping included) so the decode path
// can be exercised on a host, where a real board is not available.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "JPEGDEC.h"
static uint16_t *dst; static int dstW = 520, dstH = 300;
static int drawCb(JPEGDRAW *d) {
  const int cw = d->iWidth, ch = d->iHeight;
  for (int y = 0; y < ch; y++) {
    const int dy = d->y + y;
    if (dy < 0 || dy >= dstH) continue;
    for (int x = 0; x < cw; x++) {
      const int dx = d->x + x;
      if (dx < 0 || dx >= dstW) continue;
      dst[dy * dstW + dx] = d->pPixels[y * cw + x];
    }
  }
  return 1;
}
int main(int argc, char **argv) {
  FILE *f = fopen(argv[1], "rb");
  if (!f) { printf("cannot open %s\n", argv[1]); return 1; }
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  uint8_t *buf = (uint8_t*)malloc(n);
  if (fread(buf, 1, n, f) != (size_t)n) { printf("short read\n"); return 1; }
  fclose(f);
  dst = (uint16_t*)calloc((size_t)dstW * dstH, 2);
  printf("sizeof(JPEGDEC) = %zu bytes\n", sizeof(JPEGDEC));
  JPEGDEC jpg;
  if (!jpg.openRAM(buf, (int)n, drawCb)) { printf("openRAM FAILED\n"); return 1; }
  int w = jpg.getWidth(), h = jpg.getHeight();
  jpg.setPixelType(RGB565_LITTLE_ENDIAN);
  int ok = jpg.decode(0, 0, 0);
  jpg.close();
  printf("decode=%d src=%dx%d (scale 0)\n", ok, w, h);
  long nonblack = 0; uint32_t sum = 0;
  for (int i = 0; i < dstW * dstH; i++) { if (dst[i]) nonblack++; sum += dst[i]; }
  printf("nonblack=%ld/%d mean565=%u\n", nonblack, dstW * dstH, (unsigned)(sum / (dstW * dstH)));
  FILE *o = fopen(argv[2], "wb"); fwrite(dst, 2, (size_t)dstW * dstH, o); fclose(o);
  return ok ? 0 : 2;
}
