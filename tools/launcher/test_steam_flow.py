"""Offline friend-flow controls; disposable files and mocked game calls only."""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parent))
from steam_flow import AppIdFile, RuntimeStatus, broker_error, confirm_account, host_identity, invitation
from plan import Options, make_plan
import friend_package as f
import test_friend_package as fixtures

A='76561197960265729';B='76561197960265730'


class Flow(unittest.TestCase):
    def test_invitation_roundtrip_and_pure_steam_plan(self):
        self.assertEqual(host_identity(invitation(A)),A)
        self.assertEqual(host_identity(' '+A+' '),A)
        p=make_plan(Options(42,'join','',transport='steam',steam_self=B,steam_host=invitation(A)),Path('run'),runtime=Path('runtime'),server=Path('server'))
        argv=p['runtime_argv'];self.assertEqual(argv[argv.index('--steam-join')+1],A)
        self.assertIsNone(p['relay_argv'])
        for option in ('--server','--port','tailscale'):self.assertNotIn(option,argv)
    def test_invitation_rejects_urls_shell_arguments_and_extra_fields(self):
        for value in ('steam://run/2552430',invitation(A)+' --port 1',invitation(A)+'?allow=all','kh2coop:other:'+A,invitation(A)+'\n'+B):
            with self.subTest(value=value),self.assertRaises(ValueError):host_identity(value)
    def test_account_requires_actual_identity_and_explicit_confirmation(self):
        self.assertEqual(confirm_account(A,A,True),A)
        for expected,confirmed in ((B,True),('',False),(A,1)):
            with self.subTest(expected=expected,confirmed=confirmed),self.assertRaises(ValueError):confirm_account(A,expected,confirmed)
    def test_host_left_is_persistent_and_generic_loss_rejoins(self):
        s=RuntimeStatus();s.feed('[Runtime] Network: closed code=0')
        self.assertIn('Reconnecting',s.text);self.assertFalse(s.terminal)
        s.feed('[Runtime] Rejoin attempt=1');s.feed('[Runtime] Network: SessionState session=2')
        self.assertTrue(s.roster)
        s.feed('[Runtime] Networking stopped: terminal relay/session close')
        self.assertIn('Host left',s.text);self.assertFalse(s.roster);self.assertTrue(s.terminal)
        s.feed('[Runtime] Network: SessionState session=3');self.assertIn('Host left',s.text)
    def test_host_requires_current_listener_and_roster(self):
        s=RuntimeStatus(hosting=True);s.feed('[Runtime] Network: SessionState session=2')
        self.assertNotIn('Hosting',s.text)
        s.feed('[steam-broker] listen handle=22 iceCreation=0');self.assertIn('Hosting',s.text)
    def test_refusal_and_relay_errors_are_actionable_and_persistent(self):
        s=RuntimeStatus();s.feed('[Runtime] Network: refused by relay: version mismatch')
        self.assertIn('version mismatch',s.text);self.assertTrue(s.terminal)
        s.feed('[Runtime] Rejoin attempt=1');self.assertIn('version mismatch',s.text)
        self.assertIn('Start Steam',broker_error('[steam-broker] unavailable existing-session'))
        self.assertIn('authentication or Valve relay',broker_error('[steam-broker] unavailable auth-relay'))
        with self.assertRaisesRegex(ValueError,'Start Steam'):
            f.parse_broker_identity('[steam-broker] unavailable existing-session\n')
    def test_packager_requires_explicit_steam_products(self):
        from build_friend import select_products
        with self.assertRaises(ValueError):select_products('steam',None)
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'products.json';p.write_text(json.dumps({'products':{}}))
            with self.assertRaisesRegex(ValueError,'declare Steam'):select_products('steam',p)
            receipt={'transport':'steam','products':{k:{} for k in ('dll','runtime','server','avatarctl')}}
            p.write_text(json.dumps(receipt));self.assertEqual(select_products('steam',p),receipt)
            receipt['products']['extra']={};p.write_text(json.dumps(receipt))
            with self.assertRaisesRegex(ValueError,'exactly'):select_products('steam',p)
    def test_appid_lease_consent_create_cleanup_and_preserve_existing(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'steam_appid.txt';lease=AppIdFile(p)
            with self.assertRaises(ValueError):lease.prepare(False)
            self.assertFalse(p.exists());self.assertTrue(lease.prepare(True)['created'])
            self.assertEqual(p.read_bytes(),b'2552430\n');self.assertTrue(lease.cleanup()['removed'])
            p.write_bytes(b'2552430\r\n');self.assertFalse(lease.prepare(False)['created'])
            self.assertFalse(lease.cleanup()['removed']);self.assertEqual(p.read_bytes(),b'2552430\r\n')
    def test_changed_and_wrong_appid_are_preserved(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'steam_appid.txt';lease=AppIdFile(p);lease.prepare(True)
            p.write_bytes(b'480\n');self.assertFalse(lease.cleanup()['removed'])
            with self.assertRaises(ValueError):AppIdFile(p).prepare(True)
            self.assertEqual(p.read_bytes(),b'480\n')
            p.unlink();p.mkdir()
            with self.assertRaises(ValueError):AppIdFile(p).prepare(True)
    def test_concurrent_creator_is_not_adopted(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'steam_appid.txt';lease=AppIdFile(p)
            original=Path.open;first=True
            def racing(path,*args,**kw):
                nonlocal first
                if args and args[0]=='xb' and first:
                    first=False
                    with original(path,'wb') as stream:stream.write(b'2552430\n')
                    raise FileExistsError()
                return original(path,*args,**kw)
            with patch.object(Path,'open',racing):self.assertFalse(lease.prepare(True)['created'])
            self.assertFalse(lease.cleanup()['removed']);self.assertTrue(p.exists())

    @unittest.skipUnless(os.name=='nt','Windows launcher widgets')
    def test_real_widgets_confirmations_and_invitation_gates(self):
        import tkinter as tk
        from tkinter import ttk
        from friend import make_view
        view=make_view(tk,ttk,lambda:None);view.app.withdraw()
        try:
            view.values['mode'].set('host');view.app.update_idletasks()
            self.assertEqual(view.steam.transport(),'steam')
            self.assertEqual(str(view.steam.invite['state']),'disabled')
            view.steam.identity.set(A);view.steam.confirmed.set(True)
            view.steam.identity.set(B);self.assertFalse(view.steam.confirmed.get())
            view.steam.allow.set(A);view.steam.allow_confirmed.set(True)
            view.steam.allow.set(B);self.assertFalse(view.steam.allow_confirmed.get())
            view.steam.hosting=True;view.steam.refresh()
            self.assertEqual(str(view.steam.invite['state']),'normal')
            view.steam.set_locked(True)
            self.assertEqual(str(view.steam.allow_entry['state']),'disabled')
            self.assertLessEqual(view.frame.winfo_reqheight(),940)
        finally:view.app.destroy()


class Owner(unittest.TestCase):
    setUp=fixtures.Safety.setUp
    save_manifest=fixtures.Safety.save_manifest
    owner=fixtures.Safety.owner
    command=fixtures.Safety.command
    def test_owned_game_closure_removes_only_its_appid(self):
        owner=self.owner();appid=self.root.parent/'steam_appid.txt'
        with patch.object(f,'verify_game',return_value=self.root.parent/f.GAME_NAME):
            owner.launch('unused',self.root/'run',transport='steam',appid_consent=True)
        self.assertTrue(appid.exists());owner.close_game();self.assertFalse(appid.exists())
        self.assertTrue(json.loads((self.root/'run/appid-cleanup.json').read_text())['removed'])
    def test_missing_consent_never_launches_and_failed_closure_preserves(self):
        owner=self.owner();appid=self.root.parent/'steam_appid.txt'
        with patch.object(f,'verify_game',return_value=self.root.parent/f.GAME_NAME):
            with self.assertRaises(ValueError):owner.launch('unused',self.root/'no-consent',transport='steam')
            self.assertEqual(self.calls,[])
            owner.launch('unused',self.root/'run',transport='steam',appid_consent=True)
        with patch.object(owner,'command',side_effect=ValueError('closure failed')),self.assertRaises(ValueError):owner.close_game()
        self.assertTrue(appid.exists())


if __name__=='__main__':unittest.main(verbosity=2)
