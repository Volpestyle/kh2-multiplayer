"""Bind the canonical runner to sealed packet-local launch products."""
import hashlib,json
from pathlib import Path

def install(runner,packet):
 config=json.loads((packet/'execution.json').read_text(encoding='utf-8'))
 products=config.get('products')
 if products is None:raise ValueError('packet-local products required')
 paths={}
 for key in ('kh2ctl','runtime','server','avatarctl','dll'):
  row=products[key];p=(packet/row['path']).resolve()
  if not p.is_relative_to((packet/'products').resolve()) or hashlib.sha256(p.read_bytes()).hexdigest()!=row['sha256']:raise ValueError('packet product changed: '+key)
  paths[key]=p
 for key,attr in [('kh2ctl','KH2CTL'),('runtime','RUNTIME'),('server','SERVER'),('avatarctl','AVATARCTL')]:setattr(runner,attr,paths[key])
 original=runner.kh2ctl
 def call(command,*args,**kwargs):
  if command=='launch':
   if '--dll' in args:raise ValueError('DLL override forbidden')
   args=('--dll',str(paths['dll']),*args)
  return original(command,*args,**kwargs)
 runner.kh2ctl=call
 return paths
