# Third-party components

- Qt 6.8.3: LGPL-3.0/GPL-3.0 and Qt exceptions. https://code.qt.io/cgit/qt/qtbase.git/tag/?h=v6.8.3 and https://code.qt.io/cgit/qt/qtdeclarative.git/tag/?h=v6.8.3
- MuPDF 1.28.2: AGPL-3.0. https://mupdf.com/ and https://github.com/ArtifexSoftware/mupdf/tree/1.28.2
- libwebp 1.6.0: BSD-3-Clause. https://github.com/webmproject/libwebp/tree/v1.6.0
- libavif 1.4.2 and dav1d: BSD-2-Clause. Custom decoder size limit and build provenance are recorded in tools/avif-runtime/SOURCES.json. https://github.com/AOMediaCodec/libavif/tree/v1.4.2
- libheif 1.23.4 and its bundled HEVC codec dependencies: LGPL/GPL and component licenses retained in licenses/pillow-heif. https://github.com/strukturag/libheif/tree/v1.23.4
- FFmpeg and ffprobe: Gyan GPL-enabled static build, commit 94422871fc. Build configuration and license are in licenses. https://github.com/FFmpeg/FFmpeg/tree/94422871fc
- Microsoft Visual C++ runtime: Microsoft redistributable runtime binaries.

License notices from the original dependency packages are retained in the licenses folder. Athanor uses the native libraries directly; Python is not included in the runtime.

The versions and Windows build provenance above describe the original managed
runtime. Cross-platform builds use the native dependencies documented in
README.md. macOS bundles preserve Homebrew formula/build provenance and the
installed dependency environment in `Contents/Resources/DEPENDENCIES.txt`;
Windows MSYS2 builds retain their package license texts. Linux builds retain
the MuPDF 1.28.2, libavif 1.4.2 and libheif 1.23.6 license texts alongside
the system dependency copyright notices. Public releases must retain the
corresponding dependency sources and build configuration as applicable.
