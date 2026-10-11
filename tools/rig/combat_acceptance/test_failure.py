"""Actual canonical refusal with in-memory RPM leaves, lossless JSON terminal."""
import copy,json,unittest
from unittest.mock import patch
from . import failure,profile,health
from .test_profile import Adapter
class Controls(unittest.TestCase):
    def test_actual_guard_full_refusal_survives_local_wrapper(self):
        a=Adapter();saved=[];a.retain=lambda name,row:saved.append((name,json.loads(json.dumps(row))))
        class RAM:
            base=0x100000
            def __init__(self):
                actor=0x200000;obj=0x300000;status=0x400000
                self.values={self.base+health.NATIVE_PLAYER:actor,self.base+health.ACTIVE_HEAD:actor,actor+0x918:obj,actor+0x5C0:status,obj:84,obj+4:0,actor+0x4DC:1,status:8,status+4:24,self.base+0x2A23598:16,self.base+0x2A2359C:24}
                self.values.update({self.base+int(rva,16):v for _,rva,_,v in health.FLAGS})
            def value(self,address,fmt):return self.values[address]
        a.ram=RAM()
        with patch.object(profile.Profile,'run',profile.Profile.canonical):
            with self.assertRaises(health.HealthRefusal) as caught:failure.run_with_evidence(a)
        self.assertEqual([name for name,row in saved],['canonical-health-refusal','source-terminal-failure'])
        for name,row in saved:
            self.assertEqual(row['evidence'],caught.exception.evidence)
            self.assertEqual(row['evidence']['after']['hp'],8)
            self.assertEqual(row['evidence']['cachedSlot']['hp'],16)
            self.assertIn('flags',row['evidence']);self.assertIn('identityAfter',row['evidence'])
        self.assertFalse(any(kind=='spawn' for kind,_ in a.calls))
    def test_nested_cause_evidence_is_not_a_string_or_mutable_alias(self):
        source={'before':{'hp':16},'after':{'hp':8},'disagreements':['native hit']}
        try:
            try:raise health.HealthRefusal('straddle',source)
            except health.HealthRefusal as inner:raise RuntimeError('service refused') from inner
        except RuntimeError as outer:record=failure.failure_record(outer)
        self.assertEqual(json.loads(json.dumps(record))['cause']['evidence'],source)
        source['after']['hp']=0
        self.assertEqual(record['cause']['evidence']['after']['hp'],8)
if __name__=='__main__':unittest.main()
