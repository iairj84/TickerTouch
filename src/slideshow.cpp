#include "slideshow.h"
#include <LittleFS.h>
#include <JPEGDEC.h>

namespace Slideshow {

static bool   _mounted = false;
static int    _count   = 0;
static File   _upFile;
static int    _upSlot  = -1;
static size_t _upBytes = 0;

static const char DIR[] = "/img";

static void pathFor(int idx, char *out, size_t n) {
  snprintf(out, n, "%s/%d.jpg", DIR, idx);
}

// The list is contiguous by construction, so the first gap is the end.
static void scan() {
  _count = 0;
  char p[24];
  for (int i = 0; i < MAX_IMAGES; i++) {
    pathFor(i, p, sizeof(p));
    if (!LittleFS.exists(p)) break;
    _count++;
  }
}

bool begin() {
  if (_mounted) return true;
  // true = format on mount failure. The partition ships unformatted, and
  // formatting it cannot touch NVS, so saved settings survive.
  _mounted = LittleFS.begin(true);
  if (!_mounted) {
    Serial.println(F("[Slideshow] LittleFS mount failed"));
    return false;
  }
  if (!LittleFS.exists(DIR)) LittleFS.mkdir(DIR);
  scan();
  Serial.printf("[Slideshow] ready: %d image(s), %u/%u bytes used\n",
                _count, (unsigned)usedBytes(), (unsigned)totalBytes());
  return true;
}

bool mounted() { return _mounted; }
int  count()   { return _mounted ? _count : 0; }

size_t usedBytes()  { return _mounted ? LittleFS.usedBytes()  : 0; }
size_t totalBytes() { return _mounted ? LittleFS.totalBytes() : 0; }
size_t freeBytes()  { return totalBytes() - usedBytes(); }

size_t sizeAt(int idx) {
  if (!_mounted || idx < 0 || idx >= _count) return 0;
  char p[24];
  pathFor(idx, p, sizeof(p));
  File f = LittleFS.open(p, "r");
  if (!f) return 0;
  size_t s = f.size();
  f.close();
  return s;
}

// ── Decoding ─────────────────────────────────────────────────────────────────
static uint16_t *_dst  = nullptr;
static int       _dstW = 0;
static int       _dstH = 0;

// JPEGDEC hands back one MCU block at a time; copy it into our frame buffer and
// clip to the edges so an unexpectedly large JPEG cannot write past the buffer.
static int drawCb(JPEGDRAW *d) {
  const int cw = d->iWidth, ch = d->iHeight;
  for (int y = 0; y < ch; y++) {
    const int dy = d->y + y;
    if (dy < 0 || dy >= _dstH) continue;
    for (int x = 0; x < cw; x++) {
      const int dx = d->x + x;
      if (dx < 0 || dx >= _dstW) continue;
      _dst[dy * _dstW + dx] = d->pPixels[y * cw + x];
    }
  }
  return 1;
}

bool decode(int idx, uint16_t *dst, int *outW, int *outH) {
  if (!_mounted && !begin()) return false;
  if (idx < 0 || idx >= _count) return false;

  char p[24];
  pathFor(idx, p, sizeof(p));
  File f = LittleFS.open(p, "r");
  if (!f) {
    Serial.printf("[Slideshow] image %d missing\n", idx);
    return false;
  }

  // JPEGDEC holds roughly 15KB of scratch buffers. As a local it would overflow
  // the LVGL task stack, so keep one instance on the heap and reuse it.
  static JPEGDEC *jpg = nullptr;
  if (!jpg) {
    jpg = new JPEGDEC();
    if (!jpg) {
      Serial.println(F("[Slideshow] decoder allocation failed"));
      f.close();
      return false;
    }
  }
  _dst = dst; _dstW = IMG_W; _dstH = IMG_H;

  if (!jpg->open(f, drawCb)) {
    Serial.printf("[Slideshow] image %d: decoder rejected the file\n", idx);
    f.close();
    return false;
  }

  const int srcW = jpg->getWidth(), srcH = jpg->getHeight();
  // Uploads are already IMG_W x IMG_H; only a hand-placed larger file needs
  // hardware scaling.
  int opts = 0;
  if (srcW > IMG_W || srcH > IMG_H) {
    if (srcW / 2 <= IMG_W && srcH / 2 <= IMG_H)        opts = JPEG_SCALE_HALF;
    else if (srcW / 4 <= IMG_W && srcH / 4 <= IMG_H)   opts = JPEG_SCALE_QUARTER;
    else                                               opts = JPEG_SCALE_EIGHTH;
  }
  jpg->setPixelType(RGB565_LITTLE_ENDIAN);   // matches LV_COLOR_16_SWAP 0
  const bool ok = jpg->decode(0, 0, opts) != 0;
  if (!ok) Serial.printf("[Slideshow] image %d: decode failed (%dx%d)\n", idx, srcW, srcH);
  jpg->close();
  f.close();

  if (outW) *outW = srcW / (opts ? opts : 1);
  if (outH) *outH = srcH / (opts ? opts : 1);
  return ok;
}

// ── Upload ───────────────────────────────────────────────────────────────────
bool uploading() { return (bool)_upFile; }

bool uploadBegin() {
  if (!_mounted && !begin()) return false;
  if (_upSlot >= 0) uploadAbort();          // a previous upload never finished
  if (_count >= MAX_IMAGES) {
    Serial.println(F("[Slideshow] image limit reached"));
    return false;
  }
  if (freeBytes() < 48 * 1024) {
    Serial.println(F("[Slideshow] partition full"));
    return false;
  }
  char p[24];
  pathFor(_count, p, sizeof(p));
  _upFile = LittleFS.open(p, "w");
  if (!_upFile) {
    Serial.println(F("[Slideshow] cannot open slot for writing"));
    return false;
  }
  _upSlot  = _count;
  _upBytes = 0;
  return true;
}

size_t uploadWrite(const uint8_t *data, size_t len) {
  if (!_upFile) return 0;
  if (_upBytes + len > MAX_UPLOAD_BYTES) return 0;   // refuse oversized bodies
  const size_t w = _upFile.write(data, len);
  _upBytes += w;
  return w;
}

bool uploadEnd() {
  if (!_upFile) return false;
  _upFile.close();

  const bool ok = _upBytes > 1024;                  // anything smaller is not a JPEG
  if (ok) {
    _upSlot = -1;
    _upBytes = 0;
    scan();
    Serial.printf("[Slideshow] stored image %d (%d total)\n", _count - 1, _count);
  } else {
    uploadAbort();
  }
  return ok;
}

void uploadAbort() {
  if (_upFile) _upFile.close();
  if (_upSlot >= 0) {
    char p[24];
    pathFor(_upSlot, p, sizeof(p));
    LittleFS.remove(p);
  }
  _upSlot  = -1;
  _upBytes = 0;
  scan();
}

// ── Removal ──────────────────────────────────────────────────────────────────
bool removeAt(int idx) {
  if (!_mounted || idx < 0 || idx >= _count) return false;
  char p[24];
  pathFor(idx, p, sizeof(p));
  if (!LittleFS.remove(p)) return false;

  // Shift the tail down one slot so the set stays contiguous.
  for (int i = idx + 1; i < _count; i++) {
    char a[24], b[24];
    pathFor(i, a, sizeof(a));
    pathFor(i - 1, b, sizeof(b));
    LittleFS.rename(a, b);
  }
  _count--;
  return true;
}

void removeAll() {
  if (!_mounted && !begin()) return;
  for (int i = 0; i < _count; i++) {
    char p[24];
    pathFor(i, p, sizeof(p));
    LittleFS.remove(p);
  }
  _count = 0;
}

} // namespace Slideshow
