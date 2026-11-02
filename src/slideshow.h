#pragma once
/**
 * slideshow.h — screensaver photo store
 *
 * Images live in the LittleFS partition ("spiffs" subtype) at firmware/imgNN.jpg.
 * They are plain JPEGs, small enough to decode one at a time; the decoded frame
 * goes straight into a caller-supplied PSRAM buffer, so the photo set costs no
 * internal RAM and does not grow the OTA app image.
 *
 * The set is kept contiguous (0.jpg .. N-1.jpg) so the rendered order is the
 * upload order: removing a middle image renumbers the ones after it.
 */

#include <Arduino.h>
#include "config.h"

namespace Slideshow {

// Upload target size. Slightly larger than the 480x272 panel so the slow pan
// has room to drift without showing an edge.
static constexpr int IMG_W = 520;
static constexpr int IMG_H = 300;

static constexpr int    MAX_IMAGES       = 40;
static constexpr size_t MAX_UPLOAD_BYTES = 220 * 1024;

bool begin();               // mount the partition (formats once if blank)
bool mounted();
int  count();
size_t sizeAt(int idx);
size_t usedBytes();
size_t totalBytes();
size_t freeBytes();

// Decode image idx into dst, which must hold IMG_W*IMG_H RGB565 pixels.
// Returns false on a missing file or a JPEG the decoder rejects.
bool decode(int idx, uint16_t *dst, int *outW, int *outH);

// Streaming upload (driven from the HTTP multipart callbacks).
bool   uploadBegin();
bool   uploading();
size_t uploadWrite(const uint8_t *data, size_t len);
bool   uploadEnd();
void   uploadAbort();

bool removeAt(int idx);
void removeAll();

} // namespace Slideshow
