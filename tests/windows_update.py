"""Verify Windows update download/application for unpacked and single-file apps."""
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time

runtime = Path(sys.argv[1]).resolve()
portable = Path(sys.argv[2]).resolve()
fixtures = Path(sys.argv[3]).resolve()
legacy = Path(sys.argv[4]).resolve() if len(sys.argv) > 4 else portable
payload = portable.read_bytes()
digest = hashlib.sha256(payload).hexdigest()
corrupt = False

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/download':
            body = b'bad download' if corrupt else payload
        else:
            body = json.dumps(dict(tag_name='alpha-9.9',name='Alpha 9.9',draft=False,prerelease=False,
                assets=[dict(name='Athanor-Alpha-9.9-Windows-x64.exe',state='uploaded',size=len(payload),
                digest='sha256:'+digest,browser_download_url='https://github.com/SixFawn253/Athanor/releases/download/alpha-9.9/Athanor-Alpha-9.9-Windows-x64.exe')])).encode()
        self.send_response(200)
        self.send_header('Content-Length',str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def log_message(self,*args): pass

server = ThreadingHTTPServer(('127.0.0.1',0),Handler)
threading.Thread(target=server.serve_forever,daemon=True).start()
base = f'http://127.0.0.1:{server.server_port}'
originals = {p.name:(p.stat().st_size,p.stat().st_mtime_ns) for p in fixtures.iterdir() if p.is_file()}
try:
    with tempfile.TemporaryDirectory(prefix='athanor-windows-update-') as folder:
        root = Path(folder)
        for mode in ['folder','launcher']:
            case = root/mode
            case.mkdir()
            cache = case/'cache'
            if mode == 'folder':
                install = case/'Athanor ü portable'
                shutil.copytree(runtime,install)
                target = install/'Athanor.exe'
                settings = install
            else:
                target = case/'Athanor ü.exe'
                shutil.copy2(legacy,target)
                settings = cache/'settings'
                settings.mkdir(parents=True)
            settingsFile = settings/'settings.json'
            settingsFile.write_text('{"appearance":"Light","quality":63}',encoding='utf8')
            saved = settingsFile.read_bytes()
            before = hashlib.sha256(target.read_bytes()).hexdigest()
            env = dict(os.environ,ATHANOR_TEST='1',QT_QPA_PLATFORM='windows',ATHANOR_PORTABLE_CACHE=str(cache),
                ATHANOR_TEST_UPDATE_API=base+'/release',ATHANOR_TEST_UPDATE_DOWNLOAD=base+'/download',
                ATHANOR_TEST_UPDATE_INSTALL='1',ATHANOR_TEST_FIXTURES=str(fixtures),
                ATHANOR_TEST_OUTPUT_DIR=str(case/'scratch'))
            for key in ['ATHANOR_SETTINGS_DIR','ATHANOR_LAUNCHER_PATH','ATHANOR_LAUNCHER_PID']:
                env.pop(key,None)
            def update():
                r = subprocess.run([str(target),'--update-test',str(case)],env=env,capture_output=True,
                    text=True,encoding='utf8',timeout=90)
                assert r.returncode == 0,(r.stdout,r.stderr)
                return json.loads(r.stdout.strip().splitlines()[-1])
            corrupt = True
            state = update()
            assert 'could not be verified' in state['status'],state
            assert hashlib.sha256(target.read_bytes()).hexdigest() == before
            assert settingsFile.read_bytes() == saved
            assert not list((settings/'updates').glob('update-*.exe'))
            print(mode,'rejects bad download and keeps app/settings',flush=True)
            corrupt = False
            state = update()
            assert 'Restarting' in state['status'],state
            deadline = time.monotonic()+20
            while time.monotonic() < deadline:
                if hashlib.sha256(target.read_bytes()).hexdigest() == digest: break
                time.sleep(.1)
            assert hashlib.sha256(target.read_bytes()).hexdigest() == digest
            assert settingsFile.read_bytes() == saved
            assert not list(target.parent.glob('.athanor-update-*'))
            print(mode,'downloads, verifies, applies and restarts without manual installation',flush=True)
            env.pop('ATHANOR_TEST_UPDATE_INSTALL')
            captures = case/'UI'
            r = subprocess.run([str(target),'--quick','--self-test',str(captures)],env=env,
                capture_output=True,text=True,encoding='utf8',timeout=45)
            result = json.loads((captures/'ui-result.json').read_text(encoding='utf8'))
            assert r.returncode == 0 and result['ok'],(result.get('errors'),r.stderr)
            installedSettings = cache/'settings/settings.json'
            assert json.loads(installedSettings.read_text(encoding='utf8'))['appearance'] == 'Dark'
            assert (cache/'.installed').exists()
            print(mode,'uses installed storage after in-place update; settings preserved',flush=True)
finally:
    server.shutdown()
    assert originals == {p.name:(p.stat().st_size,p.stat().st_mtime_ns) for p in fixtures.iterdir() if p.is_file()}
