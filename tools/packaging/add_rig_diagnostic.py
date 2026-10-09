"""Add an internal diagnostic CLI to a NEW rig ZIP; preserve friend inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile


def build(source, diagnostic, output):
    source, diagnostic, output = map(Path, (source, diagnostic, output))
    payload = diagnostic.read_bytes()
    digest = hashlib.sha256(payload).hexdigest()
    with zipfile.ZipFile(source) as old:
        names = old.namelist()
        assert len(names) == len(set(names)), 'Duplicate ZIP entries'
        marker = 'KH2-Co-op/KH2COOP-PACKAGE'
        assert old.read(marker).strip() == b'kh2coop-friend-package-v1'
        manifest_name = 'KH2-Co-op/package.json'
        manifest = json.loads(old.read(manifest_name))
        destination = 'bin/kh2ctl_diagnostic.exe'
        assert destination not in manifest['files'] and 'KH2-Co-op/'+destination not in names
        for name, expected in manifest['files'].items():
            assert hashlib.sha256(old.read('KH2-Co-op/'+name)).hexdigest() == expected, name
        manifest['files'][destination] = digest
        manifest['internalRigDiagnostic'] = {'path': destination, 'sha256': digest}
        with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as new:
            for info in old.infolist():
                if info.filename != manifest_name:
                    new.writestr(info, old.read(info))
            new.writestr(manifest_name, json.dumps(manifest, indent=2)+'\n')
            new.writestr('KH2-Co-op/'+destination, payload)
    return {'path': destination, 'sha256': digest}


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source', required=True, type=Path)
    ap.add_argument('--diagnostic', required=True, type=Path)
    ap.add_argument('--output', required=True, type=Path)
    args = ap.parse_args()
    print(json.dumps(build(args.source, args.diagnostic, args.output)))
