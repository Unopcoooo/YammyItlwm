#!/usr/bin/env python3
"""Install only the matched user-space helper; never edits EFI or starts AWDL.
Default is a read-only check. Requires the beta driver already loaded.
"""
import argparse, datetime, hashlib, json, os, plistlib, shutil, subprocess
from pathlib import Path

UUID = 'D6ED6749-6BCD-332E-871C-667166716DEE'
LABEL = 'org.unopco.awdl-receive-switch'
DEST = Path('/Library/PrivilegedHelperTools') / LABEL
PLIST = Path('/Library/LaunchDaemons') / (LABEL + '.plist')
FILES = ('bridge.py', 'policy.py', 'hashes.json', 'native-search-probe',
         'native-service-browse', 'native-airdrop-mode', LABEL + '.plist')
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()


def validate_bundle(root):
    root = root.resolve()
    m = json.loads((root / 'manifest.json').read_text())
    assert m['wifi_uuid'] == UUID and m['helper_revision'] == 'r26c'
    for n in FILES:
        p = root / 'helper' / n
        assert p.is_file() and not p.is_symlink()
        assert sha(p) == m['files']['helper/' + n], n
    h = json.loads((root / 'helper/hashes.json').read_text())
    assert set(h) == {'bridge.py', 'policy.py', 'native-search-probe',
                      'native-service-browse', 'native-airdrop-mode'}
    for n, expected in h.items():
        assert sha(root / 'helper' / n) == expected
    cfg = plistlib.loads((root / 'helper' / (LABEL + '.plist')).read_bytes())
    assert cfg['Label'] == LABEL and cfg['ProgramArguments'] == [
        '/usr/bin/python3', '-I', '-B', str(DEST / 'bridge.py')]
    assert cfg['WorkingDirectory'] == str(DEST) and cfg['RunAtLoad'] is True
    return root, m


def run(args, check=True):
    return subprocess.run(args, check=check, capture_output=True, text=True, timeout=15)


def require_idle():
    loaded = run(['/usr/bin/kmutil', 'showloaded', '--list-only']).stdout
    assert UUID in loaded, 'Reboot into the matched r26 driver before installing helper'
    if DEST.exists():
        assert not DEST.is_symlink() and DEST.stat().st_uid == 0
        h = json.loads((DEST / 'hashes.json').read_text())
        for n, expected in h.items():
            p = DEST / n
            assert p.is_file() and not p.is_symlink() and p.stat().st_uid == 0
            assert not p.stat().st_mode & 0o022 and sha(p) == expected
        q = run([str(DEST / 'native-search-probe'), '--status'])
        status = json.loads(q.stdout)
        assert status.get('WorkerPending', 0) == 0, 'Turn reception Off and wait for retirement'
        assert status.get('BackendEnabled', 0) == 0, 'Radio lease is still active'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('bundle', type=Path, help='Directory containing manifest.json and helper/')
    ap.add_argument('--apply', action='store_true', help='Install after checks; default is read-only')
    args = ap.parse_args()
    root, m = validate_bundle(args.bundle)
    require_idle()
    print('PASS: bundle hashes and matched loaded r26; no active AWDL lease')
    if not args.apply:
        print('Read-only check. Use sudo python3 install-helper.py BUNDLE --apply to install.')
        return
    assert os.geteuid() == 0, 'Run with sudo'
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    backup = Path('/Library/Application Support/Unopco-AWDL/Backups') / stamp
    backup.mkdir(parents=True, mode=0o700)
    stage = DEST.with_name(DEST.name + '.stage-' + stamp)
    assert not stage.exists()
    stage.mkdir(mode=0o755)
    for n in FILES:
        if n.endswith('.plist'):
            continue
        shutil.copyfile(root / 'helper' / n, stage / n)
        os.chown(stage / n, 0, 0)
        os.chmod(stage / n, 0o755 if n.startswith('native-') else 0o644)
        assert sha(stage / n) == m['files']['helper/' + n]
    old_dest = DEST.exists()
    old_plist = PLIST.exists()
    if old_dest:
        shutil.copytree(DEST, backup / 'helper')
    if old_plist:
        assert not PLIST.is_symlink() and PLIST.stat().st_uid == 0
        shutil.copyfile(PLIST, backup / PLIST.name)
    run(['/bin/launchctl', 'bootout', 'system/' + LABEL], check=False)
    try:
        if old_dest:
            os.rename(DEST, backup / 'previous-live-helper')
        os.rename(stage, DEST)
        shutil.copyfile(root / 'helper' / PLIST.name, PLIST)
        os.chown(PLIST, 0, 0)
        os.chmod(PLIST, 0o644)
        run(['/bin/launchctl', 'bootstrap', 'system', str(PLIST)])
        run(['/bin/launchctl', 'print', 'system/' + LABEL])
        for n, expected in json.loads((DEST / 'hashes.json').read_text()).items():
            assert sha(DEST / n) == expected
        os.sync()
    except BaseException:
        run(['/bin/launchctl', 'bootout', 'system/' + LABEL], check=False)
        if DEST.exists():
            os.rename(DEST, backup / 'failed-new-helper')
        if old_dest:
            os.rename(backup / 'previous-live-helper', DEST)
        if old_plist:
            shutil.copyfile(backup / PLIST.name, PLIST)
            run(['/bin/launchctl', 'bootstrap', 'system', str(PLIST)], check=False)
        elif PLIST.exists():
            PLIST.unlink()
        raise
    print('Installed. Backup:', backup)
    print('Startup does not arm AWDL. Manually switch Off -> Everyone when ready.')


if __name__ == '__main__':
    main()
