"""Double-click entry for the self-contained friend preview; --self-check is offline."""
import argparse
import datetime as dt
import json
import os
from pathlib import Path
import queue
import sys
import threading
import time
import uuid

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from friend_package import GameOwner, verify_package
from launcher import Session, require_idle_rig
from plan import ROOT, LOCAL, Options, make_plan
from steam_flow import invitation, confirm_account, RuntimeStatus



class SteamFields:
    """Default-off connection choice, used by the real UI and its offline screenshot."""
    def __init__(self, parent, tk, ttk, role):
        self.role=role;self.locked=False
        self.mode=tk.StringVar(value='Steam (beta)')
        self.identity=tk.StringVar();self.host=tk.StringVar();self.allow=tk.StringVar()
        self.expected=tk.StringVar();self.confirmed=tk.BooleanVar(value=False)
        self.allow_confirmed=tk.BooleanVar(value=False);self.appid_consent=tk.BooleanVar(value=False)
        self.hosting=False
        self.note=tk.StringVar(value='Start Steam and sign in to the account owning KH2 before Start game.')
        self.frame=ttk.Frame(parent);self.frame.columnconfigure(1,weight=1)
        ttk.Label(self.frame,text='Connection').grid(row=0,column=0,sticky='w',pady=5)
        self.choice=ttk.Combobox(self.frame,textvariable=self.mode,values=('ENet / relay','Steam (beta)'),state='readonly')
        self.choice.grid(row=0,column=1,columnspan=2,sticky='ew',padx=(15,0))
        self.body=ttk.Frame(self.frame);self.body.columnconfigure(1,weight=1)
        ttk.Label(self.body,text='Your SteamID').grid(row=0,column=0,sticky='w')
        self.own=ttk.Entry(self.body,textvariable=self.identity,state='readonly')
        self.own.grid(row=0,column=1,sticky='ew',padx=10)
        self.copy=ttk.Button(self.body,text='Copy',command=self.copy_identity,state='disabled')
        self.copy.grid(row=0,column=2)
        self.host_label=ttk.Label(self.body,text='Host SteamID')
        self.host_entry=ttk.Entry(self.body,textvariable=self.host)
        self.allow_label=ttk.Label(self.body,text='Allowed friend IDs')
        self.allow_entry=ttk.Entry(self.body,textvariable=self.allow)
        self.hint=tk.StringVar()
        ttk.Label(self.body,textvariable=self.hint,wraplength=670).grid(row=2,columnspan=3,sticky='w',pady=(5,0))
        ttk.Label(self.body,textvariable=self.note,wraplength=670).grid(row=3,columnspan=3,sticky='w',pady=(3,5))
        ttk.Label(self.body,text='Expected SteamID (optional)').grid(row=4,column=0,sticky='w')
        self.expected_entry=ttk.Entry(self.body,textvariable=self.expected)
        self.expected_entry.grid(row=4,column=1,columnspan=2,sticky='ew',padx=10)
        self.account_check=ttk.Checkbutton(self.body,text='The displayed SteamID is my account',variable=self.confirmed)
        self.account_check.grid(row=5,columnspan=3,sticky='w')
        self.allow_check=ttk.Checkbutton(self.body,text='Only these friend accounts may join this hosting session',variable=self.allow_confirmed)
        self.allow_check.grid(row=6,columnspan=3,sticky='w')
        self.appid_check=ttk.Checkbutton(self.body,text='Allow creation of missing steam_appid.txt (2552430) beside KH2; remove it after owned game closure',variable=self.appid_consent)
        self.appid_check.grid(row=7,columnspan=3,sticky='w')
        self.invite=ttk.Button(self.body,text='Copy invitation',command=self.copy_invitation,state='disabled')
        self.invite.grid(row=8,columnspan=3,sticky='e')
        self.identity.trace_add('write',lambda *args:self.confirmed.set(False))
        self.allow.trace_add('write',lambda *args:self.allow_confirmed.set(False))
        for var in (self.mode,self.role,self.identity):var.trace_add('write',lambda *args:self.refresh())
        self.refresh()

    def transport(self):
        modes={'ENet / relay':'enet','Steam (beta)':'steam'}
        if self.mode.get() not in modes:raise ValueError('Choose a connection mode.')
        return modes[self.mode.get()]

    def copy_identity(self):
        from plan import steam_id
        value=steam_id(self.identity.get())
        self.frame.clipboard_clear();self.frame.clipboard_append(value)

    def copy_invitation(self):
        if not self.hosting:raise ValueError('Start hosting before sharing an invitation.')
        self.frame.clipboard_clear();self.frame.clipboard_append(invitation(self.identity.get()))

    def set_locked(self, locked):
        self.locked=locked;self.refresh()

    def refresh(self):
        self.choice.configure(state='disabled' if self.locked else 'readonly')
        if self.mode.get() == 'Steam (beta)':
            self.body.grid(row=1,columnspan=3,sticky='ew',pady=6)
        else:self.body.grid_remove()
        host=self.role.get() == 'host'
        self.hint.set('Allow 1–2 SteamID64s, separated by commas. Disconnect to change this list; this ends the friends\' session.' if host else 'Paste the host SteamID64 or kh2coop:steam: invitation. The host must allow your SteamID. Valve relays only.')
        for widget in (self.host_label,self.host_entry,self.allow_label,self.allow_entry):widget.grid_remove()
        label,entry=(self.allow_label,self.allow_entry) if host else (self.host_label,self.host_entry)
        label.grid(row=1,column=0,sticky='w',pady=5);entry.grid(row=1,column=1,columnspan=2,sticky='ew',padx=10)
        entry.configure(state='disabled' if self.locked else 'normal')
        self.copy.configure(state='normal' if self.identity.get() else 'disabled')
        self.invite.configure(state='normal' if host and self.hosting and self.identity.get() else 'disabled')
        self.expected_entry.configure(state='disabled' if self.locked else 'normal')
        for widget in (self.account_check,self.allow_check,self.appid_check):
            widget.configure(state='disabled' if self.locked else 'normal')
        if host:self.allow_check.grid()
        else:self.allow_check.grid_remove()


def make_view(tk, ttk, browse):
    """Construct the real launcher widgets only. No discovery, files, game or network."""
    app = tk.Tk(); app.title('KH2 Co-op — friend preview'); app.geometry('900x940');app.minsize(900,940)
    frame=ttk.Frame(app,padding=20);frame.pack(fill='both',expand=True);frame.columnconfigure(1,weight=1)
    ttk.Label(frame,text='KH2 Co-op',font=('Segoe UI',20,'bold')).grid(row=0,columnspan=3,sticky='w')
    ttk.Label(frame,text='Steam hosting and joining: no Tailscale needed. Start your game, load your save, then connect. Do not save during this preview.',
              wraplength=630).grid(row=1,columnspan=3,sticky='w',pady=(5,15))
    values={k:tk.StringVar(value=v) for k,v in {'game':'','mode':'join','endpoint':'','port':'27795','name':'Friend','slot':'friend1'}.items()}
    relay=tk.BooleanVar(value=False);loaded=tk.BooleanVar(value=False)
    def field(n,title,widget):
        label=ttk.Label(frame,text=title)
        label.grid(row=n,column=0,sticky='w',pady=7)
        widget.grid(row=n,column=1,columnspan=2,sticky='ew',padx=(15,0))
        return label,widget
    field(2,'Your KH2 game folder',ttk.Entry(frame,textvariable=values['game']))
    ttk.Button(frame,text='Browse…',command=browse).grid(row=3,column=2,sticky='e')
    play_as=ttk.Combobox(frame,textvariable=values['mode'],values=('host','join'),state='readonly')
    field(4,'Play as',play_as)
    steam = SteamFields(frame, tk, ttk, values['mode'])
    steam.frame.grid(row=5,columnspan=3,sticky='ew',pady=4)
    enet_widgets=(*field(6,'Relay address from your host',ttk.Entry(frame,textvariable=values['endpoint'])),
                  *field(7,'Port',ttk.Entry(frame,textvariable=values['port'])))
    field(8,'Your name',ttk.Entry(frame,textvariable=values['name']))
    field(9,'Join slot',ttk.Combobox(frame,textvariable=values['slot'],values=('friend1','friend2'),state='readonly'))
    relay_check=ttk.Checkbutton(frame,text='Host only: run the relay here (not supported in this preview)',variable=relay)
    relay_check.grid(row=10,columnspan=3,sticky='w')
    def connection_fields(*_):
        for widget in (*enet_widgets,relay_check):
            if steam.transport() == 'enet':widget.grid()
            else:widget.grid_remove()
    steam.mode.trace_add('write',connection_fields)
    connection_fields()
    ttk.Checkbutton(frame,text='I loaded my save and the host says the room is ready to join',variable=loaded).grid(row=11,columnspan=3,sticky='w',pady=10)
    status=tk.StringVar(value='Nothing starts automatically. Steam uses your own KH2 Steam session and Valve relays.')
    ttk.Label(frame,textvariable=status,wraplength=630).grid(row=13,columnspan=3,sticky='w',pady=10)
    ttk.Label(frame,text='Disconnect leaves the game open. Exit & close game closes only the game started here.\n'
              'Session limit: 30 minutes. Logs stay in this package. No save is committed.',wraplength=630).grid(row=14,columnspan=3,sticky='w')
    buttons=ttk.Frame(frame);buttons.grid(row=12,columnspan=3,sticky='ew')
    actions={}
    for key,label in [('launch','Start game'),('connect','Connect'),('disconnect','Disconnect'),('close','Exit & close game'),('hud','Show HUD')]:
        actions[key]=ttk.Button(buttons,text=label)
        actions[key].pack(side='left',padx=(0,8))
    actions['hud'].configure(state='disabled')
    from types import SimpleNamespace
    return SimpleNamespace(app=app,frame=frame,values=values,relay=relay,loaded=loaded,
                           status=status,actions=actions,steam=steam,play_as=play_as)



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-check', action='store_true')
    args = parser.parse_args()
    os.chdir(ROOT)
    manifest = verify_package(ROOT)
    for key in list(os.environ):
        if key.upper().startswith('KH2COOP_'):
            del os.environ[key]
    os.environ['TCL_LIBRARY'] = str(ROOT/'python/tcl/tcl8.6')
    os.environ['TK_LIBRARY'] = str(ROOT/'python/tcl/tk8.6')
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox
    if args.self_check:
        print(json.dumps({'ok':True,'scope':'offline files/imports/Tcl only; no game or network',
                          'python':sys.version,'tcl':tk.Tcl().eval('info patchlevel'),
                          'package':str(ROOT),'bridgeVersion':manifest['avatarBridgeVersion'],
                          'defaultTransport':manifest.get('defaultTransport','enet')}))
        return 0
    from windows_owned import Windows
    win = Windows()
    if win.session() == 0:
        raise ValueError('Open Start KH2 Co-op.cmd on your Windows desktop.')
    # One friend UI per package folder. Distinct unpacked copies can rehearse together.
    import hashlib
    key = int(hashlib.sha256(str(ROOT).casefold().encode()).hexdigest()[:12],16)
    package_lock = win.mutex('package_'+str(key))
    owner = GameOwner(ROOT, manifest, win)
    view = make_view(tk, ttk, lambda: view.values['game'].set(filedialog.askdirectory() or view.values['game'].get()))
    view.steam.mode.set('Steam (beta)' if manifest.get('defaultTransport','enet') == 'steam' else 'ENet / relay')
    app,frame,values,relay,loaded,status = view.app,view.frame,view.values,view.relay,view.loaded,view.status
    steam=view.steam
    events=queue.Queue();busy=False;session=None;closing=False
    log_offset=0;log_fragment='';roster=False;last_rtt='';rtt_at=0.;connection_line=''
    runtime_status=RuntimeStatus();broker_offset=0;broker_fragment=''
    def run_work(fn,label):
        nonlocal busy
        if busy:return
        busy=True;status.set(label)
        def work():
            try:events.put(('ok',fn()))
            except Exception as error:events.put(('error',str(error)))
        threading.Thread(target=work,daemon=False).start()
    def fresh_dir():
        return LOCAL/'runs'/(dt.datetime.now(dt.timezone.utc).strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:8])
    def launch():
        directory=values['game'].get()
        transport=steam.transport()
        appid_consent=steam.appid_consent.get()
        steam.identity.set('')
        steam.note.set('Waiting for the owned game broker receipt.')
        def action():
            require_idle_rig(win)
            r=owner.launch(directory,fresh_dir(),transport=transport,appid_consent=appid_consent)
            return f'Game {r["processId"]} prepared with save protection. Load your save, confirm your Steam account, then Start hosting or Join.'
        run_work(action,'Checking the exact game build and starting KH2…')
    def connect():
        nonlocal session,log_offset,log_fragment,roster,last_rtt,rtt_at,connection_line,runtime_status,broker_offset,broker_fragment
        if busy:return
        try:
            if not owner.ready or not win.alive(owner.handle):raise ValueError('Start and prepare your game here first.')
            if not loaded.get():raise ValueError('Load your save and wait for your host, then check the ready box.')
            if session and not session.done.is_set():raise ValueError('Already connected or connecting.')
            transport=steam.transport()
            identity=owner.connection_identity(transport)
            if transport == 'steam':
                confirm_account(identity,steam.expected.get(),steam.confirmed.get())
                if values['mode'].get() == 'host' and not steam.allow_confirmed.get():
                    raise ValueError('Confirm the explicit friend allowlist before Start hosting.')
            opt=Options(owner.pid,values['mode'].get(),values['endpoint'].get(),
                        int(values['port'].get()) if transport == 'enet' else 27795,
                        values['name'].get(),values['slot'].get(),relay.get() if transport == 'enet' else False,1800,
                        transport=transport,steam_self=identity,
                        steam_host=steam.host.get(),steam_allow=steam.allow.get())
            plan=make_plan(opt,fresh_dir(),runtime=owner.product('runtime'),server=owner.product('server'))
            verify_package(ROOT)
            log_offset=0;log_fragment='';roster=False;last_rtt='';rtt_at=0.;connection_line=''
            runtime_status=RuntimeStatus(hosting=values['mode'].get() == 'host')
            broker_path=ROOT/'build/rig/logs'/f'steam-broker_{owner.pid}.log'
            broker_offset=broker_path.stat().st_size if transport == 'steam' else 0;broker_fragment=''
            session=Session(win,plan,owner.product('cli'))
            run_work(lambda:(session.start() or session.error or 'Connecting; waiting for verified roster.'),'Starting private session…')
        except Exception as error:messagebox.showerror('Cannot connect',str(error))
    def disconnect():
        if busy:return
        if session and not session.done.is_set():run_work(lambda:(session.stop() or 'Disconnected. Game left open.'),'Disconnecting…')
    def close():
        nonlocal closing
        if busy:return
        closing=True
        def action():
            if session:session.stop('launcher closing')
            result=owner.close_game();return json.dumps(result)
        run_work(action,'Closing owned helpers and game…')
    def hud(enabled, automatic=False):
        if busy or closing or not roster or not session:return
        current=session;line=connection_line
        def action():
            owner.set_overlay(enabled,current,line,automatic=automatic)
            return 'HUD '+('on' if enabled else 'off')+' requested. Receipt saved in session logs.'
        run_work(action,'Updating HUD…')
    for key,fn in [('launch',launch),('connect',connect),('disconnect',disconnect),('close',close)]:
        view.actions[key].configure(command=fn)
    hud_button=view.actions['hud']
    hud_button.configure(command=lambda:hud(owner.overlay_enabled is not True))
    def tick():
        nonlocal busy,closing,log_offset,log_fragment,roster,last_rtt,rtt_at,connection_line,broker_offset,broker_fragment
        try:
            kind,text=events.get_nowait();busy=False
            if kind=='error':
                closing=False;status.set(text);messagebox.showerror('Action stopped',text)
            else:
                status.set(text)
                if closing:app.destroy();return
        except queue.Empty:pass
        if session and not busy:
            if session.started.is_set() and not session.done.is_set():
                if not win.alive(session.game) or any(p.poll() is not None for p in session.processes.values()):
                    if owner.transport == 'steam':
                        try:
                            with (session.path/'runtime.log').open('rb') as stream:
                                stream.seek(log_offset);tail=stream.read(65536).decode('utf-8',errors='replace')
                            for line in (log_fragment+tail).splitlines():runtime_status.feed(line)
                        except OSError:pass
                    run_work(lambda:session.stop('game/helper exited'),'Session ended…')
                else:
                    log=session.path/'runtime.log'
                    text=''
                    if log.exists():
                        with log.open('rb') as stream:
                            stream.seek(log_offset);text=stream.read(65536).decode('utf-8',errors='replace');log_offset=stream.tell()
                    parts=(log_fragment+text).split('\n');log_fragment=parts.pop();text='\n'.join(parts)
                    import re
                    rtt=re.findall(r'\[Runtime\] Net: rtt=(\d+)ms',text)
                    if rtt:last_rtt=rtt[-1];rtt_at=time.monotonic()
                    for line in text.splitlines():
                        runtime_status.feed(line)
                        if line.startswith('[Runtime] Network: SessionState session='):
                            roster=True;connection_line=line
                        if 'Network: closed ' in line or 'refused by relay:' in line:
                            roster=False;last_rtt='';connection_line=''
                    if owner.transport == 'steam':
                        from friend_package import read_shared_prefix
                        try:
                            broker_text,_=read_shared_prefix(ROOT/'build/rig/logs'/f'steam-broker_{owner.pid}.log',offset=broker_offset)
                            broker_offset+=len(broker_text.encode('utf-8'))
                            broker_lines=(broker_fragment+broker_text).split('\n');broker_fragment=broker_lines.pop()
                            for line in broker_lines:runtime_status.feed(line.rstrip('\r'))
                        except OSError as error:
                            runtime_status.text='Cannot read the owned Steam receipt: '+str(error)
                            runtime_status.roster=False;runtime_status.terminal=True
                        roster=runtime_status.roster
                    status.set((runtime_status.text if owner.transport == 'steam' else 'Connected — roster verified' if roster else 'Waiting for verified roster')+
                               (f' · RTT {last_rtt} ms'+(' (stale)' if time.monotonic()-rtt_at>5 else '') if last_rtt else '')+'\n'+str(session.path))
                    if owner.transport == 'steam' and runtime_status.terminal and not session.stopping.is_set():
                        run_work(lambda:session.stop('Steam session ended'),runtime_status.text)
                    elif roster and owner.overlay_enabled is None and owner.overlay_auto_session is not session:
                        hud(True,automatic=True)
            elif session.done.is_set():status.set(session.error or (runtime_status.text if runtime_status.terminal else 'Disconnected. Game remains open; do not save.'))
        active = bool(session and not session.done.is_set())
        steam.hosting=active and roster and runtime_status.hosting and runtime_status.listener and not runtime_status.terminal
        steam.set_locked(busy or closing or active)
        view.play_as.configure(state='disabled' if busy or closing or active else 'readonly')
        view.actions['connect'].configure(text='Start hosting' if values['mode'].get() == 'host' else 'Join')
        if not busy and not closing and owner.ready and owner.transport == 'steam':
            try:
                identity=owner.read_steam_identity()
                if steam.identity.get() != identity:steam.identity.set(identity)
                steam.note.set('Your game Steam session is ready.' if identity else 'Waiting for the game Steam session (broker cap: 60 seconds).')
            except (OSError, ValueError) as error:
                steam.identity.set('')
                steam.note.set('Steam ID unavailable: '+str(error))
        elif not owner.ready:
            steam.identity.set('')
        hud_button.configure(text='Hide HUD' if owner.overlay_enabled is True else 'Show HUD',
            state='normal' if not busy and not closing and roster and session and
            session.started.is_set() and not session.stopping.is_set() and not session.done.is_set() else 'disabled')
        app.after(500,tick)
    app.protocol('WM_DELETE_WINDOW',close);app.after(500,tick)
    try:app.mainloop()
    finally:
        if session:session.stop('launcher exit')
        win.close(package_lock)
    return 0


if __name__=='__main__':
    try:raise SystemExit(main())
    except Exception as error:
        if '--self-check' in sys.argv:raise
        import tkinter.messagebox
        tkinter.messagebox.showerror('KH2 Co-op could not start',str(error))
        raise SystemExit(1)
