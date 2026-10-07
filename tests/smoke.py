"""End-to-end checks using generated fixtures; no private files or Python packages."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib


def png(path, opaque=False, phase=0):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    width = height = 128
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            rows.extend(((x * 2 + phase) % 256, y * 2, (x + y + phase) % 256, 255 if opaque else x * 2))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def pdf(path):
    # Two selectable-text pages with a link annotation and a bookmark.
    content = b"BT /F1 12 Tf 20 70 Td (Athanor portable PDF test) Tj ET"
    objects = [
        b"<< /Type /Catalog /Pages 2 0 R /Outlines 7 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R 6 0 R] /Count 2 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 128 128] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R /Annots [9 0 R] >>",
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        b"<< /Length " + str(len(content)).encode() + b" >>\nstream\n" + content + b"\nendstream",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 128 128] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R >>",
        b"<< /Type /Outlines /First 8 0 R /Last 8 0 R /Count 1 >>",
        b"<< /Title (Test bookmark) /Parent 7 0 R /Dest [3 0 R /Fit] >>",
        b"<< /Type /Annot /Subtype /Link /Rect [10 10 80 20] /A << /S /URI /URI (https://example.org/) >> >>",
    ]
    data = bytearray(b"%PDF-1.4\n")
    offsets = [0]
    for i, obj in enumerate(objects, 1):
        offsets.append(len(data))
        data.extend(f"{i} 0 obj\n".encode() + obj + b"\nendobj\n")
    xref = len(data)
    data.extend(f"xref\n0 {len(offsets)}\n0000000000 65535 f \n".encode())
    for offset in offsets[1:]:
        data.extend(f"{offset:010} 00000 n \n".encode())
    data.extend(f"trailer\n<< /Size {len(offsets)} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode())
    path.write_bytes(data)


def run(binary, root, *args, ok=True):
    env = dict(os.environ, ATHANOR_TEST="1", ATHANOR_SETTINGS_DIR=str(root / "settings"),
               ATHANOR_TEST_FIXTURES=str(root), ATHANOR_TEST_OUTPUT_DIR=str(root / "test-output"))
    result = subprocess.run([str(binary), *map(str, args)], capture_output=True, text=True, encoding="utf-8", env=env, timeout=180)
    records = []
    for line in result.stdout.splitlines():
        try:
            records.append(json.loads(line))
        except json.JSONDecodeError:
            pass
    finals = [record for record in records if "ok" in record]
    if ok:
        assert result.returncode == 0 and finals and all(record["ok"] for record in finals), (args, result.stdout, result.stderr)
        for record in finals:
            if "output" in record:
                assert Path(record["output"]).exists(), record
    else:
        assert result.returncode != 0 and finals and not finals[-1]["ok"], (args, result.stdout, result.stderr)
    return finals[-1]


def tool(name):
    override = os.environ.get("ATHANOR_TOOLS_DIR")
    path = Path(override) / (name + (".exe" if os.name == "nt" else "")) if override else None
    return str(path) if path else shutil.which(name)


def generate(root):
    png(root / "Apple_first_logo.png")
    png(root / "frame-0.png", phase=0)
    png(root / "frame-1.png", phase=20)
    png(root / "opaque.png", opaque=True)
    ffmpeg = tool("ffmpeg")
    assert ffmpeg, "FFmpeg is required for media fixtures"
    def ff(*args):
        subprocess.run([ffmpeg, "-nostdin", "-v", "error", "-y", *map(str, args)], check=True, timeout=60)
    ff("-i", root / "opaque.png", root / "artificial.jpg")
    ff("-framerate", "2", "-i", root / "frame-%d.png", "-plays", "0", root / "animated.apng")
    ff("-framerate", "2", "-i", root / "frame-%d.png", "-filter_complex",
       "split[a][b];[a]palettegen[p];[b][p]paletteuse", root / "astrid.gif")
    ff("-f", "lavfi", "-i", "testsrc2=size=128x128:rate=10:duration=1", "-f", "lavfi", "-i",
       "sine=frequency=440:duration=1", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", "-shortest",
       root / "Recording 2026-10-06 070218.mp4")
    ff("-f", "lavfi", "-i", "sine=frequency=440:duration=1", root / "sound.wav")
    ff("-i", root / "Recording 2026-10-06 070218.mp4", "-c:v", "copy", "-c:a", "libopus", root / "video-opus.mkv")
    pdf(root / "document.pdf")


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="athanor-smoke-") as folder:
        root = Path(folder)
        generate(root)
        source = root / "Apple_first_logo.png"
        output = root / "output"
        output.mkdir()
        cli = ("--cli", "--speed", "Fast", "--threads", "2", "--output", output)
        for fmt in ("avif", "webp", "heic", "heif", "jpg", "png", "ico", "pdf"):
            result = run(binary, root, *cli, "--image", fmt, source)
            assert Path(result["output"]).suffix == "." + fmt
            print("image", fmt, flush=True)
        for fmt in ("avif", "webp", "heic", "heif", "png"):
            run(binary, root, *cli, "--image", fmt, "--lossless", source)
            print("lossless image", fmt, flush=True)
        webp = run(binary, root, *cli, "--image", "webp", root / "animated.apng")
        shutil.copyfile(webp["output"], root / "willowshore-waldgeist.webp")
        run(binary, root, *cli, "--image", "avif", root / "willowshore-waldgeist.webp")
        # Queue combination expects still images; use a still WebP for that fixture.
        still = run(binary, root, *cli, "--image", "webp", source)
        shutil.copyfile(still["output"], root / "willowshore-waldgeist.webp")
        for fmt in ("opus", "mp3", "wav"):
            run(binary, root, *cli, "--audio", fmt, root / "sound.wav")
            print("audio", fmt, flush=True)
        video = root / "Recording 2026-10-06 070218.mp4"
        for fmt in ("webm", "mkv", "av1", "mp4", "gif", "opus", "mp3", "wav"):
            run(binary, root, *cli, "--acceleration", "CPU", "--video", fmt, video)
            print("video", fmt, flush=True)
        for fmt in ("webm", "mkv", "av1"):
            run(binary, root, *cli, "--video", fmt, "--lossless", root / "video-opus.mkv" if fmt == "webm" else video)
            print("lossless video", fmt, flush=True)
        for fmt in ("webm", "mkv", "av1"):
            run(binary, root, *cli, "--video", fmt, root / "astrid.gif")
        run(binary, root, *cli, "--video", "mkv", "--lossless", root / "astrid.gif")
        for profile in ("balanced", "smallest", "extreme", "lossless"):
            run(binary, root, *cli, "--pdf", profile, root / "document.pdf")
        pages = run(binary, root, *cli, "--pdf-output", "jpg", root / "document.pdf")
        assert len(list(Path(pages["output"]).glob("page-*.jpg"))) == 2
        combined = run(binary, root, *cli, "--image", "pdf", source, root / "artificial.jpg")
        combined_pages = run(binary, root, *cli, "--pdf-output", "jpg", combined["output"])
        assert len(list(Path(combined_pages["output"]).glob("page-*.jpg"))) == 2
        for args, input_file in [(("--image", "webp"), source), (("--audio", "opus"), root / "sound.wav"),
                                 (("--video", "webm"), video), (("--pdf", "balanced"), root / "document.pdf")]:
            run(binary, root, *cli, *args, "--target-size", "0.02", input_file)
        first = run(binary, root, *cli, "--image", "png", source)
        contents = Path(first["output"]).read_bytes()
        second = run(binary, root, *cli, "--image", "png", source)
        assert first["output"] != second["output"] and Path(first["output"]).read_bytes() == contents
        unicode = root / "foto à ' $ test.png"
        shutil.copyfile(source, unicode)
        run(binary, root, *cli, "--image", "png", "--delete-originals", unicode)
        assert not unicode.exists()
        broken = root / "broken.png"
        broken.write_bytes(b"invalid image")
        run(binary, root, *cli, "--delete-originals", broken, ok=False)
        assert broken.exists() and source.exists()
        assert not list(output.glob(".athanor-*")), "Temporary output workspaces were left behind"
        for test in ("--controller-test", "--queue-test", "--format-queue-test"):
            run(binary, root, test, root)
            print(test, "passed", flush=True)
        print("All conversion, queue, preview, cancellation and publication checks passed", flush=True)


if __name__ == "__main__":
    main()
