"""Offline Steam launcher boundaries. No game, broker, discovery or sockets."""
from dataclasses import replace
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import friend_package as f
from plan import Options, make_plan, steam_id
import test_friend_package as fixtures

SELF = '76561197960265729'
FRIEND = '76561197960265730'
OTHER = '76561197960265731'
READY = f'[steam-broker] ready appId=2552430 identity={SELF} modulePinnedUntilExit=1\n'


class Plans(unittest.TestCase):
    def plan(self, **kw):
        opt = Options(42, 'host', '', transport='steam', steam_self=SELF, steam_allow=FRIEND)
        return make_plan(replace(opt, **kw), Path('run'), runtime=Path('runtime'), server=Path('server'))

    def test_host_explicit_allowlist_no_enet_or_standalone_relay(self):
        p = self.plan(steam_allow=FRIEND + ', ' + OTHER)
        a = p['runtime_argv']
        self.assertEqual(a[6:11], ['--steam-host', '--steam-allow', FRIEND, '--steam-allow', OTHER])
        self.assertEqual(a[a.index('--role')+1], 'player')
        self.assertIsNone(p['relay_argv'])
        for token in ('--server', '--port', '--steam-join'):self.assertNotIn(token, a)

    def test_join_exact_host_identity_and_friend_role(self):
        a = self.plan(mode='join', steam_host=FRIEND, slot='friend2')['runtime_argv']
        self.assertEqual(a[6:8], ['--steam-join', FRIEND])
        self.assertEqual(a[a.index('--role')+1], 'friend2')
        self.assertNotIn('--steam-host', a)

    def test_invalid_allowlist_refused(self):
        for ids in ('', SELF, FRIEND+','+FRIEND, FRIEND+','+OTHER+','+SELF, FRIEND+',', 'https://steamcommunity.com/id/name', '1234', FRIEND+' --port 1'):
            with self.subTest(ids=ids), self.assertRaises(ValueError):self.plan(steam_allow=ids)

    def test_invalid_or_self_join_refused(self):
        for identity in ('', SELF, '480', 'https://steamcommunity.com/profiles/'+FRIEND):
            with self.subTest(identity=identity), self.assertRaises(ValueError):self.plan(mode='join',steam_host=identity)

    def test_id_native_contract_and_unknown_self(self):
        self.assertEqual(steam_id(SELF), SELF)
        for identity in ('76561197960265728', '00000000000000001', '１７５６１１９７９６０２６５７３０', '+'+SELF, int(SELF)):
            with self.subTest(identity=identity), self.assertRaises(ValueError):steam_id(identity)
        with self.assertRaises(ValueError):self.plan(steam_self='')

    def test_no_mixed_local_relay_or_unknown_transport(self):
        with self.assertRaises(ValueError):self.plan(local_relay=True)
        with self.assertRaises(ValueError):self.plan(transport='auto')

    def test_enet_argv_and_config_unchanged_from_committed_pre_ui(self):
        # Retained source comparison in the evidence runner covers the entire
        # plan; this literal protects the externally executed ENet command.
        p = make_plan(Options(42,'host','100.64.1.2',local_relay=True),Path('run'),runtime=Path('runtime'),server=Path('server'))
        self.assertEqual(p['runtime_argv'], ['runtime','--config',str(Path('run/runtime.ini')),
            '--mode','campaign_coop','--network','--server','100.64.1.2','--port','7782',
            '--pid','42','--role','player','--peer-id','player-1','--no-camera','--tick-ms','16',
            '--max-ticks','37500','--desync-dir',str(Path('run/runtime-desync'))])
        self.assertEqual(p['runtime_config'],'game_build=1.0.0.10-steam-global\ncontent_hash=none\nmod_hash=none\n')
        self.assertEqual(p['options']['transport'],'enet')


class Ownership(unittest.TestCase):
    # Reuse the established fake game/manifest; existing safety tests run separately.
    setUp = fixtures.Safety.setUp
    save_manifest = fixtures.Safety.save_manifest
    owner = fixtures.Safety.owner
    command = fixtures.Safety.command
    def launch_steam(self):
        owner=self.owner()
        with patch.object(f,'verify_game',return_value=self.root.parent/f.GAME_NAME):
            owner.launch('unused',self.root/'steam-run',transport='steam')
        return owner

    def test_opt_in_only_in_launch_child_not_parent_or_later_commands(self):
        kwargs=[]
        def command(*args,**kw):
            kwargs.append((args[2],kw));return self.command(*args,**kw)
        o=self.owner(command)
        with patch.object(f,'verify_game',return_value=self.root.parent/f.GAME_NAME):
            o.launch('unused',self.root/'steam-run',transport='steam')
        o.close_game()
        self.assertTrue(kwargs[0][1]['steam_broker'])
        self.assertTrue(all('steam_broker' not in kw for _,kw in kwargs[1:]))
        request=json.loads((self.root/'steam-run/launch-request.json').read_text())
        self.assertEqual(request['transport'],'steam');self.assertTrue(request['steamBroker'])
        with patch.dict(os.environ,{'KH2COOP_STEAM_BROKER':'unsafe','KH2COOP_TRACE_HITS':'1'}):
            self.assertNotIn('KH2COOP_STEAM_BROKER',f.child_environment(self.root))
            self.assertEqual(f.child_environment(self.root,steam_broker=True)['KH2COOP_STEAM_BROKER'],'1')
            self.assertNotIn('KH2COOP_TRACE_HITS',f.child_environment(self.root,steam_broker=True))
            self.assertEqual(os.environ['KH2COOP_STEAM_BROKER'],'unsafe')

    def test_canonical_nonlaunch_cannot_enable_broker(self):
        with patch.object(subprocess,'run') as run, self.assertRaises(ValueError):
            f.canonical(self.root,'cli',['instances'],timeout=8,steam_broker=True,run=run)
        run.assert_not_called()

    def test_owned_fresh_identity_and_no_repeat_file_read(self):
        o=self.launch_steam()
        reader=lambda path:(READY,o.launch_wall_ns+1)
        self.assertEqual(o.read_steam_identity(read=reader),SELF)
        with patch.object(f,'read_shared_prefix',side_effect=AssertionError('cached')):
            self.assertEqual(o.connection_identity('steam'),SELF)
        self.win.live=False
        with self.assertRaises(ValueError):o.connection_identity('steam')

    def test_stale_receipt_lock_partial_unknown_and_changed_mode_refused(self):
        o=self.launch_steam()
        with self.assertRaisesRegex(ValueError,'predates'):o.read_steam_identity(read=lambda p:(READY,o.launch_wall_ns-1))
        with patch.object(f,'read_shared_prefix',side_effect=PermissionError('locked')),self.assertRaises(PermissionError):o.connection_identity('steam')
        with patch.object(f,'read_shared_prefix',return_value=(READY[:-1],o.launch_wall_ns+1)),self.assertRaisesRegex(ValueError,'not ready'):o.connection_identity('steam')
        with self.assertRaisesRegex(ValueError,'mode changed'):o.connection_identity('enet')
        self.assertEqual(o.steam_identity,'')

    def test_identity_requires_guard_attestation_and_retained_handle(self):
        o=self.launch_steam()
        for attr,value in (('ready',False),('handle',None),('unresolved_launch',True),('transport','enet')):
            with self.subTest(attr=attr),patch.object(o,attr,value),self.assertRaises(ValueError):
                o.read_steam_identity(read=lambda p:(READY,o.launch_wall_ns+1))

    def test_new_game_resets_broker_identity(self):
        o=self.launch_steam();o.read_steam_identity(read=lambda p:(READY,o.launch_wall_ns+1))
        o.close_game();self.win.live=True
        with patch.object(f,'verify_game',return_value=self.root.parent/f.GAME_NAME):
            o.launch('unused',self.root/'enet-run')
        self.assertEqual(o.transport,'enet');self.assertEqual(o.steam_identity,'')


class Receipts(unittest.TestCase):
    def test_only_exact_complete_app_identity_line(self):
        self.assertEqual(f.parse_broker_identity(READY),SELF)
        for text in (READY[:-1],READY.replace('2552430','480'),'[steam-broker] connect peer='+SELF+' handle=1\n'):
            self.assertEqual(f.parse_broker_identity(text),'')
        with self.assertRaises(ValueError):f.parse_broker_identity(READY+READY)
        with self.assertRaises(ValueError):f.parse_broker_identity(READY+'[steam-broker] refused pipe-create\n')

    @unittest.skipUnless(os.name=='nt','Windows file-sharing receipt')
    def test_actual_bounded_shared_file_read_without_game(self):
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder)/'broker.log';p.write_bytes((READY+'x'*20000).encode())
            with p.open('ab') as writer:
                writer.write(b'\n');writer.flush()
                text,created=f.read_shared_prefix(p)
            self.assertEqual(len(text),8192);self.assertGreater(created,0)
            self.assertEqual(f.parse_broker_identity(text),SELF)


if __name__=='__main__':unittest.main(verbosity=2)
