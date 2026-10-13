"""Observed Windows large-peek refusal must not truncate personal snapshots."""
import ast,hashlib,json,tempfile,unittest
from pathlib import Path
from types import SimpleNamespace

class ProgressSnapshotBatches(unittest.TestCase):
    def test_complete_snapshot_with_supported_command_size_and_personal_diff(self):
        tree=ast.parse((Path(__file__).resolve().parents[1]/'tools/scenario/run.py').read_text(encoding='utf-8'))
        fn=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='step_progress_snapshot')
        # Snapshot read boundary from the observed run:32 launches,64/128 deny.
        shared=(('programs',0x10,0x1C80),('story',0x1C90,0x260),('visited',0x22F8,0x98),('chests',0x23AC,0x34))
        personal=(('characters',0x24F0,0xE04),('inventory',0x3580,0x140),('munny',0x2440,4),('exp',0x36E0,4))
        memory=bytearray(bytes(range(256))*56);memory[0x2718]=100
        ns=dict(json=json,hashlib=hashlib,Context=object,PROGRESS_RANGES=shared,PERSONAL_RANGES=personal)
        exec(compile(ast.Module(body=[fn],type_ignores=[]),'actual snapshot collector','exec'),ns)
        calls=[]
        def read(*args,**kw):
            self.assertEqual(args[:2],('peek','--rva'));self.assertEqual(kw['pid'],123)
            specs=args[2].split(',');self.assertLessEqual(len(specs),32);calls.append(len(specs))
            sample={}
            for spec in specs:
                key,kind=spec.split(':');offset=int(key,16)-0x9A98B0;size=int(kind[1:])//8
                value=int.from_bytes(memory[offset:offset+size],'little');sample[key]=hex(value) if size==8 else value
            return {'samples':[sample]}
        with tempfile.TemporaryDirectory() as temp:
            ctx=SimpleNamespace(saved={},run_dir=Path(temp),artifacts=[],inst=lambda _:SimpleNamespace(pid=123))
            ns['step_progress_snapshot'](ctx,{'as':'before','instances':[1]},_read=read)
            for label,offset,length in shared+personal:
                part=ctx.saved['before']['instances']['1']['ranges'][label]
                self.assertEqual(bytes.fromhex(part['hex']),bytes(memory[offset:offset+length]))
                self.assertEqual(part['length'],length)
            self.assertEqual(sum(r[2] for r in shared),8108)
            memory[0x2718]=90
            ns['step_progress_snapshot'](ctx,{'as':'after','instances':[1],'compare':'before'},_read=read)
            diffs=ctx.saved['after']['instances']['1']['diffs']
            self.assertEqual(diffs['characters'],[{'saveOffset':0x2718,'before':100,'after':90}])
            self.assertTrue(all(not diffs[label] for label,_,_ in shared))
            self.assertGreater(len(calls),32)

if __name__=='__main__':unittest.main()
