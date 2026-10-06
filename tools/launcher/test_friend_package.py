"""Offline safety boundaries; all game operations are fakes, no KH2/network."""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parent))
import friend_package as f
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'packaging'))
from build_friend import leakage, allowed
from plan import Options, make_plan


class Win:
    live=True
    def alive(self, handle):return self.live
    def open_query(self,pid):return 'retained'
    def close(self,handle):self.closed=handle


class Safety(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)/'package';self.root.mkdir()
        self.products={r:'bin/'+r for r in ('cli','dll','runtime','server','avatarctl')}
        for name in self.products.values():
            p=self.root/name;p.parent.mkdir(exist_ok=True);p.write_bytes(b'fixture')
        self.manifest={'schema':1,'avatarBridgeVersion':3,'products':self.products,
                       'files':{n:f.digest(self.root/n) for n in self.products.values()},
                       'supportedGame':{'sha256':f.SUPPORTED_GAME}}
        self.save_manifest();self.win=Win();self.calls=[]
    def save_manifest(self):(self.root/'package.json').write_text(json.dumps(self.manifest))
    def owner(self,command=None):return f.GameOwner(self.root,self.manifest,self.win,command or self.command)
    def command(self,root,cli,args,**kw):
        self.calls.append(args)
        if args[0]=='kill':self.win.live=False;return {'ok':True,'killed':[42]}
        if args[0]=='instances':return {'ok':True,'instances':[{'processId':42,'owned':True}]}
        log=self.root/'build/rig/logs/kh2coop_inject_42.log';log.parent.mkdir(parents=True);log.write_text('Save guard installed: fixture')
        return {'ok':True,'processId':42,'hooksInstalled':True,'errors':[],'log':str(log)}
    def test_package_integrity_and_bridge_mix(self):
        f.verify_package(self.root)
        self.manifest['avatarBridgeVersion']=2;self.save_manifest()
        with self.assertRaises(ValueError):f.verify_package(self.root)
        self.manifest['avatarBridgeVersion']=3;self.save_manifest()
        (self.root/'bin/dll').write_bytes(b'wrong')
        with self.assertRaises(ValueError):f.verify_package(self.root)
    def test_escape_path_refused(self):
        self.manifest['files']['../outside']='bad';self.save_manifest()
        with self.assertRaises(ValueError):f.verify_package(self.root)
    def test_unknown_exe_no_launch(self):
        game=self.root.parent/'game';game.mkdir();(game/f.GAME_NAME).write_bytes(b'unknown')
        with self.assertRaisesRegex(ValueError,'not supported'):self.owner().launch(game,self.root/'run')
        self.assertEqual(self.calls,[])
    def test_package_inside_game_refused(self):
        with self.assertRaisesRegex(ValueError,'beside'):f.verify_game(self.root.parent,self.root)
    def test_verified_launch_then_only_owned_canonical_kill(self):
        o=self.owner()
        with patch.object(f,'verify_game',return_value=self.root.parent/f.GAME_NAME):o.launch('unused',self.root/'run')
        self.assertTrue(o.ready);self.assertEqual(self.calls[0][0],'launch')
        o.close_game();self.assertEqual(self.calls[-1],['kill','--pid','42'])
        self.assertIsNone(o.handle)
    def test_dead_retained_handle_never_kills_reused_pid(self):
        o=self.owner();o.handle='old';o.pid=42;self.win.live=False
        o.close_game();self.assertEqual(self.calls,[])
    def test_failed_kill_keeps_retained_identity(self):
        o=self.owner(lambda *a,**k:{'killed':[]});o.handle='retained';o.pid=42
        with self.assertRaises(ValueError):o.close_game()
        self.assertEqual(o.handle,'retained')
    def test_missing_guard_never_ready(self):
        def bad(*args,**kw):
            r=self.command(*args,**kw)
            if 'log' in r:Path(r['log']).write_text('InputCollector hook installed')
            return r
        o=self.owner(bad)
        with patch.object(f,'verify_game',return_value=self.root.parent/f.GAME_NAME), self.assertRaises(ValueError):o.launch('unused',self.root/'run')
        self.assertFalse(o.ready);self.assertEqual(self.calls[-1],['kill','--pid','42'])
        self.assertFalse(self.win.live)
    def test_unknown_start_blocks_repeat_without_pid_adoption(self):
        def fail(*a,**kw):raise TimeoutError('fixture')
        o=self.owner(fail)
        with patch.object(f,'verify_game',return_value=self.root.parent/f.GAME_NAME), self.assertRaises(TimeoutError):o.launch('unused',self.root/'run')
        with self.assertRaisesRegex(ValueError,'previous start'):o.launch('unused',self.root/'run2')
        self.assertIsNone(o.pid);self.assertEqual(o.close_game()['killed'],[])
    def test_canonical_hook_error_with_exact_launch_pid_is_closed(self):
        def error(*args,**kw):
            if args[2][0]=='launch':
                raise f.CanonicalFailure('save guard incomplete',{'ok':False,'command':'launch','processId':42})
            return self.command(*args,**kw)
        o=self.owner(error)
        with patch.object(f,'verify_game',return_value=self.root.parent/f.GAME_NAME), self.assertRaisesRegex(ValueError,'was closed'):
            o.launch('unused',self.root/'run')
        self.assertEqual(self.calls,[['kill','--pid','42']]);self.assertFalse(self.win.live)
        self.assertFalse(o.unresolved_launch)
        self.assertEqual(json.loads((self.root/'run/launch-result.json').read_text())['cleanup']['killed'],[42])
    def test_failed_guard_cleanup_keeps_unresolved_and_handle(self):
        def fail(*args,**kw):
            if args[2][0]=='launch':raise f.CanonicalFailure('guard failed',{'command':'launch','processId':42})
            raise ValueError('canonical kill failed')
        o=self.owner(fail)
        with patch.object(f,'verify_game',return_value=self.root.parent/f.GAME_NAME), self.assertRaisesRegex(ValueError,'Exit that game normally'):
            o.launch('unused',self.root/'run')
        self.assertTrue(o.unresolved_launch);self.assertEqual(o.handle,'retained')
    def test_child_env_only_and_no_test_switches(self):
        with patch.dict(os.environ,{'KH2COOP_SAVEGUARD_TEST_DIR':'bad','SteamAppId':'other'}):
            env=f.child_environment(self.root)
            self.assertNotIn('KH2COOP_SAVEGUARD_TEST_DIR',env)
            self.assertEqual(env['SteamAppId'],'2552430');self.assertEqual(env['SteamGameId'],'2552430')
            self.assertEqual(env.get('PATH'),os.environ.get('PATH'))
            self.assertEqual(os.environ['SteamAppId'],'other')
    def test_private_endpoint_only(self):
        for address in ('0.0.0.0','127.0.0.1','8.8.8.8','example.com'):
            with self.assertRaises(ValueError):make_plan(Options(42,'join',address),self.root,runtime=Path('runtime'),server=Path('server'))
        plan=make_plan(Options(42,'host','100.108.214.60',local_relay=True),self.root,runtime=Path('runtime'),server=Path('server'))
        self.assertIn('100.108.214.60',plan['relay_argv'])
    def test_leak_encodings_and_exclusions(self):
        for enc in ('utf-8','utf-16-le','utf-16-be'):
            with self.assertRaises(ValueError):leakage('file',('xxVoLpEyy').encode(enc))
        for path in ('python/Lib/site-packages/x.py','x/__pycache__/a.pyc','x.pdb','developer.log'):
            self.assertFalse(allowed(Path(path)))
        leakage('python/Lib/json/__init__.py',b'ordinary stdlib')


if __name__=='__main__':unittest.main(verbosity=2)
