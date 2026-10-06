"""Offline safety boundaries; all game operations are fakes, no KH2/network."""
import json
import os
from pathlib import Path
import sys
import tempfile
import threading
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parent))
import friend_package as f
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'packaging'))
from build_friend import leakage, allowed
from plan import Options, make_plan


class Win:
    live=True
    def __init__(self):self.k=self;self.wait_calls=[]
    def WaitForSingleObject(self,handle,milliseconds):
        self.wait_calls.append((handle,milliseconds))
        return 258 if self.live else 0
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
        log=self.root/'build/rig/logs/kh2coop_inject_42.log';log.parent.mkdir(parents=True,exist_ok=True);log.write_text('Save guard installed: fixture')
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
    def test_delayed_termination_waits_same_handle_once(self):
        o=self.owner(lambda *a,**k:{'ok':True,'killed':[42]});o.handle='retained';o.pid=42
        def wait(handle,ms):
            self.win.wait_calls.append((handle,ms))
            return 258 if ms==0 else 0
        self.win.WaitForSingleObject=wait
        result=o.close_game()
        self.assertEqual(self.win.wait_calls,[('retained',0),('retained',2000)])
        self.assertTrue(result['closureVerified']);self.assertIsNone(o.handle)
    def test_termination_still_pending_refuses_without_retry(self):
        o=self.owner(lambda *a,**k:{'ok':True,'killed':[42]});o.handle='retained';o.pid=42
        with self.assertRaises(f.CanonicalFailure) as error:o.close_game()
        self.assertEqual(error.exception.receipt['exitWaitCode'],258)
        self.assertEqual(error.exception.receipt['canonicalKill']['killed'],[42])
        self.assertEqual(self.win.wait_calls,[('retained',0),('retained',2000)])
        self.assertEqual(o.handle,'retained')
    def test_late_exit_retry_resolves_same_retained_handle(self):
        def pending(*args,**kw):
            self.calls.append(args[2])
            if args[2][0]=='launch':raise f.CanonicalFailure('guard failed',{'command':'launch','processId':42})
            return {'ok':True,'killed':[42]}
        o=self.owner(pending)
        with patch.object(self.win,'open_query',wraps=self.win.open_query) as opened:
            with patch.object(f,'verify_game',return_value=self.root.parent/f.GAME_NAME), self.assertRaisesRegex(ValueError,'If the game is still open'):
                o.launch('unused',self.root/'run')
            self.assertTrue(o.unresolved_launch);self.assertEqual(o.handle,'retained')
            with self.assertRaisesRegex(ValueError,'Use Exit & close game'):o.launch('unused',self.root/'run2')
            self.win.live=False  # original process exits after the bounded wait
            result=o.close_game()  # same GameOwner called again by Exit
            opened.assert_called_once_with(42)
        self.assertTrue(result['closureVerified']);self.assertEqual(result['exitWaitCode'],0)
        self.assertFalse(o.unresolved_launch);self.assertIsNone(o.handle)
        self.assertEqual([a[0] for a in self.calls],['launch','kill'])
        self.assertEqual(self.win.wait_calls,[('retained',0),('retained',2000),('retained',0)])
    def test_invalid_retained_handle_is_not_exited(self):
        o=self.owner();o.handle='retained';o.pid=42
        self.win.WaitForSingleObject=lambda handle,ms:0xFFFFFFFF
        with self.assertRaises(f.CanonicalFailure):o.close_game()
        self.assertEqual(o.handle,'retained');self.assertEqual(self.calls,[])
    def test_postkill_wait_error_and_missing_kill_receipt_refuse(self):
        for killed,final_code in (([42],0xFFFFFFFF),([],0)):
            with self.subTest(killed=killed,final_code=final_code):
                o=self.owner(lambda *a,**k:{'ok':True,'killed':killed});o.handle='retained';o.pid=42
                codes=iter((258,final_code));self.win.WaitForSingleObject=lambda handle,ms:next(codes)
                with self.assertRaises(f.CanonicalFailure):o.close_game()
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
        with patch.object(f,'verify_game',return_value=self.root.parent/f.GAME_NAME), self.assertRaisesRegex(ValueError,'If the game is still open'):
            o.launch('unused',self.root/'run')
        self.assertTrue(o.unresolved_launch);self.assertEqual(o.handle,'retained')
    def test_child_env_only_and_no_test_switches(self):
        with patch.dict(os.environ,{'KH2COOP_SAVEGUARD_TEST_DIR':'bad','SteamAppId':'other'}):
            env=f.child_environment(self.root)
            self.assertNotIn('KH2COOP_SAVEGUARD_TEST_DIR',env)
            self.assertEqual(env['SteamAppId'],'2552430');self.assertEqual(env['SteamGameId'],'2552430')
            self.assertEqual(env.get('PATH'),os.environ.get('PATH'))
            self.assertEqual(os.environ['SteamAppId'],'other')
    def hud_setup(self, response=None):
        o=self.owner()
        with patch.object(f,'verify_game',return_value=self.root.parent/f.GAME_NAME):
            o.launch('unused',self.root/'launch')
        session=SimpleNamespace(lock=threading.RLock(),plan={'options':{'pid':42}},
            started=threading.Event(),stopping=threading.Event(),done=threading.Event(),
            processes={'runtime':SimpleNamespace(poll=lambda:None)},path=self.root/'session')
        session.started.set();session.path.mkdir();self.calls.clear()
        def command(root,cli,args,**kw):
            self.calls.append(args)
            self.assertEqual(kw['timeout'],8)
            return response if response is not None else {'ok':True,'processId':42,'overlay':args[1]=='on'}
        o.command=command
        return o,session,'[Runtime] Network: SessionState session=123 actors=2 room=fixture'
    def test_hud_auto_once_then_manual_off_on_exact_receipts(self):
        o,s,line=self.hud_setup()
        o.set_overlay(True,s,line,automatic=True)
        self.assertIsNone(o.set_overlay(True,s,line,automatic=True))
        o.set_overlay(False,s,line);o.set_overlay(True,s,line)
        self.assertEqual(self.calls,[['overlay',v,'--pid','42'] for v in ('on','off','on')])
        rows=[json.loads(r) for r in (s.path/'overlay.jsonl').read_text().splitlines()]
        self.assertEqual(len(rows),3)
        self.assertTrue(all(r['confirmed'] and r['receipt']['processId']==42 and
                            r['finishedNs']>=r['startedNs'] for r in rows))
        self.assertEqual([r['automatic'] for r in rows],[True,False,False])
    def test_hud_refuses_unprepared_unconnected_wrong_owner_or_stopped(self):
        o,s,line=self.hud_setup()
        changes=[(o,'ready',False),(o,'unresolved_launch',True),(o,'handle',None),
                 (self.win,'live',False),(s,'plan',{'options':{'pid':43}}),
                 (s,'processes',{}),(s,'processes',{'runtime':SimpleNamespace(poll=lambda:0)})]
        for obj,key,value in changes:
            with self.subTest(key=key),patch.object(obj,key,value),self.assertRaises(ValueError):
                o.set_overlay(True,s,line)
        for flag in (s.stopping,s.done):
            flag.set()
            with self.assertRaises(ValueError):o.set_overlay(True,s,line)
            flag.clear()
        s.started.clear()
        with self.assertRaises(ValueError):o.set_overlay(True,s,line)
        s.started.set()
        with self.assertRaises(ValueError):o.set_overlay(True,s,'Connecting')
        with self.assertRaises(ValueError):o.set_overlay('on',s,line)
        self.assertEqual(self.calls,[])
    def test_hud_hide_survives_reconnect_but_new_game_resets_preference(self):
        o,s,line=self.hud_setup()
        overlay_command=o.command
        o.set_overlay(True,s,line,automatic=True);o.set_overlay(False,s,line)
        s.done.set()
        second=SimpleNamespace(**vars(s));second.done=threading.Event()
        self.assertIsNone(o.set_overlay(True,second,line,automatic=True))
        self.assertFalse(o.overlay_enabled);self.assertEqual(len(self.calls),2)
        self.win.live=False;o.close_game()  # verified closure of original handle
        self.win.live=True;o.command=self.command
        with patch.object(f,'verify_game',return_value=self.root.parent/f.GAME_NAME):
            o.launch('unused',self.root/'launch2')
        self.assertIsNone(o.overlay_enabled);self.assertIsNone(o.overlay_auto_session)
        o.command=overlay_command
        o.set_overlay(True,second,line,automatic=True)
        self.assertTrue(o.overlay_enabled)
        self.assertEqual([a for a in self.calls if a[0]=='overlay'],
                         [['overlay',v,'--pid','42'] for v in ('on','off','on')])
    def test_hud_rechecks_guard_and_package_before_flag(self):
        o,s,line=self.hud_setup()
        log=Path(o.receipt['log']);log.write_text('guard unavailable')
        with self.assertRaises(ValueError):o.set_overlay(True,s,line)
        log.write_text('Save guard installed')
        (self.root/'bin/cli').write_bytes(b'changed')
        with self.assertRaises(ValueError):o.set_overlay(True,s,line)
        self.assertEqual(self.calls,[])
    def test_hud_wrong_receipt_unknown_and_no_automatic_retry(self):
        o,s,line=self.hud_setup({'ok':True,'processId':43,'overlay':True})
        with self.assertRaises(ValueError):o.set_overlay(True,s,line,automatic=True)
        self.assertIsNone(o.set_overlay(True,s,line,automatic=True))
        self.assertIsNone(o.overlay_enabled);self.assertEqual(len(self.calls),1)
        row=json.loads((s.path/'overlay.jsonl').read_text())
        self.assertFalse(row['confirmed']);self.assertEqual(row['receipt']['processId'],43)
        for receipt in ({'ok':True,'processId':42,'overlay':False},
                        {'ok':True,'processId':42,'overlay':1}, {'ok':False,'processId':42,'overlay':True}):
            o.command=lambda *a,**k:receipt
            with self.assertRaises(ValueError):o.set_overlay(True,s,line)
        def fail(*a,**k):raise f.CanonicalFailure('refused',{'ok':False,'error':'refused'})
        o.command=fail
        with self.assertRaises(f.CanonicalFailure):o.set_overlay(False,s,line)
        row=json.loads((s.path/'overlay.jsonl').read_text().splitlines()[-1])
        self.assertEqual(row['receipt'],{'ok':False,'error':'refused'})
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
