#!/usr/bin/env bash
# Host-side check of the screensaver image path: decodes a real JPEG through the
# same clipping callback slideshow.cpp uses and compares it to Pillow's decode.
# Usage: tools/decode_test/run.sh [path-to-JPEGDEC/src]
set -e
cd "$(dirname "$0")"
LIB="${1:-$HOME/Arduino/libraries/JPEGDEC/src}"
mkdir -p .build && cd .build
python3 - <<'PY'
from PIL import Image, ImageDraw
im = Image.new("RGB", (520, 300)); d = ImageDraw.Draw(im)
for x in range(520): d.line([(x,0),(x,300)], fill=(x*255//519, 60, 200-x*150//519))
d.rectangle([20,20,180,120], fill=(255,240,0)); d.ellipse([400,150,500,280], fill=(255,255,255))
im.save("fixture.jpg", "JPEG", quality=82)
PY
g++ -O2 -std=c++17 -I../shim -I"$LIB" ../decode_test.cpp "$LIB"/JPEGDEC.cpp -o decode_test
./decode_test fixture.jpg out.raw
python3 - <<'PY'
import struct
from PIL import Image
W, H = 520, 300
px = struct.unpack("<%dH" % (W*H), open("out.raw","rb").read())
ref = Image.open("fixture.jpg").convert("RGB").load()
tot = bad = nonblack = blackbug = 0
for y in range(H):
    for x in range(W):
        v = px[y*W+x]
        if v: nonblack += 1
        r, g, b = (v>>11)&0x1F, (v>>5)&0x3F, v&0x1F
        rr, gg, bb = ref[x, y]
        d = (abs(r*255//31-rr) + abs(g*255//63-gg) + abs(b*255//31-bb))//3
        tot += d
        if d > 24: bad += 1
        if (rr|gg|bb) and v == 0: blackbug += 1
mean = tot/(W*H)
print(f"nonblack {100.0*nonblack/(W*H):.2f}%  mean diff {mean:.2f}/255  >24: {100.0*bad/(W*H):.2f}%")
ok = nonblack > W*H*0.99 and mean < 6 and blackbug == 0
print("PASS" if ok else "FAIL")
raise SystemExit(0 if ok else 1)
PY
