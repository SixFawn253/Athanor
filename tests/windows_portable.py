"""Launch the original Windows wrapper with the newly deployed shared runtime."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import wave
from smoke import png

binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="athanor-portable-") as folder:
    root = Path(folder)
    windows = Path(os.environ["SystemRoot"])
    env = dict(os.environ, ATHANOR_TEST="1", ATHANOR_PORTABLE_CACHE=str(root / "cache"),
               QT_QPA_PLATFORM="windows", PATH=os.pathsep.join(map(str, [windows / "System32", windows])))
    for name in ("ATHANOR_TOOLS_DIR", "QT_PLUGIN_PATH", "QML2_IMPORT_PATH", "QML_IMPORT_PATH"):
        env.pop(name, None)

    def run(*args):
        result = subprocess.run([str(binary), *map(str, args)], env=env, capture_output=True,
                                text=True, encoding="utf-8", timeout=120)
        assert result.returncode == 0, (args, result.stdout, result.stderr)
        return result

    run("--cli")
    run("--qml-check")
    run("--quick", "--qml-check")
    source = root / "foto à ' $ test.png"
    png(source)
    result = run("--cli", "--image", "webp", "--delete-originals", source)
    record = json.loads(result.stdout.strip().splitlines()[-1])
    assert record["ok"] and Path(record["output"]).is_file() and not source.exists(), record
    audio = root / "sound.wav"
    with wave.open(str(audio), "wb") as stream:
        stream.setparams((1, 2, 16000, 0, "NONE", "not compressed"))
        stream.writeframes(b"\x00\x00" * 4000)
    result = run("--cli", "--audio", "opus", audio)
    record = json.loads(result.stdout.strip().splitlines()[-1])
    assert record["ok"] and Path(record["output"]).is_file() and audio.exists(), record
    print("Single-file Windows launch, full/compact QML and bundled conversion passed", flush=True)
