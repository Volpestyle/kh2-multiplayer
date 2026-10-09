"""Exact spent HBsync03 replay plus hostile cause-history/CRLF controls.

The original scenario stays FAIL; a corrected offline oracle is not a new run.
"""
import copy,hashlib,importlib.util,json,re
import native_sync_proof as proof
import test_native_sync_proof

def evidence(packet):
 root=packet/'history/hbsync03'
 result=json.loads((root/'native_sync_result.json').read_text())
 trace=json.loads((root/'native_sync_trace.json').read_text())
 identity=copy.deepcopy(result['identity']);identity['owned']={int(k):v for k,v in identity['owned'].items()}
 return dict(identity=identity,baselines=result['baselines'],hostLog=(root/'native_sync_inject_0.log').read_bytes(),friendLog=(root/'native_sync_inject_1.log').read_bytes(),runtimeLogs={i:(root/f'native_sync_runtime_{i}.log').read_bytes() for i in (0,1)},request=[4,0,0,65535,65535,1],current={i:trace[-1]['current'][i] for i in (0,1)})

def reprefix(e,data):
 # Semantic mutants intentionally replace the supplied historical prefix AND
 # its synthetic authentication receipt; byte corruption tests do NOT rehash.
 base=e['baselines']['friend'];old=base['offset'];suffix=e['friendLog'][old:]
 e['friendLog']=data+suffix;base.update(offset=len(data),prefixSha256=hashlib.sha256(data).hexdigest())

def run(packet):
 rows=[]
 def check(name,ok):
  assert ok,name;rows.append(dict(name=name,status='PASS',scope='offline retained replay/mutant; original HBsync03 remains FAIL'))
 good=evidence(packet);original=json.loads((packet/'history/hbsync03/native_sync_result.json').read_text())
 check('spent HBsync03 remains FAIL no paired result',original['status']=='FAIL' and original['pairedFreshHBArrival'] is False)
 spec=importlib.util.spec_from_file_location('historical_source4_proof',packet/'history/hbsync03/source4_native_sync_proof.py');old=importlib.util.module_from_spec(spec);spec.loader.exec_module(old)
 try:old.verify(copy.deepcopy(good))
 except ValueError as error:check('exact historical source4 refuses retained five-row suffix',str(error)=='complete sole ordinary cause sequence required')
 else:raise AssertionError('historical source4 unexpectedly accepted')
 result=proof.verify(good);ret=result['friendBaselineRetirement']
 check('unmodified CRLF retained logs replay under source5 without raw omission',result['location']==[4,10,50,0,0,22] and result['epoch']==2 and result['hostSource']==10 and ret is not None)
 check('proof retains old complete prefix and sole superseded row separately',len(ret['baselineCause'])==4 and 'seq=7 ' in ret['row'][1] and all(f'seq={n} ' in row[1] for n,row in zip(range(8,12),result['friendCause'])))
 # LF/CRLF tokens must agree; byte-oriented authentication remains independent.
 for crlf in (False,True):
  e=test_native_sync_proof.synthetic()
  if crlf:
   for i,label in enumerate(('host','friend')):
    key='hostLog' if i==0 else 'friendLog';base=e['baselines'][label];prefix=e[key][:base['offset']].replace(b'\n',b'\r\n');suffix=e[key][base['offset']:].replace(b'\n',b'\r\n')
    e[key]=prefix+suffix;base.update(offset=len(prefix),prefixSha256=hashlib.sha256(prefix).hexdigest())
    e['runtimeLogs'][i]=e['runtimeLogs'][i].replace(b'\n',b'\r\n');base.update(runtimeOffset=len(e['runtimeLogs'][i]),runtimePrefixSha256=hashlib.sha256(e['runtimeLogs'][i]).hexdigest())
  p=proof.verify(e);check('synthetic '+('CRLF' if crlf else 'LF')+' causal/personal integrity token replay',p['location']==[4,0,0,0,0,1] and p['friendBaselineRetirement'] is None)
 off=good['baselines']['friend']['offset'];prefix=good['friendLog'][:off];suffix=good['friendLog'][off:]
 retire=next(line for line in suffix.splitlines(keepends=True) if b'event=superseded ' in line)
 queue=next(line for line in suffix.splitlines(keepends=True) if b'event=queue ' in line and line.startswith(b'[load-cause]'))
 # Per-row keys, not substring replacement of unrelated complete logs.
 def prefix_change(e,event,key,value):
  lines=prefix.splitlines(keepends=True)
  for n,line in enumerate(lines):
   if line.startswith(b'[load-cause] ') and ('event='+event+' ').encode() in line and b'cause=3 ' in line:
    lines[n]=re.sub((r'\b'+key+r'=[^ \r\n]+').encode(),(key+'='+value).encode(),line);break
  else:raise AssertionError('baseline row absent')
  reprefix(e,b''.join(lines))
 mutants={
  'raw-prefix-byte-change':lambda e:e.update(friendLog=b'X'+e['friendLog'][1:]),
  'raw-prefix-offset-middle-line':lambda e:e['baselines']['friend'].update(offset=off-2),
  'missing-completed-old-arrival':lambda e:reprefix(e,b''.join(line for line in prefix.splitlines(keepends=True) if not (line.startswith(b'[load-cause] ') and b'event=arrival ' in line))),
  'missing-old-native-load':lambda e:reprefix(e,b''.join(line for line in prefix.splitlines(keepends=True) if not line.startswith(b'[warp] load complete serial=3 '))),
  'missing-old-product-arrival':lambda e:reprefix(e,b''.join(line for line in prefix.splitlines(keepends=True) if not line.startswith(b'[enemysync] client arrived epoch=1 '))),
  'missing-old-progress-apply':lambda e:reprefix(e,b''.join(line for line in prefix.splitlines(keepends=True) if not line.startswith(b'[progresssync] apply '))),
  'old-personal-change':lambda e:reprefix(e,prefix.replace(b'personal_after=124AA108',b'personal_after=124AA109')),
  'old-reset-after-completion':lambda e:reprefix(e,prefix+b'[enemysync] session reset: host epoch and pending target cleared\r\n'),
  'old-native-pending-after-completion':lambda e:reprefix(e,prefix+b'[warp] client queued epoch=2 target=04/0A door=50 map=0 btl=0 evt=22\r\n'),
  'retirement-of-new-cause':lambda e:e.update(friendLog=prefix+suffix.replace(b'event=superseded cause=3 ',b'event=superseded cause=8 ',1)),
  'duplicate-retirement':lambda e:e.update(friendLog=prefix+retire+suffix),
  'retirement-after-new-queue':lambda e:e.update(friendLog=prefix+suffix.replace(retire,b'',1).replace(queue,queue+retire,1)),
  'new-cause-superseded':lambda e:e.update(friendLog=e['friendLog']+queue.replace(b'event=queue ',b'event=superseded ')),
  'old-retirement-sequence-gap':lambda e:e.update(friendLog=prefix+suffix.replace(retire,retire.replace(b'seq=7 ',b'seq=6 ',1),1)),
  'new-queue-sequence-gap':lambda e:e.update(friendLog=prefix+suffix.replace(queue,queue.replace(b'seq=8 ',b'seq=9 ',1),1)),
  'old-retirement-foreign-session':lambda e:e.update(friendLog=prefix+suffix.replace(retire,retire.replace(e['identity']['session'].encode(),b'a'*32),1)),
  'old-retirement-partial':lambda e:e.update(friendLog=prefix+suffix.replace(retire,retire.replace(b'complete=1 ',b'complete=0 '),1)),
  'CR-embedded-personal-token':lambda e:e.update(friendLog=e['friendLog'].replace(b'personal_unchanged=1\r\n',b'personal_unchanged=1\rjunk\r\n')),
  'CR-duplicated-line-ending':lambda e:e.update(friendLog=e['friendLog'].replace(b'personal_unchanged=1\r\n',b'personal_unchanged=1\r\r\n')),
  'duplicate-personal-token':lambda e:e.update(friendLog=e['friendLog'].replace(b'personal_unchanged=1\r\n',b'personal_unchanged=0 personal_unchanged=1\r\n')),
  'new-runtime-session-replacement':lambda e:e['runtimeLogs'].update({1:e['runtimeLogs'][1]+e['runtimeLogs'][1].replace(e['identity']['session'].encode(),b'a'*32)}),
  'new-cause-missing-personal-hashes':lambda e:e.update(friendLog=prefix+suffix.replace(b' personal_before=124AA108 personal_after=124AA108',b'')),
  'new-unsafe-raw-bookend':lambda e:e['current'][1]['rawReads'][1]['native'].update(menu=7),
 }
 for key,value in dict(cause='9',seq='9',generation='3',delivery='2',hostSource='10',session='a'*32,host='9',target='9',targetDelivery='2',loadBefore='1',load='4',transition='3',issueTransition='3',epoch='2',targetRoom='04/0A',room='04/0A',available='0',complete='0',origin='resync_bootstrap',request='1',phase='0',cut='1',snapshot='bad').items():
  mutants['old-completion-'+key]=lambda e,key=key,value=value:prefix_change(e,'arrival',key,value)
 for name,mutate in mutants.items():
  e=copy.deepcopy(good);mutate(e)
  try:proof.verify(e)
  except proof.Refused:check('retained history refuses '+name,True)
  else:raise AssertionError(name+' unexpectedly admitted')
 return rows
