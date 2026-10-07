"""Test matching-release selection and Unix installation/rollback in temporary roots."""
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import threading
import zipfile

binary = Path(sys.argv[1]).resolve()
package_root = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else None
system = {"Darwin": "macOS", "Windows": "Windows", "Linux": "Linux"}[platform.system()]
arch = {"x86_64": "x64", "AMD64": "x64", "arm64": "arm64", "aarch64": "arm64"}[platform.machine()]
suffix = {"macOS": ".zip", "Windows": ".exe", "Linux": ".tar.gz"}[system]
release = {}


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        body = json.dumps(release).encode()
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


with tempfile.TemporaryDirectory(prefix="athanor-updater-") as folder:
    root = Path(folder).resolve()
    qpa = {"macOS": "cocoa", "Windows": "windows", "Linux": "xcb"}[system] if package_root else "offscreen"
    env = dict(os.environ, ATHANOR_TEST="1", QT_QPA_PLATFORM=qpa, ATHANOR_SETTINGS_DIR=str(root / "settings"))
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    env["ATHANOR_TEST_UPDATE_API"] = f"http://127.0.0.1:{server.server_port}/release"
    env["ATHANOR_LAUNCHER_PATH"] = str(root / "Athanor.exe")
    expected = f"Athanor-Alpha-9.9-{system}-{arch}{suffix}"
    asset = {"name": expected, "state": "uploaded", "size": 100,
             "digest": "sha256:" + "a" * 64,
             "browser_download_url": "https://github.com/SixFawn253/Athanor/releases/download/alpha-9.9/" + expected}

    def check_release(available):
        result = subprocess.run([str(binary), "--update-test", str(root)], env=env, capture_output=True, text=True, encoding="utf-8", timeout=30)
        assert result.returncode == 0, (result.stdout, result.stderr)
        state = json.loads(result.stdout.strip().splitlines()[-1])
        assert bool(state["version"]) == available, state
        return state

    release.update(tag_name="alpha-9.9", name="Athanor 9.9", draft=False, prerelease=False, assets=[asset])
    check_release(True)
    asset["name"] = "Athanor-Alpha-9.9-OtherOS-x64.exe"
    check_release(False)
    asset["name"] = expected
    asset["digest"] = "sha256:invalid"
    check_release(False)
    asset["digest"] = "sha256:" + "a" * 64
    release["prerelease"] = True
    check_release(False)
    release["prerelease"] = False
    release["tag_name"] = "alpha-1.0"
    check_release(False)
    server.shutdown()
    print("Release OS/architecture, digest, prerelease and version checks passed", flush=True)

    if system != "Windows":
        name = "Athanor.app" if system == "macOS" else "Athanor"
        executable = Path("Contents/MacOS/Athanor") if system == "macOS" else Path("usr/bin/Athanor")
        target = root / name
        replacement = root / "new" / name
        for destination in (target, replacement):
            if package_root:
                shutil.copytree(package_root, destination, symlinks=True)
            else:
                (destination / executable).parent.mkdir(parents=True)
                shutil.copy2(binary, destination / executable)
            if system == "Linux":
                (destination / ".athanor-portable").write_text("portable\n")
        (target / "generation").write_text("old")
        (replacement / "generation").write_text("new")
        archive = root / ("update" + suffix)
        def make_archive(path):
            if system == "macOS" and package_root:
                # Match the release packager, including Qt framework symlinks.
                subprocess.run(["/usr/bin/ditto", "-c", "-k", "--keepParent", str(replacement), str(path)], check=True)
            elif system == "macOS":
                with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as zip:
                    for entry in replacement.rglob("*"):
                        if entry.is_file():
                            zip.write(entry, entry.relative_to(replacement.parent))
            else:
                with tarfile.open(path, "w:gz") as tar:
                    tar.add(replacement, arcname=name)
        make_archive(archive)
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        parent = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(.5)"])
        reaper = threading.Thread(target=parent.wait)
        reaper.start()
        args = [str(target / executable), "--apply-update", str(archive), str(target), str(parent.pid)]
        rejected = subprocess.run([*args, "b" * 64], env=env, capture_output=True, text=True, encoding="utf-8", timeout=10)
        assert rejected.returncode != 0 and (target / "generation").read_text() == "old", rejected.stdout
        installed = subprocess.run([*args, digest], env=env, capture_output=True, text=True, encoding="utf-8", timeout=30)
        reaper.join()
        assert installed.returncode == 0 and (target / "generation").read_text() == "new", (installed.stdout, installed.stderr)
        assert not archive.exists() and not list(root.glob("*.athanor-backup-*"))
        # A verified archive whose replacement cannot launch must keep the old app.
        (replacement / executable).write_bytes(b"not an executable")
        broken_archive = root / ("broken" + suffix)
        make_archive(broken_archive)
        digest = hashlib.sha256(broken_archive.read_bytes()).hexdigest()
        rejected = subprocess.run([str(target / executable), "--apply-update", str(broken_archive), str(target), str(os.getpid()), digest], env=env, capture_output=True, text=True, encoding="utf-8", timeout=15)
        assert rejected.returncode != 0 and (target / "generation").read_text() == "new"
        print("Update installation, restart, checksum rejection and broken-runtime retention passed", flush=True)
