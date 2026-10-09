"""Pure checks over retained native readbacks and unmodified log byte prefixes.

Always PENDING: this packet supplies observations, never world-map hold policy.
"""
import re
from native_log_format import _identities, ARRIVAL, LOAD, APPLY, _location

LOCATION_RVAS=(0x717008,0x717009,0x71700A,0x71700C,0x71700E,0x717010)
FLAGS={'eventState':0xB65210,'eventContext':0x2A11478,'frozen':0x2A171E8,'inField':0x9BA8D0,'openMenu':0x7435D0}


def native(row):
    response=row['peek']
    before=row['peekBefore']
    if (before.get('ok') is not True or before.get('processId')!=row['pid']
        or before.get('moduleBase')!=response.get('moduleBase') or before.get('samples')!=response.get('samples')):
        raise ValueError('native state changed within bracket')
    if response.get('ok') is not True or response.get('processId')!=row['pid']:
        raise ValueError('native read PID/result mismatch')
    base=response.get('moduleBase')
    base=int(base,0) if isinstance(base,str) else base
    if type(base) is not int or not 0x10000<=base<0x800000000000:
        raise ValueError('native module base unavailable')
    raw=response['samples'][0]
    location=[raw[f'0x{r:X}'] for r in LOCATION_RVAS]
    if any(type(v) is not int or not 0<=v<=(255 if i<3 else 65535) for i,v in enumerate(location)):
        raise ValueError('invalid full native location')
    flags={k:raw[f'0x{r:X}'] for k,r in FLAGS.items()}
    if isinstance(flags['eventContext'],str): flags['eventContext']=int(flags['eventContext'],0)
    if any(type(v) is not int for v in flags.values()):
        raise ValueError('invalid native safety flag types')
    safe=flags['eventState']==0 and flags['eventContext']==0 and flags['frozen']==0 and flags['inField']!=0 and flags['openMenu']==255
    # A world-map room is never field-ready, even if all field flags look idle.
    mode='world-map' if location[:2]==[15,0] else 'field' if safe else 'nonfield'
    return dict(location=location,flags=flags,mode=mode,safeField=safe and location[0] not in (15,255),moduleBase=base)


def evaluate(trace):
    problems=[]; checkpoints=[]; floors={}; identities={}; prefixes={}; baselines={}; modules={}
    saw_map=False; saw_hb=False; last_host_world=None
    def require(condition,message):
        if not condition: problems.append(message)
    try:
        require(trace.get('schema')==1,'unsupported trace schema')
        require(len(trace.get('samples',[]))>=3,'departure/map/landing samples missing')
        for ordinal,pair in enumerate(trace.get('samples',[])):
            require(len(pair)==2 and {r['instance'] for r in pair}=={0,1},'checkpoint must contain both peers')
            parsed={}; logs={}; current_ids={}
            for row in pair:
                i=row['instance']; label=f'checkpoint{ordinal}/peer{i}'
                require(type(row['startedNs']) is int and type(row['finishedNs']) is int and row['startedNs']>floors.get(i,-1)
                    and row['finishedNs']>=row['startedNs'],label+': read floor did not advance')
                floors[i]=row['finishedNs']
                n=parsed[i]=native(row)
                process=(row['pid'],n['moduleBase'])
                if i in modules: require(process==modules[i],label+': process/module replaced')
                modules[i]=process
                for kind in ('runtime','inject'):
                    data=bytes.fromhex(row[kind+'LogHex'])
                    require(data.endswith(b'\n'),label+': partial '+kind+' log line')
                    require(data.startswith(prefixes.get((i,kind),b'')),label+': '+kind+' log prefix changed')
                    prefixes[i,kind]=data
                    logs[i,kind]=data.decode('utf-8',errors='replace')
                candidates=_identities(logs[i,'runtime'])
                require(bool(candidates),label+': runtime identity missing')
                identity=candidates[-1] if candidates else {}
                current_ids[i]=identity
                for key,value in {'schema':1,'identityCurrent':1,'stringsComplete':1,'attachedPid':row['pid'],
                                  'slot':i,'worldSlot':i,'generationValid':1,'authority':2,'bridgeOpen':1,
                                  'pinPresent':1,'admitted':1,'transportConnected':1,'quarantine':0}.items():
                    require(identity.get(key)==value,label+': identity '+key+' mismatch')
                roster=[identity.get('roster'+str(j)) for j in range(3)]
                require(all(type(v) is int and v>0 for v in roster[:2]) and roster[2]==0,label+': exact two-peer roster absent')
                require(roster==[identity.get('worldRoster'+str(j)) for j in range(3)] and
                        identity.get('selfConnection')==roster[i] and identity.get('hostConnection')==roster[0],label+': roster/source mismatch')
                require(isinstance(identity.get('session'),str) and len(identity['session'])==32,label+': session missing')
                require(identity.get('pinSession')==identity.get('session') and
                        identity.get('pinHostConnection')==roster[0] and identity.get('pinSlot')==i,label+': original session pin mismatch')
                for key in ('generation','delivery','seq','observationMs'):
                    require(type(identity.get(key)) is int and identity[key]>0,label+': missing '+key)
                fixed=tuple(identity.get(k) for k in ('session','hostConnection','selfConnection','generation','delivery','roster0','roster1','roster2'))
                if i in identities:
                    require(fixed==identities[i][0],label+': session/source/roster changed')
                    require(identity.get('seq',0)>identities[i][1] and identity.get('observationMs',0)>identities[i][2],label+': runtime identity did not advance')
                identities[i]=(fixed,identity.get('seq',0),identity.get('observationMs',0))
                arrivals=list(ARRIVAL.finditer(logs[i,'inject']))
                loads=list(LOAD.finditer(logs[i,'inject']))
                if ordinal==0:
                    require(n['location'][0]==2 and n['safeField'],label+': baseline is not safe TT')
                    require(bool(arrivals and loads),label+': baseline completed arrival/load missing')
                    if arrivals and loads:
                        require(_location(arrivals[-1])==n['location']==_location(loads[-1]),label+': baseline location not bound to completion')
                        baselines[i]=(int(arrivals[-1]['epoch']),int(loads[-1]['serial']),len(logs[i,'inject']))
                if i in baselines:
                    new_native=logs[i,'inject'][baselines[i][2]:]
                    if i==0:
                        require(re.search(r'Warp: [^\r\n]* -> 04/',new_native) is None,label+': direct host HB warp is excluded')
                    else:
                        require('[warp] client native exit blocked' not in new_native,label+': independent client exit attempted')
                if n['location'][:2]==[4,10] and n['safeField']:
                    require(bool(arrivals and loads and i in baselines),label+': HB arrival/load missing')
                    if arrivals and loads and i in baselines:
                        require(_location(arrivals[-1])==n['location']==_location(loads[-1]),label+': HB full tuple does not bind load')
                        require(int(arrivals[-1]['epoch'])>baselines[i][0] and int(loads[-1]['serial'])>baselines[i][1],label+': stale HB completion')
                        if i==1:
                            applies=list(APPLY.finditer(logs[i,'inject']))
                            require(any(baselines[i][2]<=a.start()<a.end()<loads[-1].start()
                                and a['before']==a['after'] for a in applies),label+': fresh progress apply/personal preservation before load missing')
            require(current_ids[0].get('session')==current_ids[1].get('session') and
                    current_ids[0].get('hostConnection')==current_ids[1].get('hostConnection'),f'checkpoint{ordinal}: peers differ in session/host')
            host,client=parsed[0],parsed[1]
            if host['mode']=='world-map':
                saw_map=True
                require(client['location'][0]==2,f'checkpoint{ordinal}: client entered map/another world instead of TT wait')
            if host['location'][:2]==client['location'][:2]==[4,10] and host['safeField'] and client['safeField']:
                require(saw_map,'direct TT-to-HB arrival is not a world-map observation')
                require(host['location']==client['location'],'HB peers disagree on full tuple')
                h=list(ARRIVAL.finditer(logs[0,'inject'])); c=list(ARRIVAL.finditer(logs[1,'inject']))
                require(bool(h and c) and h[-1]['epoch']==c[-1]['epoch'],'HB peer epochs disagree')
                saw_hb=True
            if saw_hb and host['location'][0]!=4: require(False,'host left HB after candidate landing')
            checkpoints.append(dict(index=ordinal,peers=parsed))
        require(saw_map,'native host world0F/room00 was not observed')
        require(saw_hb,'both peers never reached the safe authored HB room10 successor')
    except (KeyError,TypeError,ValueError,IndexError) as error:
        problems.append('incomplete/malformed trace: '+str(error))
    return dict(status='PENDING',observationChecksPassed=not problems,problems=problems,
        sawHostWorldMap=saw_map,sawBothSafeHB=saw_hb,checkpoints=checkpoints,
        acceptance=False,scope='sampled boundary only; hold ACK/source ordering, all masked bytes and fresh input still require qualification')
