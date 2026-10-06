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
    app = tk.Tk(); app.title('KH2 Co-op — friend preview'); app.geometry('690x635')
    frame=ttk.Frame(app,padding=20);frame.pack(fill='both',expand=True);frame.columnconfigure(1,weight=1)
    ttk.Label(frame,text='KH2 Co-op',font=('Segoe UI',20,'bold')).grid(row=0,columnspan=3,sticky='w')
    ttk.Label(frame,text='Start your game, load your save, then connect. Do not save during this preview.',
              wraplength=630).grid(row=1,columnspan=3,sticky='w',pady=(5,15))
    values={k:tk.StringVar(value=v) for k,v in {'game':'','mode':'join','endpoint':'','port':'27795','name':'Friend','slot':'friend1'}.items()}
    relay=tk.BooleanVar(value=False);loaded=tk.BooleanVar(value=False)
    def field(n,title,widget):
        ttk.Label(frame,text=title).grid(row=n,column=0,sticky='w',pady=7)
        widget.grid(row=n,column=1,columnspan=2,sticky='ew',padx=(15,0))
    field(2,'Your KH2 game folder',ttk.Entry(frame,textvariable=values['game']))
    ttk.Button(frame,text='Browse…',command=lambda:values['game'].set(filedialog.askdirectory() or values['game'].get())).grid(row=3,column=2,sticky='e')
    field(4,'Play as',ttk.Combobox(frame,textvariable=values['mode'],values=('host','join'),state='readonly'))
    field(5,'Relay address from James',ttk.Entry(frame,textvariable=values['endpoint']))
    field(6,'Port',ttk.Entry(frame,textvariable=values['port']))
    field(7,'Your name / peer ID',ttk.Entry(frame,textvariable=values['name']))
    field(8,'Join slot',ttk.Combobox(frame,textvariable=values['slot'],values=('friend1','friend2'),state='readonly'))
    ttk.Checkbutton(frame,text='Host only: run the relay here on the tailnet address above',variable=relay).grid(row=9,columnspan=3,sticky='w')
    ttk.Checkbutton(frame,text='I loaded my save and James says the room is ready to join',variable=loaded).grid(row=10,columnspan=3,sticky='w',pady=10)
    status=tk.StringVar(value='Nothing starts automatically. Steam and Tailscale must already be ready.')
    ttk.Label(frame,textvariable=status,wraplength=630).grid(row=12,columnspan=3,sticky='w',pady=10)
    ttk.Label(frame,text='Disconnect leaves the game open. Exit & close game closes only the game started here.\n'
              'Session limit: 30 minutes. Logs stay in this package. No save is committed.',wraplength=630).grid(row=13,columnspan=3,sticky='w')
    events=queue.Queue();busy=False;session=None;closing=False
    log_offset=0;log_fragment='';roster=False;last_rtt='';rtt_at=0.
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
        def action():
            require_idle_rig(win)
            r=owner.launch(directory,fresh_dir())
            return f'Game {r["processId"]} prepared with save protection. Load your save manually, then Connect.'
        run_work(action,'Checking the exact game build and starting KH2…')
    def connect():
        nonlocal session,log_offset,log_fragment,roster,last_rtt,rtt_at
        if busy:return
        try:
            if not owner.ready or not win.alive(owner.handle):raise ValueError('Start and prepare your game here first.')
            if not loaded.get():raise ValueError('Load your save and wait for James, then check the ready box.')
            if session and not session.done.is_set():raise ValueError('Already connected or connecting.')
            opt=Options(owner.pid,values['mode'].get(),values['endpoint'].get(),int(values['port'].get()),
                        values['name'].get(),values['slot'].get(),relay.get(),1800)
            plan=make_plan(opt,fresh_dir(),runtime=owner.product('runtime'),server=owner.product('server'))
            verify_package(ROOT)
            session=Session(win,plan,owner.product('cli'))
            log_offset=0;log_fragment='';roster=False;last_rtt='';rtt_at=0.
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
    buttons=ttk.Frame(frame);buttons.grid(row=11,columnspan=3,sticky='ew')
    for label,fn in [('Start game',launch),('Connect',connect),('Disconnect',disconnect),('Exit & close game',close)]:
        ttk.Button(buttons,text=label,command=fn).pack(side='left',padx=(0,8))
    def tick():
        nonlocal busy,closing,log_offset,log_fragment,roster,last_rtt,rtt_at
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
                    if 'SessionState session=' in text:roster=True
                    if 'Network: closed ' in text or 'refused by relay:' in text:roster=False;last_rtt=''
                    status.set(('Connected — roster verified' if roster else 'Waiting for verified roster')+
                               (f' · RTT {last_rtt} ms'+(' (stale)' if time.monotonic()-rtt_at>5 else '') if last_rtt else '')+'\n'+str(session.path))
            elif session.done.is_set():status.set(session.error or 'Disconnected. Game remains open; do not save.')
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
