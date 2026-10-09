"""Copy a reviewed internal media capability into a fresh package ZIP only."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile


def digest(data):return hashlib.sha256(data).hexdigest()


def build(source, clip_cli, encoder, module, output):
    source,clip_cli,encoder,module,output=map(Path,(source,clip_cli,encoder,module,output))
    if output.exists():raise ValueError('fresh output ZIP required')
    payloads={'bin/kh2ctl_clip.exe':clip_cli.read_bytes(),'bin/ffmpeg.exe':encoder.read_bytes()}
    for name in ('__init__.py','adapter.py','metrics.py','pc2-owned-clip-v1.schema.json',
                 'pc2-owned-clip-failure-v1.schema.json','pc2-decoded-frames-v1.schema.json'):
        payloads['tools/pc2_clip/'+name]=(module/name).read_bytes()
    roles={role:dict(packagePath=name,sha256=digest(payloads[name])) for role,name in
           {'clipCli':'bin/kh2ctl_clip.exe','encoder':'bin/ffmpeg.exe','verifier':'bin/ffmpeg.exe',
            'metrics':'tools/pc2_clip/metrics.py'}.items()}
    media=dict(schema='pc2-internal-media-product-v1',products=roles,
               requestedSeconds=3,requestedFps=30,decodedFrames=90,acceptance=False)
    payloads['pc2-media-products.json']=(json.dumps(media,indent=2)+'\n').encode('utf-8')
    with zipfile.ZipFile(source) as old:
        names=old.namelist()
        if len(names)!=len(set(names)):raise ValueError('duplicate source ZIP member')
        for name in names:
            p=PurePosixPath(name)
            if p.is_absolute() or '..' in p.parts or '\\' in name:raise ValueError('unsafe source ZIP member')
        if old.read('KH2-Co-op/KH2COOP-PACKAGE').strip()!=b'kh2coop-friend-package-v1':raise ValueError('package marker mismatch')
        manifest_name='KH2-Co-op/package.json';manifest=json.loads(old.read(manifest_name))
        for name,expected in manifest['files'].items():
            if digest(old.read('KH2-Co-op/'+name))!=expected:raise ValueError('original product pin mismatch: '+name)
        for name,data in payloads.items():
            if name in manifest['files'] or 'KH2-Co-op/'+name in names:raise ValueError('refusing existing package member: '+name)
            manifest['files'][name]=digest(data)
        if 'internalRigMedia' in manifest:raise ValueError('existing media declaration')
        manifest['internalRigMedia']=media
        with zipfile.ZipFile(output,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as new:
            for info in old.infolist():
                if info.filename!=manifest_name:new.writestr(info,old.read(info))
            new.writestr(manifest_name,json.dumps(manifest,indent=2)+'\n')
            for name,data in payloads.items():new.writestr('KH2-Co-op/'+name,data)
    return media


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('source','clip-cli','encoder','module','output'):parser.add_argument('--'+name,required=True,type=Path)
    args=parser.parse_args()
    print(json.dumps(build(args.source,args.clip_cli,args.encoder,args.module,args.output),indent=2))
