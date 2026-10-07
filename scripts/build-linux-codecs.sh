#!/usr/bin/env bash
set -euo pipefail
# Build the APIs used by the unchanged conversion engine. Ubuntu's older
# libavif/libheif/MuPDF packages do not expose all of those APIs.
root=$(cd "$(dirname "$0")/.." && pwd)
work="$root/.build/codecs"
prefix="$root/.build/sdk"
mkdir -p "$work" "$prefix/include" "$prefix/lib" "$prefix/licenses"
clone() {
    if [ ! -d "$work/$1/.git" ]; then
        git clone --depth 1 --branch "$2" "$3" "$work/$1"
    fi
}
clone mupdf 1.28.2 https://github.com/ArtifexSoftware/mupdf.git
git -C "$work/mupdf" submodule update --init --depth 1
make -C "$work/mupdf" -j2 build=release HAVE_X11=no HAVE_GLUT=no HAVE_CURL=no HAVE_OCR=no libs
cp -R "$work/mupdf/include/mupdf" "$prefix/include/"
cp "$work/mupdf/build/release/libmupdf.a" "$work/mupdf/build/release/libmupdf-third.a" "$prefix/lib/"
cp "$work/mupdf/COPYING" "$prefix/licenses/MuPDF-COPYING"
clone avif v1.4.2 https://github.com/AOMediaCodec/libavif.git
cmake -S "$work/avif" -B "$work/avif-build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_INSTALL_RPATH='$ORIGIN/../lib' -DBUILD_SHARED_LIBS=ON -DAVIF_CODEC_AOM=SYSTEM -DAVIF_CODEC_DAV1D=SYSTEM -DAVIF_LIBYUV=OFF -DAVIF_BUILD_APPS=ON
cmake --build "$work/avif-build" -j2
cmake --install "$work/avif-build"
cp "$work/avif/LICENSE" "$prefix/licenses/libavif-LICENSE"
clone heif v1.23.6 https://github.com/strukturag/libheif.git
cmake -S "$work/heif" -B "$work/heif-build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DWITH_LIBDE265_PLUGIN=OFF -DWITH_X265_PLUGIN=OFF -DENABLE_PLUGIN_LOADING=OFF -DWITH_EXAMPLES=OFF \
    -DWITH_GDK_PIXBUF=OFF -DBUILD_TESTING=OFF
cmake --build "$work/heif-build" -j2
cmake --install "$work/heif-build"
cp "$work/heif/COPYING" "$prefix/licenses/libheif-COPYING"
