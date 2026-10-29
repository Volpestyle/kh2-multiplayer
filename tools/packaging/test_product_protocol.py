"""Offline source/package protocol and coherent product receipt boundaries."""
import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'launcher'))
import product_protocol as p
import build_friend as builder


class Controls(unittest.TestCase):
    def setUp(self):
        self.protocol = p.source_protocol(builder.ROOT)
        self.rows = {r:dict(path='bin/'+r, sha256='a'*64, protocol=self.protocol,
                           sourceRevision='b'*40, buildId='reviewed') for r in p.ROLES}
        self.receipt = dict(protocol=self.protocol, avatarBridgeVersion=p.AVATAR_BRIDGE_VERSION,
                            transport='steam', products={r:v for r,v in self.rows.items() if r!='cli'},
                            cli=self.rows['cli'])

    def test_current_source_accepted(self):
        self.assertEqual(p.validate_protocol_set(self.protocol, self.rows,
                         {r['path']:r['sha256'] for r in self.rows.values()}), self.protocol)

    def test_each_mixed_and_missing_role(self):
        for role in self.rows:
            with self.subTest(role=role):
                rows=copy.deepcopy(self.rows);rows[role]['protocol']=self.protocol-1
                with self.assertRaisesRegex(ValueError, 'Mixed protocols'):
                    p.validate_protocol_set(self.protocol, rows)
                del rows[role]
                with self.assertRaisesRegex(ValueError, 'roles missing'):
                    p.validate_protocol_set(self.protocol, rows)

    def test_stale_coherent_missing_and_bool_refused(self):
        for protocol in (self.protocol-1, True, None):
            rows=copy.deepcopy(self.rows)
            for row in rows.values():row['protocol']=protocol
            with self.subTest(protocol=protocol), self.assertRaises(ValueError):
                p.validate_protocol_set(protocol, rows)

    def test_mixed_cohort_and_hash(self):
        for field,value in (('sourceRevision','c'*40), ('buildId','another-build')):
            rows=copy.deepcopy(self.rows);rows['dll'][field]=value
            with self.assertRaisesRegex(ValueError, 'Mixed source'):
                p.validate_protocol_set(self.protocol, rows)
        with self.assertRaisesRegex(ValueError, 'hash disagrees'):
            p.validate_protocol_set(self.protocol, self.rows, {})

    def test_actual_builder_selection(self):
        with tempfile.TemporaryDirectory() as d:
            file=Path(d)/'selection.json'
            file.write_text(json.dumps(self.receipt), encoding='utf-8-sig')
            self.assertEqual(builder.select_products('steam', file)['protocol'], self.protocol)
            self.receipt['products']['runtime']['protocol']=self.protocol-1
            file.write_text(json.dumps(self.receipt))
            with self.assertRaisesRegex(ValueError, 'Mixed protocols'):
                builder.select_products('steam', file)

    def test_stale_selection_refused_and_current_pin_accepted(self):
        with tempfile.TemporaryDirectory() as d:
            receipt=copy.deepcopy(self.receipt);receipt['protocol']=self.protocol-1
            for row in [*receipt['products'].values(),receipt['cli']]:row['protocol']=self.protocol-1
            file=Path(d)/'stale.json';file.write_text(json.dumps(receipt))
            with self.assertRaisesRegex(ValueError, 'protocol differs from source'):
                builder.select_products('steam', file)
        selected=builder.select_products('steam',builder.ROOT/'tools/packaging/friend-products.json')
        self.assertEqual(selected['protocol'],self.protocol)

    def test_builder_reads_header_at_build_without_a_new_launcher_constant(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);header=root/p.PROTOCOL_HEADER;header.parent.mkdir(parents=True)
            header.write_text('inline constexpr std::uint16_t PROTOCOL_VERSION = 123;\n')
            (header.parent/'AvatarBridge.hpp').write_text(
                f'AVATAR_BRIDGE_VERSION = {p.AVATAR_BRIDGE_VERSION};')
            receipt=copy.deepcopy(self.receipt);receipt['protocol']=123
            for row in [*receipt['products'].values(),receipt['cli']]:row['protocol']=123
            file=root/'selection.json';file.write_text(json.dumps(receipt))
            with patch.object(builder, 'ROOT', root):
                self.assertEqual(builder.select_products('steam', file)['protocol'],123)
            header.write_text('// no declaration')
            with self.assertRaisesRegex(ValueError, 'declaration unavailable'):p.source_protocol(root)

    def test_package_build_carries_source_protocol_and_hashed_header(self):
        import friend_package
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);checkout=root/'source';receipt=copy.deepcopy(self.receipt)
            def put(name, data=b'fixture'):
                path=checkout/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
                return path
            put(p.PROTOCOL_HEADER, (builder.ROOT/p.PROTOCOL_HEADER).read_bytes())
            put('common/include/kh2coop/AvatarBridge.hpp',
                (builder.ROOT/'common/include/kh2coop/AvatarBridge.hpp').read_bytes())
            for row in [*receipt['products'].values(),receipt['cli']]:
                row['sha256']=builder.digest(put(row['path']))
            for name in ('friend.py','friend_package.py','launcher.py','plan.py','windows_owned.py','steam_flow.py','product_protocol.py'):
                put('tools/launcher/'+name)
            for name in ('python.exe','pythonw.exe','python3.dll','python311.dll','vcruntime140.dll','vcruntime140_1.dll','LICENSE.txt','tcl/tk8.6/license.terms'):
                put('python/'+name)
            for name in ('Redist.txt','ThirdPartyNotices.txt'):put('licenses/'+name)
            for name in ('enet-src/LICENSE','minhook-src/LICENSE.txt'):put('build/_deps/'+name)
            put('docs/FRIEND_PLAYTEST_STEAM.md')
            selection=put('selection.json',json.dumps(receipt).encode())
            args=SimpleNamespace(output=root/'package', evidence=root/'evidence',
                python_home=checkout/'python', crt=checkout/'crt', dumpbin=checkout/'dumpbin',
                vs_licenses=checkout/'licenses', cli=checkout/receipt['cli']['path'],
                cli_sha256=receipt['cli']['sha256'],transport='steam', products=selection)
            with patch.object(builder,'ROOT',checkout), patch.object(builder,'crt_closure',return_value=[]), patch('builtins.print'):
                builder.build(args)
            manifest=friend_package.verify_package(args.output)
            self.assertEqual(manifest['protocol'],self.protocol)
            self.assertEqual(manifest['files'][p.PROTOCOL_HEADER],builder.digest(args.output/p.PROTOCOL_HEADER))
            self.assertEqual(p.source_protocol(args.output),self.protocol)

    def test_builder_refuses_changed_input_bytes(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            for row in self.receipt['products'].values():
                product=root/row['path'];product.parent.mkdir(exist_ok=True);product.write_bytes(b'changed')
            args=SimpleNamespace(output=root/'out', evidence=root/'evidence',
                                 python_home=root/'python', crt=root/'crt', transport='steam', products=None)
            with patch.object(builder, 'ROOT', root), patch.object(builder, 'select_products', return_value=self.receipt):
                with self.assertRaisesRegex(ValueError, 'hash mismatch'):builder.build(args)


if __name__=='__main__':unittest.main()
