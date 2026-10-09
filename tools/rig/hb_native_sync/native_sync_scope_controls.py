"""Retained HB-sync02 baseline replay and hostile lifecycle variants; no HB PASS."""
import native_sync
def run(packet):
    root=packet/'history/hbsync02';host=(root/'native_sync_inject_0.log').read_bytes();friend=(root/'native_sync_inject_1.log').read_bytes();checks=[]
    def passed(name):checks.append(dict(name=name,status='PASS',scope='retained TT baseline only; HB request/follower untested'))
    h=native_sync.scope(host,'host');f=native_sync.scope(friend,'client')
    assert [h[k] for k in ('loadSerial','transitionSerial','epoch')]==[2,1,1] and h['location']==native_sync.START and h['bootstrapResetCount']==3
    passed('actual02 host TT02 load2/trans1 reset-before-firstarrival1 admission')
    assert [f[k] for k in ('loadSerial','transitionSerial','epoch')]==[3,2,1] and f['location']==native_sync.START and f['bootstrapResetCount']==0
    passed('actual02 friend completed load3/trans2/epoch1 stays unchanged')
    assert native_sync.scope(host+b'unrelated diagnostic only\n','host')==h
    passed('nonlifecycle diagnostic does not change immutable native scope')
    reset=b'[enemysync] session reset: host epoch and pending target cleared\n'
    arrival=b'[enemysync] host arrived epoch=1 room=02/02 door=0 map=4 btl=0 evt=0'
    cases=[('reset after arrival',host+reset,'host'),('reset and reannounce sameepoch',host+reset+arrival+b'\n','host'),
        ('pending queue after arrival',host+b'[warp] client queued epoch=2 target=02/02 door=0 map=4 btl=0 evt=0\n','host'),
        ('new load without arrival',host+b'[warp] load complete serial=3 transition=2 room=02/02 door=0 map=4 btl=0 evt=0\n','host'),
        ('reset bootstrap exception notepoch2',host.replace(arrival,arrival.replace(b'epoch=1',b'epoch=2')),'host'),
        ('reset bootstrap exception notHB',host.replace(b'room=02/02 door=0 map=4 btl=0 evt=0',b'room=04/00 door=0 map=4 btl=0 evt=0'),'host'),
        ('wrong arrival role',host.replace(arrival,arrival.replace(b'host arrived',b'client arrived')),'host'),
        ('wrong arrival fulltuple',host.replace(arrival,arrival.replace(b'map=4',b'map=5')),'host'),
        ('missing arrival',host.replace(arrival,b'removed arrival'),'host'),
        ('friend reset after arrival',friend+reset,'client')]
    for name,data,role in cases:
        try:native_sync.scope(data,role)
        except ValueError:passed('native scope refuses '+name)
        else:raise AssertionError('scope admitted '+name)
    return checks
