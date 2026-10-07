"""Create relocatable distributions from the same CMake target on each OS."""
import json
import os
from pathlib import Path
import platform
import plistlib
import shutil
import subprocess
import sys
import urllib.request

repo = Path(__file__).resolve().parents[1]
build = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve()
output.mkdir(parents=True, exist_ok=True)
system = platform.system()
arch = {"x86_64": "x64", "AMD64": "x64", "aarch64": "arm64", "arm64": "arm64"}[platform.machine()]
version = "1.2.1"


def run(*args, **kwargs):
    return subprocess.run(list(map(str, args)), check=True, **kwargs)


def command(*names):
    for name in names:
        path = shutil.which(name)
        if path:
            return path
    raise RuntimeError("Missing packaging tool: " + "/".join(names))


def copy_tools(destination):
    destination.mkdir(parents=True, exist_ok=True)
    tools = []
    for name in ("ffmpeg", "ffprobe", "avifenc"):
        source = Path(command(name))
        target = destination / source.name
        shutil.copy2(source, target)
        tools.append(target)
    encoders = subprocess.check_output([str(tools[0]), "-hide_banner", "-encoders"], text=True, stderr=subprocess.STDOUT)
    for codec in ("libaom-av1", "libsvtav1", "libvpx-vp9", "libx264", "libx264rgb", "ffv1", "libopus", "libmp3lame"):
        assert codec in encoders, "Bundled FFmpeg is missing " + codec
    return tools


def verify(binary, app):
    env = dict(os.environ, ATHANOR_TEST="1", QT_QPA_PLATFORM={"Darwin": "cocoa", "Windows": "windows", "Linux": "xcb"}[system])
    # Remove developer search paths: launch and convert with the packaged codecs.
    env.pop("ATHANOR_TOOLS_DIR", None)
    env.pop("QT_PLUGIN_PATH", None)
    env.pop("QML2_IMPORT_PATH", None)
    env.pop("LD_LIBRARY_PATH", None)
    env.pop("DYLD_LIBRARY_PATH", None)
    env.pop("QML_IMPORT_PATH", None)
    if system == "Windows":
        windows = Path(os.environ["SystemRoot"])
        env["PATH"] = os.pathsep.join(map(str, [binary.parent, binary.parent / "tools", windows / "System32", windows]))
    elif system == "Darwin":
        env["PATH"] = os.pathsep.join(map(str, [binary.parent, binary.parent.parent / "Resources/tools", "/usr/bin", "/bin"]))
    else:
        env["PATH"] = os.pathsep.join(map(str, [binary.parent, "/usr/bin", "/bin"]))
        headless = dict(env)
        for name in ("DISPLAY", "WAYLAND_DISPLAY", "QT_QPA_PLATFORM"):
            headless.pop(name, None)
        run(binary, "--cli", env=headless)
    run(binary, "--qml-check", env=env)
    run(binary, "--quick", "--qml-check", env=env)
    run(sys.executable, repo / "tests/smoke.py", binary, env=env)
    run(sys.executable, repo / "tests/ui.py", binary, output / "ui-packaged", env=env)
    run(sys.executable, repo / "tests/updater.py", binary, app, env=env)


if system == "Darwin":
    app = output / "Athanor.app"
    if app.exists():
        raise RuntimeError("Choose an empty packaging output directory")
    run("cmake", "--install", build, "--prefix", output)
    resources = app / "Contents/Resources"
    resources.mkdir(parents=True, exist_ok=True)
    tools = copy_tools(resources / "tools")
    shutil.copytree(output / "licenses", resources / "licenses", dirs_exist_ok=True)
    shutil.copy2(repo / "THIRD-PARTY-NOTICES.md", resources)
    # Record source dependencies and preserve their distributed license texts.
    dependencies = subprocess.check_output(["brew", "list", "--formula", "--versions"], text=True)
    (resources / "DEPENDENCIES.txt").write_text(dependencies)
    cellar = Path(subprocess.check_output(["brew", "--cellar"], text=True).strip())
    for formula in cellar.iterdir():
        if not formula.is_dir():
            continue
        for installed in formula.iterdir():
            for license_dir in (installed / "share/licenses", installed / ".brew"):
                if license_dir.exists():
                    shutil.copytree(license_dir, resources / "licenses/homebrew" / formula.name / license_dir.name, dirs_exist_ok=True)
            for license_file in installed.glob("*COPYING*"):
                shutil.copy2(license_file, resources / "licenses" / (formula.name + "-" + license_file.name))
    run(command("macdeployqt", "macdeployqt6"), app, "-qmldir=" + str(repo / "native"),
        *("-executable=" + str(tool) for tool in tools), "-verbose=1")
    # macdeployqt rewrites dependency references, but some Homebrew dylibs keep
    # an absolute install ID. Normalize those IDs before signing the bundle.
    for library in (app / "Contents/Frameworks").glob("*.dylib"):
        run("/usr/bin/install_name_tool", "-id", "@rpath/" + library.name, library)
        commands = subprocess.check_output(["otool", "-l", str(library)], text=True)
        paths = [line.strip().split(" (offset", 1)[0][5:] for line in commands.splitlines() if line.strip().startswith("path ")]
        if "@loader_path" not in paths:
            run("/usr/bin/install_name_tool", "-add_rpath", "@loader_path", library)
    for tool in tools:
        commands = subprocess.check_output(["otool", "-l", str(tool)], text=True)
        if "@loader_path/../../Frameworks" not in commands:
            run("/usr/bin/install_name_tool", "-add_rpath", "@loader_path/../../Frameworks", tool)
    # Executables under Resources/tools are not discovered by --deep. Sign
    # these explicit code objects after install_name_tool rewrites their links.
    for tool in tools:
        run("/usr/bin/codesign", "--force", "--sign", "-", tool)
    run("/usr/bin/codesign", "--force", "--deep", "--sign", "-", app)
    run("/usr/bin/codesign", "--verify", "--deep", "--strict", app)
    binary = app / "Contents/MacOS/Athanor"
    # macdeployqt also relocates the bundled tools' non-Qt dylibs.
    for path in [binary, *tools, *(app / "Contents/Frameworks").glob("*.dylib")]:
        links = subprocess.check_output(["otool", "-L", str(path)], text=True)
        assert "/opt/homebrew/" not in links and "/usr/local/" not in links, links
    verify(binary, app)
    archive = output / f"Athanor-Alpha-{version}-macOS-{arch}.zip"
    run("/usr/bin/ditto", "-c", "-k", "--keepParent", app, archive)
elif system == "Linux":
    app = output / "Athanor"
    if app.exists():
        raise RuntimeError("Choose an empty packaging output directory")
    run("cmake", "--install", build, "--prefix", app / "usr")
    (app / ".athanor-portable").write_text("Athanor portable distribution\n")
    tools = copy_tools(app / "usr/bin")
    cache = build / "packaging-tools"
    cache.mkdir(parents=True, exist_ok=True)
    for name in ("linuxdeploy", "linuxdeploy-plugin-qt"):
        target = cache / (name + "-" + platform.machine() + ".AppImage")
        if not target.exists():
            urllib.request.urlretrieve(f"https://github.com/linuxdeploy/{name}/releases/download/continuous/{target.name}", target)
            target.chmod(0o755)
    qt_plugins = Path(subprocess.check_output([command("qmake", "qmake6"), "-query", "QT_INSTALL_PLUGINS"], text=True).strip())
    platform_plugins = ["libqoffscreen.so", *(path.name for path in (qt_plugins / "platforms").glob("libqwayland*.so"))]
    env = dict(os.environ, APPIMAGE_EXTRACT_AND_RUN="1", QML_SOURCES_PATHS=str(repo / "native"),
               QMAKE=command("qmake", "qmake6"), PATH=str(cache) + os.pathsep + os.environ["PATH"],
               LD_LIBRARY_PATH=str(repo / ".build/sdk/lib") + os.pathsep + os.environ.get("LD_LIBRARY_PATH", ""),
               EXTRA_PLATFORM_PLUGINS=";".join(platform_plugins))
    # CMake removes build RPATHs when installing. Expose the source-built SDK
    # while linuxdeploy collects it; verification below clears this search path.
    run(cache / ("linuxdeploy-" + platform.machine() + ".AppImage"), "--appdir", app,
        "--executable", app / "usr/bin/Athanor", *(arg for tool in tools for arg in ("--executable", tool)),
        "--desktop-file", repo / "packaging/io.github.SixFawn253.Athanor.desktop",
        "--icon-file", app / "usr/share/icons/hicolor/256x256/apps/io.github.SixFawn253.Athanor.png", "--plugin", "qt", env=env)
    sdk_licenses = repo / ".build/sdk/licenses"
    if sdk_licenses.exists():
        shutil.copytree(sdk_licenses, app / "usr/licenses/source-codecs", dirs_exist_ok=True)
    shutil.copytree("/usr/share/doc", app / "usr/licenses/system", ignore=lambda directory, names: [name for name in names if name != "copyright" and not (Path(directory) / name).is_dir()])
    verify(app / "usr/bin/Athanor", app)
    run("tar", "-czf", output / f"Athanor-Alpha-{version}-Linux-{arch}.tar.gz", "-C", output, "Athanor")
else:
    app = output / "Athanor"
    if app.exists():
        raise RuntimeError("Choose an empty packaging output directory")
    run("cmake", "--install", build, "--prefix", app)
    tools = copy_tools(app / "tools")
    run(command("windeployqt6", "windeployqt"), "--release", "--qmldir", repo / "native", app / "Athanor.exe")
    # Resolve the transitive DLL closure for the app, Qt plugins and all tools.
    pending = [app / "Athanor.exe", *tools, *app.rglob("*.dll")]
    copied = {path.name.lower() for path in pending}
    prefixes = [Path(path) for path in os.environ["PATH"].split(os.pathsep)]
    while pending:
        binary = pending.pop()
        imports = subprocess.check_output([command("objdump"), "-p", str(binary)], text=True, errors="replace")
        for line in imports.splitlines():
            if "DLL Name:" not in line:
                continue
            name = line.split("DLL Name:", 1)[1].strip()
            if name.lower() in copied:
                continue
            source = next((prefix / name for prefix in prefixes if (prefix / name).is_file()), None)
            if source is None or "windows" in str(source).lower():
                continue  # Windows system DLLs are supplied by the OS.
            target = app / name
            shutil.copy2(source, target)
            copied.add(name.lower())
            pending.append(target)
    # Windows loads dependency DLLs relative to each child tool's executable.
    for dll in app.glob("*.dll"):
        shutil.copy2(dll, app / "tools" / dll.name)
    prefix = Path(command("cmake")).parent.parent
    if (prefix / "share/licenses").exists():
        shutil.copytree(prefix / "share/licenses", app / "licenses/msys2", dirs_exist_ok=True)
    verify(app / "Athanor.exe", app)
    shutil.make_archive(str(output / f"Athanor-Alpha-{version}-Windows-{arch}"), "zip", output, "Athanor")
print("Verified distributions:", output, flush=True)
