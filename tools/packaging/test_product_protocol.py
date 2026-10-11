import copy,json,sys,tempfile,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'launcher'))
import product_protocol as p
import build_friend as builder
class Controls(unittest.TestCase):
    def setUp(self):
        self.rows={r:dict(path='bin/'+r,sha256='a'*64,protocol=17,sourceRevision='b'*40,buildId='reviewed') for r in p.ROLES}
        self.receipt=dict(protocol=17,avatarBridgeVersion=4,transport='steam',products={r:v for r,v in self.rows.items() if r!='cli'},cli=self.rows['cli'])
    def test_coherent17(self):self.assertEqual(p.validate_protocol_set(17,self.rows,{r['path']:r['sha256'] for r in self.rows.values()}),17)
    def test_each_mixed_role(self):
        for role in self.rows:
            rows=copy.deepcopy(self.rows);rows[role]['protocol']=16
            with self.assertRaisesRegex(ValueError,'Mixed protocols'):p.validate_protocol_set(17,rows)
    def test_old_missing_bool(self):
        for protocol in (10,16,True,None):
            with self.assertRaises(ValueError):p.validate_protocol_set(protocol,self.rows)
        with self.assertRaises(ValueError):p.validate_protocol_set(17,{})
    def test_mixed_cohort_and_hash(self):
        rows=copy.deepcopy(self.rows);rows['dll']['sourceRevision']='c'*40
        with self.assertRaisesRegex(ValueError,'Mixed source'):p.validate_protocol_set(17,rows)
        with self.assertRaisesRegex(ValueError,'hash disagrees'):p.validate_protocol_set(17,self.rows,{})
    def test_actual_builder(self):
        with tempfile.TemporaryDirectory() as d:
            file=Path(d)/'selection.json';file.write_text(json.dumps(self.receipt),encoding='utf-8-sig')
            self.assertEqual(builder.select_products('steam',file)['protocol'],17)
            self.receipt['products']['runtime']['protocol']=10;file.write_text(json.dumps(self.receipt))
            with self.assertRaisesRegex(ValueError,'Mixed protocols'):builder.select_products('steam',file)
    def test_legacy_default_refused(self):
        with self.assertRaises(ValueError):builder.select_products('enet',None)
if __name__=='__main__':unittest.main()
