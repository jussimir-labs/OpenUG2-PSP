#!/usr/bin/env bash
# Maintainer/CI only. Testers receive the compiled output, never run this script.
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
OUT="$(mkdir -p "$1" && cd "$1" && pwd)"
CACHE="$(mkdir -p "$2" && cd "$2" && pwd)"
JOBS="${NATIVE_JOBS:-3}"
fetch() {
  local url="$1" archive="$2" digest="$3"
  if [ ! -f "$archive" ]; then curl --fail --location --retry 3 "$url" -o "$archive"; fi
  if command -v shasum >/dev/null; then check=(shasum -a 256 -c -); else check=(sha256sum -c -); fi
  printf '%s  %s\n' "$digest" "$archive" | "${check[@]}"
}
fetch https://ffmpeg.org/releases/ffmpeg-8.1.2.tar.xz "$CACHE/ffmpeg.tar.xz" 464beb5e7bf0c311e68b45ae2f04e9cc2af88851abb4082231742a74d97b524c
fetch https://downloads.sourceforge.net/project/lame/lame/4.0/lame-4.0.tar.gz "$CACHE/lame.tar.gz" 3df5124d5ad3a98312ffd7ba6a9b36230e4f8a3e66d3ce0f425e336c32d216eb
if [ ! -d "$CACHE/ffmpeg-8.1.2" ]; then tar -xf "$CACHE/ffmpeg.tar.xz" -C "$CACHE"; fi
if [ ! -d "$CACHE/lame-4.0" ]; then tar -xf "$CACHE/lame.tar.gz" -C "$CACHE"; fi
SUFFIX=""; EXTRA=(--enable-pic); LDFLAGS_EXTRA=""; CXX_LDFLAGS=""; LAME_ENV=""
case "$(uname -s)" in
 MINGW*|MSYS*) SUFFIX=.exe; EXTRA=(--target-os=mingw32 --arch=x86_64); LDFLAGS_EXTRA="-static -static-libgcc"; CXX_LDFLAGS="$LDFLAGS_EXTRA" ;;
 # Only glibc stays dynamic: jpegli takes the C++/GCC runtimes inside, since
 # distributions ship different libstdc++ versions.
 # LAME's frontend would take the build machine's ncurses for its progress
 # display; distributions differ, and it changes nothing in the MP3.
 Linux) CXX_LDFLAGS="-static-libgcc -static-libstdc++"
  LAME_ENV="ac_cv_lib_termcap_initscr=no ac_cv_lib_curses_initscr=no ac_cv_lib_ncurses_initscr=no" ;;
esac
(
 cd "$CACHE/ffmpeg-8.1.2"
 ./configure --disable-doc --disable-debug --disable-shared --enable-static --disable-network --disable-autodetect \
  --disable-everything --enable-ffmpeg --disable-ffplay --disable-ffprobe --disable-x86asm \
  --enable-protocol=file,pipe,fd --enable-demuxer=ea,ea_cdata,mpeg,mpegvideo,pcm_s16le,mp3 \
  --enable-parser=mpegvideo,mpegaudio \
  --enable-decoder=vp6,vp6f,mpeg2video,adpcm_ea,adpcm_ea_r1,adpcm_ea_r2,adpcm_ea_r3,adpcm_ea_xas,pcm_s16le,mp3float \
  --enable-encoder=rawvideo,pcm_s16le --enable-muxer=rawvideo,pcm_s16le \
  --enable-filter=scale,pad,fps,aresample,aformat,format,null,anull,abuffer,buffer,abuffersink,buffersink \
  --disable-hwaccels --disable-devices --extra-ldflags="$LDFLAGS_EXTRA" "${EXTRA[@]}"
 make -j "$JOBS" "ffmpeg$SUFFIX"
 cp "ffmpeg$SUFFIX" "$OUT/"
)
if [ "${NATIVE_FFMPEG_ONLY:-0}" = 1 ]; then exit 0; fi
(
 cd "$CACHE/lame-4.0"
 # This dispatch accepts either UTF-8 bytes or UTF-16 words, selected by enc.
 # A generic pointer fixes GCC 14+'s error without changing audio encoding.
 if ! grep -qF 'int type, void const* str)' frontend/parse.c; then
  patch -p1 < "$SCRIPT_DIR/lame-4.0-portable.patch"
 fi
 # The RC21 reference MP3s come from Apple Silicon, where clang fuses a*b+c
 # (FMA). Clang emits the same fusible operations for every target, so x86-64
 # builds with FMA enabled produce identical MP3 bytes. GCC contracts other
 # expressions; build LAME with clang everywhere.
 lame_build() {
  make distclean >/dev/null 2>&1 || true
  env $LAME_ENV ac_cv_prog_cc_c23=no CC=clang ./configure --disable-shared --enable-static --disable-decoder \
   CFLAGS="-O3 -std=gnu17 -include locale.h -Wno-implicit-function-declaration $1" LDFLAGS="$LDFLAGS_EXTRA"
  make -j "$JOBS"
  cp "frontend/lame$SUFFIX" "$OUT/$2$SUFFIX"
 }
 if [ "$(uname -m)" = x86_64 ]; then
  lame_build "-mfma" lame
  # PCs without AVX2/FMA use this; output works but is not byte-identical.
  lame_build "" lame-compat
 else
  lame_build "" lame
 fi
)
if [ ! -d "$CACHE/jpegli/.git" ]; then git clone --recursive https://github.com/google/jpegli.git "$CACHE/jpegli"; fi
(
 cd "$CACHE/jpegli"
 git checkout 031a0077f5799a6041004267fc12b956c1f52a20
 git submodule update --init --recursive
 cmake -S . -B native-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DJPEGLI_STATIC=ON \
  -DJPEGLI_ENABLE_TOOLS=ON -DJPEGLI_ENABLE_BENCHMARK=OFF -DJPEGLI_ENABLE_TESTS=OFF -DJPEGLI_ENABLE_DOXYGEN=OFF \
  -DJPEGLI_ENABLE_MANPAGES=OFF -DJPEGLI_ENABLE_JNI=OFF -DJPEGLI_ENABLE_OPENEXR=OFF -DJPEGLI_BUNDLE_LIBPNG=ON \
  -DCMAKE_EXE_LINKER_FLAGS="$CXX_LDFLAGS" -DCMAKE_POLICY_VERSION_MINIMUM=3.5
 cmake --build native-build --target cjpegli -j "$JOBS"
 cp "native-build/tools/cjpegli$SUFFIX" "$OUT/"
)
# Retain complete corresponding sources, pinned archive hashes and build recipe.
mkdir -p "$OUT/sources" "$OUT/licenses"
cp "$CACHE/ffmpeg.tar.xz" "$OUT/sources/ffmpeg-8.1.2.tar.xz"
# LAME's archive as published, but for one readme (TinyXML's zlib license) in
# Windows-1252: re-encoded to UTF-8 so the package content check can read it.
python3 - "$CACHE/lame.tar.gz" "$OUT/sources/lame-4.0.tar.gz" <<'PY'
import gzip, io, sys, tarfile
raw = io.BytesIO()
with tarfile.open(sys.argv[1], 'r:gz') as t, tarfile.open(fileobj=raw, mode='w', format=t.format) as o:
    for m in t:
        data = t.extractfile(m).read() if m.isfile() else None
        if m.name == 'lame-4.0/ACM/tinyxml/readme.txt':
            data = data.decode('cp1252').encode('utf-8')
        if data is not None:
            m.size = len(data)
        o.addfile(m, io.BytesIO(data) if data is not None else None)
with open(sys.argv[2], 'wb') as f, gzip.GzipFile(fileobj=f, mode='wb', mtime=0, filename='') as z:
    z.write(raw.getvalue())
PY
# jpegli's checkout without its test data and fuzz/crash fixtures, which are
# not sources of the built tool.
tar --exclude=.git --exclude=native-build --exclude=jpegli/testdata --exclude=jpegli/third_party/libpng/contrib/testpngs \
  --exclude=jpegli/third_party/skcms/profiles -czf "$OUT/sources/jpegli-031a0077.tar.gz" -C "$CACHE" jpegli
cp "$CACHE/ffmpeg-8.1.2/COPYING.LGPLv2.1" "$OUT/licenses/FFmpeg-LGPL-2.1.txt"
cp "$CACHE/lame-4.0/COPYING" "$OUT/licenses/LAME.txt"
cp "$CACHE/jpegli/LICENSE" "$OUT/licenses/jpegli.txt"
cp "$0" "$OUT/sources/build_tools.sh"
cp "$SCRIPT_DIR/lame-4.0-portable.patch" "$OUT/sources/"
