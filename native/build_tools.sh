#!/usr/bin/env bash
# Maintainer/CI only. Testers receive the compiled output, never run this script.
set -euo pipefail
OUT="$(mkdir -p "$1" && cd "$1" && pwd)"
CACHE="$(mkdir -p "$2" && cd "$2" && pwd)"
JOBS="${NATIVE_JOBS:-3}"
fetch() {
  local url="$1" archive="$2" digest="$3"
  if [ ! -f "$archive" ]; then curl --fail --location --retry 3 "$url" -o "$archive"; fi
  printf '%s  %s\n' "$digest" "$archive" | shasum -a 256 -c -
}
fetch https://ffmpeg.org/releases/ffmpeg-8.1.2.tar.xz "$CACHE/ffmpeg.tar.xz" 464beb5e7bf0c311e68b45ae2f04e9cc2af88851abb4082231742a74d97b524c
fetch https://downloads.sourceforge.net/project/lame/lame/4.0/lame-4.0.tar.gz "$CACHE/lame.tar.gz" 3df5124d5ad3a98312ffd7ba6a9b36230e4f8a3e66d3ce0f425e336c32d216eb
if [ ! -d "$CACHE/ffmpeg-8.1.2" ]; then tar -xf "$CACHE/ffmpeg.tar.xz" -C "$CACHE"; fi
if [ ! -d "$CACHE/lame-4.0" ]; then tar -xf "$CACHE/lame.tar.gz" -C "$CACHE"; fi
SUFFIX=""; EXTRA=(--enable-pic); LDFLAGS_EXTRA=""
case "$(uname -s)" in MINGW*|MSYS*) SUFFIX=.exe; EXTRA=(--target-os=mingw32 --arch=x86_64); LDFLAGS_EXTRA="-static -static-libgcc" ;; esac
(
 cd "$CACHE/ffmpeg-8.1.2"
 ./configure --disable-doc --disable-debug --disable-shared --enable-static --disable-network --disable-autodetect \
  --disable-ffplay --disable-ffprobe --disable-x86asm --disable-encoders --enable-encoder=rawvideo,pcm_s16le \
  --disable-hwaccels --disable-devices --extra-ldflags="$LDFLAGS_EXTRA" "${EXTRA[@]}"
 make -j "$JOBS" ffmpeg
 cp "ffmpeg$SUFFIX" "$OUT/"
)
(
 cd "$CACHE/lame-4.0"
 ./configure --disable-shared --enable-static --disable-decoder LDFLAGS="$LDFLAGS_EXTRA"
 make -j "$JOBS"
 cp "frontend/lame$SUFFIX" "$OUT/"
)
if [ ! -d "$CACHE/jpegli/.git" ]; then git clone --recursive https://github.com/google/jpegli.git "$CACHE/jpegli"; fi
(
 cd "$CACHE/jpegli"
 git checkout 031a0077f5799a6041004267fc12b956c1f52a20
 git submodule update --init --recursive
 cmake -S . -B native-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DJPEGLI_STATIC=ON \
  -DJPEGLI_ENABLE_TOOLS=ON -DJPEGLI_ENABLE_BENCHMARK=OFF -DJPEGLI_ENABLE_TESTS=OFF -DJPEGLI_ENABLE_DOXYGEN=OFF \
  -DJPEGLI_ENABLE_MANPAGES=OFF -DJPEGLI_ENABLE_JNI=OFF -DJPEGLI_ENABLE_OPENEXR=OFF -DJPEGLI_BUNDLE_LIBPNG=ON \
  -DCMAKE_EXE_LINKER_FLAGS="$LDFLAGS_EXTRA" -DCMAKE_POLICY_VERSION_MINIMUM=3.5
 cmake --build native-build --target cjpegli -j "$JOBS"
 cp "native-build/tools/cjpegli$SUFFIX" "$OUT/"
)
# Retain complete corresponding sources, pinned archive hashes and build recipe.
mkdir -p "$OUT/sources" "$OUT/licenses"
cp "$CACHE/ffmpeg.tar.xz" "$OUT/sources/ffmpeg-8.1.2.tar.xz"
cp "$CACHE/lame.tar.gz" "$OUT/sources/lame-4.0.tar.gz"
tar --exclude=.git --exclude=native-build -czf "$OUT/sources/jpegli-031a0077.tar.gz" -C "$CACHE" jpegli
cp "$CACHE/ffmpeg-8.1.2/COPYING.LGPLv2.1" "$OUT/licenses/FFmpeg-LGPL-2.1.txt"
cp "$CACHE/lame-4.0/COPYING" "$OUT/licenses/LAME.txt"
cp "$CACHE/jpegli/LICENSE" "$OUT/licenses/jpegli.txt"
cp "$0" "$OUT/sources/build_tools.sh"
