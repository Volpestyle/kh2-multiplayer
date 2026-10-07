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



class SteamFields:
    """Default-off connection choice, used by the real UI and its offline screenshot."""
    def __init__(self, parent, tk, ttk, role):
        self.role=role;self.locked=False
        self.mode=tk.StringVar(value='ENet / relay')
        self.identity=tk.StringVar();self.host=tk.StringVar();self.allow=tk.StringVar()
        self.note=tk.StringVar(value='Choose Steam before Start game. Requires a broker-enabled build; release09 is unchanged.')
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

    def set_locked(self, locked):
        self.locked=locked;self.refresh()

    def refresh(self):
        self.choice.configure(state='disabled' if self.locked else 'readonly')
        if self.mode.get() == 'Steam (beta)':
            self.body.grid(row=1,columnspan=3,sticky='ew',pady=6)
        else:self.body.grid_remove()
        host=self.role.get() == 'host'
        self.hint.set('SteamID64 only. Host allows 1-2 friends, separated by commas. Valve relays only.' if host else 'SteamID64 only. Valve relays only.')
        for widget in (self.host_label,self.host_entry,self.allow_label,self.allow_entry):widget.grid_remove()
        label,entry=(self.allow_label,self.allow_entry) if host else (self.host_label,self.host_entry)
        label.grid(row=1,column=0,sticky='w',pady=5);entry.grid(row=1,column=1,columnspan=2,sticky='ew',padx=10)
        entry.configure(state='disabled' if self.locked else 'normal')
        self.copy.configure(state='normal' if self.identity.get() else 'disabled')


def make_view(tk, ttk, browse):
    """Construct the real launcher widgets only. No discovery, files, game or network."""
    app = tk.Tk(); app.title('KH2 Co-op — friend preview'); app.geometry('740x860')
    frame=ttk.Frame(app,padding=20);frame.pack(fill='both',expand=True);frame.columnconfigure(1,weight=1)
    ttk.Label(frame,text='KH2 Co-op',font=('Segoe UI',20,'bold')).grid(row=0,columnspan=3,sticky='w')
    ttk.Label(frame,text='Start your game, load your save, then connect. Do not save during this preview.',
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
    field(4,'Play as',ttk.Combobox(frame,textvariable=values['mode'],values=('host','join'),state='readonly'))
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
    status=tk.StringVar(value='Nothing starts automatically. ENet uses Tailscale; Steam (beta) uses your game Steam session.')
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
                           status=status,actions=actions,steam=steam)



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
                          'package':str(ROOT),'bridgeVersion':manifest['avatarBridgeVersion']}))
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
    app,frame,values,relay,loaded,status = view.app,view.frame,view.values,view.relay,view.loaded,view.status
    steam=view.steam
    events=queue.Queue();busy=False;session=None;closing=False
    log_offset=0;log_fragment='';roster=False;last_rtt='';rtt_at=0.;connection_line=''
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
        steam.identity.set('')
        steam.note.set('Waiting for the owned game broker receipt.')
        def action():
            require_idle_rig(win)
            r=owner.launch(directory,fresh_dir(),transport=transport)
            return f'Game {r["processId"]} prepared with save protection. Load your save manually, then Connect.'
        run_work(action,'Checking the exact game build and starting KH2…')
    def connect():
        nonlocal session,log_offset,log_fragment,roster,last_rtt,rtt_at,connection_line
        if busy:return
        try:
            if not owner.ready or not win.alive(owner.handle):raise ValueError('Start and prepare your game here first.')
            if not loaded.get():raise ValueError('Load your save and wait for your host, then check the ready box.')
            if session and not session.done.is_set():raise ValueError('Already connected or connecting.')
            transport=steam.transport()
            identity=owner.connection_identity(transport)
            opt=Options(owner.pid,values['mode'].get(),values['endpoint'].get(),
                        int(values['port'].get()) if transport == 'enet' else 27795,
                        values['name'].get(),values['slot'].get(),relay.get() if transport == 'enet' else False,1800,
                        transport=transport,steam_self=identity,
                        steam_host=steam.host.get(),steam_allow=steam.allow.get())
            plan=make_plan(opt,fresh_dir(),runtime=owner.product('runtime'),server=owner.product('server'))
            verify_package(ROOT)
            session=Session(win,plan,owner.product('cli'))
            log_offset=0;log_fragment='';roster=False;last_rtt='';rtt_at=0.;connection_line=''
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
        nonlocal busy,closing,log_offset,log_fragment,roster,last_rtt,rtt_at,connection_line
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
                        if line.startswith('[Runtime] Network: SessionState session='):
                            roster=True;connection_line=line
                        if 'Network: closed ' in line or 'refused by relay:' in line:
                            roster=False;last_rtt='';connection_line=''
                    status.set(('Connected — roster verified' if roster else 'Waiting for verified roster')+
                               (f' · RTT {last_rtt} ms'+(' (stale)' if time.monotonic()-rtt_at>5 else '') if last_rtt else '')+'\n'+str(session.path))
                    if roster and owner.overlay_enabled is None and owner.overlay_auto_session is not session:
                        hud(True,automatic=True)
            elif session.done.is_set():status.set(session.error or 'Disconnected. Game remains open; do not save.')
        active = bool(session and not session.done.is_set())
        steam.set_locked(busy or closing or active)
        if not busy and not closing and owner.ready and owner.transport == 'steam':
            try:
                identity=owner.read_steam_identity()
                steam.identity.set(identity)
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
