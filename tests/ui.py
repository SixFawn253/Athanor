"""Exercise the original UI harness with portable fixtures and capture every state."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from smoke import generate

binary = Path(sys.argv[1]).resolve()
captures = Path(sys.argv[2]).resolve()
captures.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="athanor-ui-") as folder:
    root = Path(folder)
    generate(root)
    env = dict(os.environ, ATHANOR_TEST="1", ATHANOR_SETTINGS_DIR=str(root / "settings"),
               ATHANOR_TEST_FIXTURES=str(root), ATHANOR_TEST_OUTPUT_DIR=str(root / "output"))
    for mode in ("main", "image", "video", "audio", "pdf"):
        env["ATHANOR_UI_CATEGORY"] = "" if mode == "main" else mode
        target = captures / mode
        args = [str(binary), "--self-test", str(target)]
        if mode != "main":
            args.append("--quick")
        result = subprocess.run(args, env=env, capture_output=True, text=True, encoding="utf-8", timeout=60)
        state_file = target / "ui-result.json"
        assert state_file.exists(), (mode, result.stdout, result.stderr)
        state = json.loads(state_file.read_text())
        assert result.returncode == 0 and state["ok"], (mode, state, result.stdout, result.stderr)
        assert list(target.glob("*.png")), "UI capture images missing"
        print(mode, "UI checks passed", flush=True)
