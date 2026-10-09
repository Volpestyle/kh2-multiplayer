"""Mocked packet-product routing and isolated live-build replacement controls."""
import json,hashlib
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import packet_products

def run():
 rows=[]
 with TemporaryDirectory(prefix='story-product-controls-') as temp:
  p=Path(temp);(p/'products').mkdir();live=p/'live-build';live.mkdir();spec={};calls=[]
  for key in ('kh2ctl','runtime','server','avatarctl','dll'):
   f=p/'products'/(key+'.bin');f.write_bytes(key.encode());(live/f.name).write_bytes(key.encode())
   spec[key]=dict(path='products/'+f.name,sha256=hashlib.sha256(f.read_bytes()).hexdigest())
  (p/'execution.json').write_text(json.dumps(dict(products=spec)),encoding='utf-8')
  for f in live.iterdir():f.write_bytes(b'rebuilt-live-output')
  r=SimpleNamespace(kh2ctl=lambda *a,**kw:calls.append((a,kw)) or dict(ok=True));paths=packet_products.install(r,p)
  assert r.KH2CTL==paths['kh2ctl'] and r.RUNTIME==paths['runtime'] and r.SERVER==paths['server'] and r.AVATARCTL==paths['avatarctl']
  rows.append(dict(name='all canonical product globals resolve to packet copies despite live-build replacement',status='PASS'))
  r.kh2ctl('launch','--init-timeout-ms','10');assert calls[-1][0]==('launch','--dll',str(paths['dll']),'--init-timeout-ms','10')
  rows.append(dict(name='launch explicitly selects packet DLL',status='PASS'))
  r.kh2ctl('peek','--rva','0x1:u8',pid=123);assert calls[-1][0]==('peek','--rva','0x1:u8')
  rows.append(dict(name='nonlaunch CLI arguments preserved',status='PASS'))
  try:r.kh2ctl('launch','--dll','other.dll')
  except ValueError:pass
  else:raise AssertionError('DLL override')
  rows.append(dict(name='secondary DLL override refused',status='PASS'))
  paths['dll'].write_bytes(b'corrupt')
  try:packet_products.install(SimpleNamespace(kh2ctl=lambda *a:None),p)
  except ValueError:pass
  else:raise AssertionError('corrupt packet DLL')
  rows.append(dict(name='corrupt packet DLL refuses before launch',status='PASS'))
 return rows
