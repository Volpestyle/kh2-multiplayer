"""Synthetic evidence-contract tests, using the unchanged native audit parser.

The fixture media is opaque test bytes, not live or decoded video evidence.
"""
import copy,hashlib,importlib.util,tempfile,unittest
from pathlib import Path
from . import evidence as e,health
from .profile import Refused
ROOT=Path(__file__).resolve().parents[3]
spec=importlib.util.spec_from_file_location('native_fixture',ROOT/'tests/test_trace_audit.py')
f=importlib.util.module_from_spec(spec);spec.loader.exec_module(f)
class Controls(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
        self.original={role:dict(pid=i+10,creationTicks=i+100,moduleBase='0x140000000',imageSha256='a'*64) for i,role in enumerate(('host','friend'))}
        self.scope=dict(connection=4294967298,generation=9,epoch=12,load=4,transition=3)
        self.log=f.hit_log().replace('000500060000123402010300','000500060000000100010000')
        def endpoint(role,hp,ns):
            original=self.original[role];actor=0x140100000 if role=='friend' else 0x150100000
            a=dict(actor=actor,head=actor,status=0x140500000,objentry=0x140300000,objectId=84,objectType=0,team=1,hp=hp,maxHp=40)
            c=dict(ok=True,session=original,identityBefore=original,identityAfter=original,before=a,after=a,readStartedNs=ns,readFinishedNs=ns+1,flags=[dict(name=n,actual=v,expected=v) for n,_,_,v in health.FLAGS])
            entity=dict(address=hex(actor),status=hex(a['status']),objectId=84,objectType=0,team=1,hp=hp,maxHp=40)
            sample=dict(ownedBefore=original,ownedAfter=original)
            for side in ('Before','After'):
                sample['entities'+side]=dict(ok=True,processId=original['pid'],actors=[entity])
                sample['state'+side]=dict(actors=dict(player=dict(hp=hp,downed=False)))
            if role=='host':
                world=dict(localSlot=0,connectionIds=[8589934594,4294967298,0],generation=9)
                puppet=dict(active=1,provenanceMatchesWorld=True,provenance=dict(ownerConnectionId=4294967298,localSlot=0,generation=9,localConnectionId=8589934594,hostConnectionId=8589934594),pose=dict(finite=True,worldId=5,roomId=6,hp=hp,maxHp=40))
                sample['avatarObserve']=dict(ok=True,readOnly=True,processId=original['pid'],samples=[dict(complete=True,identityStable=True,worldBefore=world,worldAfter=world,puppets=[dict(readAvailable=True,value=puppet)])])
            return dict(sample=sample,canonical=c)
        self.before={r:endpoint(r,24,1) for r in self.original};self.after={r:endpoint(r,16,3) for r in self.original}
        self.reviews={}
        for role in self.original:
            file=self.root/(role+'.mp4');file.write_bytes(b'opaque structural control fixture')
            self.reviews[role]=dict(path=file.name,sha256=e.sha(file),target=self.original[role],eventSequence=1,reviewer='offline-contract-test',gameOnly=True,allFramesPrivacyChecked=True,visibleEnemyImpact=True,coverageQualified=True,beforeFrame=0,impactFrame=1,afterFrame=2)
    def qualify(self):return e.qualify_incoming(self.log,self.before,self.after,self.original,self.scope,self.reviews,self.root,f.AUDIT)
    def test_full_typed_join_contract(self):self.assertEqual(self.qualify()['status'],'QUALIFIED_RECORDED_HIT');self.assertFalse(self.qualify()['acceptance'])
    def test_native_witness_and_scope_not_self_report(self):
        for changed in (self.log.replace('witness=1','witness=0'),self.log.replace('epoch=12','epoch=13'),self.log.replace('000500060000000100010000','000500070000000100010000')):
            old=self.log;self.log=changed
            with self.assertRaises(Refused):self.qualify()
            self.log=old
    def test_wrong_peer_hp_and_provenance(self):
        self.after['host']['sample']['avatarObserve']['samples'][0]['puppets'][0]['value']['pose']['hp']=24
        with self.assertRaises(Refused):self.qualify()
    def test_missing_changed_or_unreviewed_media(self):
        for key,value in [('coverageQualified',False),('visibleEnemyImpact',False),('allFramesPrivacyChecked',False),('sha256','b'*64),('eventSequence',2)]:
            old=self.reviews['host'][key];self.reviews['host'][key]=value
            with self.assertRaises(Refused):self.qualify()
            self.reviews['host'][key]=old
    def test_canonical_identity_and_deadline_refuse(self):
        self.after['friend']['canonical']['identityAfter']={}
        with self.assertRaises(Refused):self.qualify()
if __name__=='__main__':unittest.main()
