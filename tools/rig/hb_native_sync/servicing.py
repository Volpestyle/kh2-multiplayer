"""Read-only 50ms watchdog. Bounded sampled leases, never packet-service proof."""
import ctypes,json,struct,threading,time
import native_read,pause_scope


def header(raw):
    if len(raw)!=128:raise ValueError('world header length')
    u32=lambda n:struct.unpack_from('<I',raw,n)[0]
    u64=lambda n:struct.unpack_from('<Q',raw,n)[0]
    if (u32(0),u32(4))!=(0x42574B32,12):raise ValueError('world mapping ABI')
    return dict(slot=u32(8),generation=u32(20),roster=[u64(24+8*i) for i in range(3)],authority=u32(48),
                heartbeat=u32(52),delivery=u64(56),peerDelivery=[u64(64+8*i) for i in range(3)],writerPid=u32(116))


def check(row,previous=None,held=False):
    if row['gapMs']>250:raise ValueError('watchdog collection gap >250ms')
    for i,p in enumerate(row['peers']):
        h=p['header']
        if h['writerPid']!=p['runtimePid'] or h['slot']!=i or h['authority']!=2 or not h['generation'] or not h['delivery']:
            raise ValueError('writer/binding mismatch')
        if not all(h['roster'][:2]) or h['roster'][2]!=0 or ((row['tickMs']-h['heartbeat'])&0xFFFFFFFF)>5000:
            raise ValueError('roster/heartbeat unavailable or expired')
        if previous:
            before=previous['peers'][i]['header']
            if any(h[k]!=before[k] for k in ('writerPid','slot','generation','roster','authority','delivery','peerDelivery')):
                raise ValueError('world binding changed during interval')
    if row['peers'][0]['header']['roster']!=row['peers'][1]['header']['roster']:raise ValueError('cross-peer roster differs')
    c=row['client']
    if held and (not pause_scope.ordinary(c) or not c.get('bookendsStable') or not c.get('nativeScope')):
        raise ValueError('fixture-owned TT pause lost')


class PresentTracker:
    def __init__(self):self.counter=None;self.changedAt=None
    def sample(self,raw,now):
        if len(raw)!=56 or struct.unpack_from('<II',raw)!=(0x50433248,1):raise ValueError('capture mapping ABI mismatch')
        counter=struct.unpack_from('<I',raw,44)[0]
        if counter!=self.counter:self.counter=counter;self.changedAt=now
        age=now-self.changedAt
        if age<0 or age>2:raise ValueError('client Present stalled >2s')
        return counter,age


class Watchdog:
    def __init__(self,ctx,bases):
        self.ctx=ctx;self.bases=bases;self.stop_event=threading.Event();self.lock=threading.Lock()
        self.failure=None;self.latest=None;self.phase='held';self.started=time.monotonic();self.thread=None
    def run(self):
        maps=[];reader=None;capture=None;present=PresentTracker()
        try:
            maps=[native_read.Mapping(self.ctx.inst(i).pid) for i in (0,1)]
            reader=native_read.Reader(self.ctx.inst(1).pid,self.bases[1])
            capture=native_read.Mapping(self.ctx.inst(1).pid,'capture',56)
            collector=pause_scope.Collector(reader,self.ctx.inst(1).inject_log.read_bytes)
            previous=None;last=time.monotonic()
            with (self.ctx.run_dir/'worldmap_watchdog.jsonl').open('x',encoding='utf-8') as stream:
                while not self.stop_event.is_set():
                    now=time.monotonic();peers=[]
                    if now-self.started>300:raise ValueError('absolute300s calibration hold lifetime expired')
                    for i,m in enumerate(maps):
                        proc=next(p for n,p,_ in self.ctx.processes if n==f'runtime_{i}')
                        if proc.poll() is not None:raise ValueError('owned runtime exited')
                        first=m.read();second=m.read()
                        a,b=header(first),header(second)
                        if any(a[k]!=b[k] for k in a if k!='heartbeat'):raise ValueError('world header changed in bracket')
                        peers.append(dict(runtimePid=proc.pid,header=b))
                    with self.lock:phase=self.phase
                    client=collector.sample(phase=='held')
                    counter,present_age=present.sample(capture.read(),now)
                    row=dict(monotonic=now,gapMs=(now-last)*1000,tickMs=ctypes.windll.kernel32.GetTickCount()&0xFFFFFFFF,
                             phase=phase,peers=peers,client=client,presentCount=counter,presentAge=present_age)
                    stream.write(json.dumps(row)+'\n');stream.flush()
                    check(row,previous,phase=='held')
                    with self.lock:self.latest=row
                    previous=row;last=now;self.stop_event.wait(.05)
        except Exception as error:
            with self.lock:self.failure=str(error)
        finally:
            for m in maps:m.close()
            if reader:reader.close()
            if capture:capture.close()
    def start(self):
        self.thread=threading.Thread(target=self.run,name='worldmap-readonly-watchdog',daemon=True);self.thread.start()
        deadline=time.monotonic()+2
        while self.latest is None and self.failure is None and time.monotonic()<deadline:time.sleep(.02)
        self.require()
    def require(self):
        with self.lock:
            if self.failure or self.latest is None or time.monotonic()-self.latest['monotonic']>.25:
                raise ValueError('servicing/hold watchdog: '+str(self.failure or 'no fresh sample'))
            return self.latest
    def release(self):
        self.require()
        with self.lock:self.phase='releasing'
    def stop(self):
        self.stop_event.set()
        if self.thread:self.thread.join(2)
        if self.thread and self.thread.is_alive():raise ValueError('watchdog did not stop')
