"""Bracketed native pause state and existing native lifecycle log receipts.

Matches the canonical census scope's completed-load/arrival log source.
These are bounded read observations, not atomicity or packet-service proof.
"""
import hashlib
from native_log_format import ARRIVAL,LOAD,LIFECYCLE,_location

START=[2,2,0,4,0,0]
def native(reader):
 return dict(location=[reader.value(r,f) for r,f in ((0x717008,'B'),(0x717009,'B'),(0x71700A,'B'),(0x71700C,'H'),(0x71700E,'H'),(0x717010,'H'))],
             menu=reader.value(0x7435D0,'B'),event=reader.value(0xB65210,'i'),context=reader.value(0x2A11478,'Q'),
             frozen=reader.value(0x2A171E8,'I'),inField=reader.value(0x9BA8D0,'B'))

def ordinary(c):
 return c.get('location')==START and c.get('menu')==10 and c.get('event')==0 and c.get('context')==0 and c.get('frozen')==0 and c.get('inField')==1

def scope(data):
 if not data or not data.endswith(b'\n'):raise ValueError('native lifecycle log incomplete')
 text=data.decode('utf-8',errors='strict');loads=list(LOAD.finditer(text));arrivals=list(ARRIVAL.finditer(text));edges=list(LIFECYCLE.finditer(text))
 if not loads or not arrivals or not edges:raise ValueError('native completed load/arrival scope missing')
 load,arrival=loads[-1],arrivals[-1]
 if edges[-1].start()!=load.start() or arrival.start()<load.end() or arrival['role']!='client':raise ValueError('native lifecycle is not completed client arrival')
 if _location(load)!=START or _location(arrival)!=START:raise ValueError('native lifecycle location not START')
 values=(int(load['serial']),int(load['transition']),int(arrival['epoch']))
 if min(values)<=0:raise ValueError('native load/transition/arrival identity incomplete')
 # Include ordered lifecycle/arrival records, so a same-identity replay or a
 # reset/queued transition cannot silently return to the old admission.
 lines=[line for line in text.splitlines() if LIFECYCLE.search(line) or ARRIVAL.search(line)]
 return dict(loadSerial=values[0],transitionSerial=values[1],epoch=values[2],location=list(START),
             lifecycleSha256=hashlib.sha256(('\n'.join(lines)+'\n').encode()).hexdigest(),records=len(lines))

def valid_scope(s):
 return (type(s) is dict and all(type(s.get(k)) is int and s[k]>0 for k in ('loadSerial','transitionSerial','epoch','records'))
         and s.get('location')==START and type(s.get('lifecycleSha256')) is str and len(s['lifecycleSha256'])==64)

class Collector:
 def __init__(self,reader,read_log):self.reader=reader;self.read_log=read_log;self.anchor=None;self.failure=None
 def sample(self,held=True):
  if self.failure:raise ValueError('native pause collector terminal: '+self.failure)
  try:return self.collect(held)
  except Exception as error:
   self.failure=str(error);raise
 def collect(self,held=True):
  before=native(self.reader)
  if not held:return before
  first=scope(self.read_log());copied=native(self.reader);last=scope(self.read_log());after=native(self.reader)
  if before!=copied or copied!=after:raise ValueError('native pause state torn across bookends')
  if not ordinary(copied):raise ValueError('full native START pause state lost')
  if first!=last:raise ValueError('native lifecycle changed across copied state')
  if self.anchor is None:self.anchor=first.copy()
  if first!=self.anchor:raise ValueError('native pause admission scope replaced; never reanchor')
  return dict(copied,nativeScope=first,bookendsStable=True)
