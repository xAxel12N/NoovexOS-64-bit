#!/bin/sh
OUT=$1; R=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$OUT/root/usr/bin" "$OUT/root/usr/share/noovex/faceid"
H=$(cat "$R/tools/faceid/nvx-faceid.cpp" "$0" | sha256sum | cut -c1-16)
if [ ! -x "$OUT/nvx-faceid" ] || [ "$(cat "$OUT/hash" 2>/dev/null)" != "$H" ]; then
  [ -f "$OUT/source.o" ] && [ "$(cat "$OUT/objflags" 2>/dev/null)" = pie ] || g++ -c -O2 -fPIE -msse4.2 -std=c++17 -I"$R/third_party/dlib" -DDLIB_NO_GUI_SUPPORT -DNDEBUG -DDLIB_DISABLE_ASSERTS "$R/third_party/dlib/dlib/all/source.cpp" -o "$OUT/source.o" && echo pie > "$OUT/objflags" || exit 1
  g++ -O2 -fPIE -msse4.2 -std=c++17 -I"$R/third_party/dlib" -DDLIB_NO_GUI_SUPPORT -DNDEBUG -DDLIB_DISABLE_ASSERTS "$R/tools/faceid/nvx-faceid.cpp" "$OUT/source.o" -fPIE -static-pie -pthread -o "$OUT/nvx-faceid" || exit 1
  strip "$OUT/nvx-faceid"; echo "$H" > "$OUT/hash"
fi
cp "$OUT/nvx-faceid" "$OUT/root/usr/bin/nvx-faceid"
for m in shape_predictor_5_face_landmarks dlib_face_recognition_resnet_model_v1; do
  [ -f "$OUT/$m.dat" ] || bzip2 -dc "$R/third_party/dlib-models/$m.dat.bz2" > "$OUT/$m.dat" || exit 1
  cp "$OUT/$m.dat" "$OUT/root/usr/share/noovex/faceid/"
done
cp "$R/third_party/dlib-models/LICENSE" "$OUT/root/usr/share/noovex/faceid/LICENSE-models"
cp "$R/third_party/dlib/LICENSE.txt" "$OUT/root/usr/share/noovex/faceid/LICENSE-dlib"
echo "  face engine: nvx-faceid $(stat -c %s "$OUT/nvx-faceid") bytes, models $(du -sh "$OUT/root/usr/share/noovex/faceid" | cut -f1)"
