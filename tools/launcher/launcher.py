"""KH2 development launcher. --plan is pure/offline; GUI requires Windows desktop."""
import argparse
from dataclasses import asdict
import datetime as dt
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import threading
import time
import uuid

from plan import LOCAL, ROOT, Options, make_plan


def require_idle_rig(win):
    path = ROOT / 'build/rig/rig.lock'
    if not path.exists():
        return
    try:
        pid = int(json.loads(path.read_text())['pid'])
        if pid <= 0:
            raise ValueError('Rig lock has no valid owner PID; ask its owner to resolve it.')
        try:
            handle = win.open_query(pid)
        except OSError as error:
            if error.winerror == 87:  # no such process; leave stale lock untouched
                return
            raise
        try:
            if win.alive(handle):
                raise ValueError(f'The scenario rig is active (PID {pid}). Wait for its owner to finish.')
        finally:
            win.close(handle)
    except (KeyError, json.JSONDecodeError) as error:
        raise ValueError('Rig lock is unreadable; ask its owner to resolve it.') from error


def list_games(win, kh2ctl):
    require_idle_rig(win)
    result = subprocess.run([str(kh2ctl), 'instances'], stdin=subprocess.DEVNULL,
                            capture_output=True, text=True,
                            timeout=8, shell=False, creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode:
        raise ValueError(result.stderr.strip() or result.stdout.strip() or 'kh2ctl instances failed')
    receipt = json.loads(result.stdout.strip().splitlines()[-1])
    if not receipt.get('ok'):
        raise ValueError('kh2ctl could not list games.')
    return receipt['instances']


class Session:
    """One set of owned helpers. The game never enters our kill-on-close job."""
    def __init__(self, win, plan, kh2ctl):
        self.win, self.plan, self.kh2ctl = win, plan, kh2ctl
        self.path = Path(plan['run_dir'])
        self.lock = threading.RLock()
        self.done = threading.Event()
        self.started = threading.Event()
        self.stopping = threading.Event()
        self.processes, self.files = {}, []
        self.game = self.mutex = self.job = None
        self.timer = None
        self.error = ''
        self.stop_reason = ''
        self.exits = {}
        self.deadline = time.monotonic() + plan['wall_limit_seconds']

    def spawn(self, name, argv):
        log = (self.path / f'{name}.log').open('xb', buffering=0)
        self.files.append(log)
        self.processes[name] = self.win.spawn(self.job, argv, ROOT, log)

    def start(self):
        with self.lock:
            if self.done.is_set():
                return
            try:
                require_idle_rig(self.win)
                pid = self.plan['options']['pid']
                if pid not in {row['processId'] for row in list_games(self.win, self.kh2ctl)}:
                    raise ValueError('Selected game is no longer listed by kh2ctl.')
                self.game = self.win.selected_game(pid)
                self.mutex = self.win.mutex(pid)
                self.job = self.win.job()
                self.path.mkdir(parents=True, exist_ok=False)
                (self.path / 'runtime.ini').write_text(self.plan['runtime_config'], encoding='ascii')
                (self.path / 'plan.json').write_text(json.dumps(self.plan, indent=2), encoding='utf-8')
                # Wall cap is independent of Tk responsiveness. Runtime also has
                # its native --max-ticks bound; the relay has no duration flag.
                self.deadline = time.monotonic() + self.plan['wall_limit_seconds']
                self.timer = threading.Timer(self.plan['wall_limit_seconds'], self.stop, args=('time limit',))
                self.timer.daemon = True
                self.timer.start()
                if self.plan['relay_argv']:
                    self.spawn('relay', self.plan['relay_argv'])
                    end = time.monotonic() + 5
                    while time.monotonic() < end:
                        if self.processes['relay'].poll() is not None:
                            raise ValueError('Owned relay exited; inspect relay.log. No bind fallback attempted.')
                        if '[Server] Running.' in (self.path / 'relay.log').read_text(errors='replace'):
                            break
                        time.sleep(0.1)
                    else:
                        raise ValueError('Owned relay did not report startup within 5 seconds.')
                self.spawn('runtime', self.plan['runtime_argv'])
                self.started.set()
            except Exception as error:
                self.error = str(error)
                self.stop('startup failure')

    def stop(self, reason='user disconnected'):
        with self.lock:
            if self.done.is_set():
                return
            self.stopping.set()
            self.stop_reason = reason
            if self.timer:
                self.timer.cancel()
            try:
                # Runtime first: allow its normal world reset/disconnect path
                # before stopping a relay we own. An external relay is untouched.
                for name in ('runtime', 'relay'):
                    if name in self.processes:
                        proc = self.processes[name]
                        try:
                            self.exits[name] = {'pid': proc.pid, **self.win.stop(proc)}
                        except Exception as error:
                            self.exits[name] = {'pid': proc.pid, 'stopError': str(error), 'forced': True}
            finally:
                self.win.close(self.job)  # contains ONLY helpers we spawned
                self.job = None
                for proc in self.processes.values():
                    try:
                        proc.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        pass  # job closure already requested termination
                self.win.close(self.game); self.game = None
                self.win.close(self.mutex); self.mutex = None
                for log in self.files:
                    log.close()
                try:
                    if self.path.exists():
                        for name, row in self.exits.items():
                            marker = '[Runtime] Shutdown' if name == 'runtime' else '[Server] Shutdown complete.'
                            row['gracefulShutdownObserved'] = marker in (self.path / f'{name}.log').read_text(errors='replace')
                        (self.path / 'exit.json').write_text(json.dumps({'reason': reason, 'error': self.error,
                            'helpers': self.exits, 'gameStoppedByLauncher': False}, indent=2), encoding='utf-8')
                finally:
                    self.done.set()


def gui(args):
    if os.name != 'nt':
        raise ValueError('The GUI requires Windows; use --plan for offline command inspection.')
    from windows_owned import Windows
    win = Windows()
    if win.session() == 0:
        raise ValueError('Session 0 is unsupported. Root must open this launcher in the existing desktop session.')
    import tkinter as tk
    from tkinter import ttk, messagebox
    from tkinter.scrolledtext import ScrolledText

    root = tk.Tk()
    root.title('KH2 Co-op · Development launcher')
    root.geometry('790x740')
    root.minsize(710, 680)
    frame = ttk.Frame(root, padding=18); frame.pack(fill='both', expand=True)
    ttk.Label(frame, text='Connect your game', font=('Segoe UI', 18, 'bold')).pack(anchor='w')
    ttk.Label(frame, text='Choose an already loaded, injected KH2 game. Prepare it with kh2ctl first.').pack(anchor='w', pady=(4, 14))
    values = {'mode': 'join', 'endpoint': '', 'port': '7782', 'peer_id': 'player-1', 'slot': 'friend1',
              'local_relay': False, 'seconds': '600', 'game_build': '1.0.0.10-steam-global', 'content': 'none', 'mod': 'none'}
    prefs = LOCAL / 'preferences.json'
    if prefs.exists():
        try:
            stored = json.loads(prefs.read_text(encoding='utf-8'))
            values.update({key: stored[key] for key in values if key in stored})
        except (OSError, ValueError):
            pass
    variables = {key: (tk.BooleanVar(value=value) if key == 'local_relay' else tk.StringVar(value=value)) for key, value in values.items()}
    game = tk.StringVar(); acknowledgement = tk.BooleanVar(value=False)
    form = ttk.Frame(frame); form.pack(fill='x'); form.columnconfigure(1, weight=1)
    controls = []
    def row(number, title, widget):
        ttk.Label(form, text=title).grid(row=number, column=0, sticky='w', padx=(0, 15), pady=5)
        widget.grid(row=number, column=1, sticky='ew', pady=5)
        controls.append(widget)
        return widget
    games = row(0, 'Existing game PID', ttk.Combobox(form, textvariable=game, state='readonly'))
    row(1, 'Play as', ttk.Combobox(form, textvariable=variables['mode'], values=('host', 'join'), state='readonly'))
    row(2, 'Relay tailnet IPv4', ttk.Entry(form, textvariable=variables['endpoint']))
    row(3, 'Relay UDP port', ttk.Entry(form, textvariable=variables['port']))
    row(4, 'Join slot', ttk.Combobox(form, textvariable=variables['slot'], values=('friend1', 'friend2'), state='readonly'))
    row(5, 'Unique peer ID', ttk.Entry(form, textvariable=variables['peer_id']))
    row(6, 'Time limit (seconds)', ttk.Entry(form, textvariable=variables['seconds']))
    row(7, 'Reported game build', ttk.Entry(form, textvariable=variables['game_build']))
    row(8, 'Content identifier', ttk.Entry(form, textvariable=variables['content']))
    row(9, 'Mod identifier', ttk.Entry(form, textvariable=variables['mod']))
    relay_check = ttk.Checkbutton(frame, text='Host only: start a relay here, bound to the address above', variable=variables['local_relay'])
    relay_check.pack(anchor='w', pady=(8, 0)); controls.append(relay_check)
    confirm = ttk.Checkbutton(frame, text='No other runtime is attached to this game PID', variable=acknowledgement)
    confirm.pack(anchor='w', pady=5); controls.append(confirm)
    ttk.Label(frame, text='Host uses player slot 0. Peer ID is a network identity; in-game names are not added here.').pack(anchor='w')
    buttons = ttk.Frame(frame); buttons.pack(fill='x', pady=12)
    status = tk.StringVar(value='Choose a game and endpoint. Nothing starts automatically.')
    ping = tk.StringVar(value='Ping: unavailable')
    ttk.Label(frame, textvariable=status, wraplength=730).pack(anchor='w')
    ttk.Label(frame, textvariable=ping).pack(anchor='w', pady=(4, 8))
    output = ScrolledText(frame, height=9, state='disabled', font=('Consolas', 9)); output.pack(fill='both', expand=True)
    session = None
    pending_close = False
    offsets, fragments = {}, {}
    last_ping_at = 0.0
    last_ping = ''
    network_status = 'Connecting'

    def append(text):
        output.configure(state='normal'); output.insert('end', text + '\n')
        if int(output.index('end-1c').split('.')[0]) > 160:
            output.delete('1.0', '40.0')
        output.see('end'); output.configure(state='disabled')

    def refresh():
        try:
            rows = list_games(win, args.kh2ctl_exe)
            games['values'] = [f"{r['processId']} · {r.get('windowTitle', '')}" for r in rows]
            game.set(''); acknowledgement.set(False)
            status.set(f'{len(rows)} game(s) found. Select the intended PID.')
        except Exception as error:
            messagebox.showerror('Cannot list games', str(error))

    def selected_options():
        if not acknowledgement.get():
            raise ValueError('Stop any other runtime for this PID first, then check the confirmation.')
        return Options(pid=int(game.get().split(' · ')[0]), mode=variables['mode'].get(),
            endpoint=variables['endpoint'].get(), port=int(variables['port'].get()),
            peer_id=variables['peer_id'].get(), slot=variables['slot'].get(), local_relay=variables['local_relay'].get(),
            seconds=int(variables['seconds'].get()), game_build=variables['game_build'].get(),
            content=variables['content'].get(), mod=variables['mod'].get())

    def start():
        nonlocal session, last_ping_at, last_ping, network_status
        try:
            options = selected_options()
            run = LOCAL / 'runs' / (dt.datetime.now(dt.timezone.utc).strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:8])
            plan = make_plan(options, run, runtime=args.runtime_exe, server=args.server_exe)
            for path in (args.kh2ctl_exe, args.runtime_exe, *([args.server_exe] if options.local_relay else [])):
                if not path.is_file():
                    raise ValueError(f'Existing binary missing: {path}')
            require_idle_rig(win)
            LOCAL.mkdir(parents=True, exist_ok=True)
            preferences = asdict(options); preferences.pop('pid')
            prefs.write_text(json.dumps(preferences, indent=2), encoding='utf-8')
            offsets.clear(); fragments.clear(); last_ping_at = 0; last_ping = ''; network_status = 'Starting owned helpers'
            session = Session(win, plan, args.kh2ctl_exe)
            for widget in controls:
                widget.configure(state='disabled')
            refresh_button.configure(state='disabled'); start_button.configure(state='disabled'); stop_button.configure(state='normal')
            append('Session logs: ' + str(run))
            threading.Thread(target=session.start, daemon=True).start()
        except Exception as error:
            messagebox.showerror('Cannot connect', str(error))

    def stop():
        if session and not session.done.is_set():
            stop_button.configure(state='disabled')
            status.set('Disconnecting owned helpers…')
            threading.Thread(target=session.stop, daemon=True).start()

    def close():
        nonlocal pending_close
        pending_close = True
        if session and not session.done.is_set():
            stop()
        else:
            root.destroy()

    def tick():
        nonlocal session, network_status, last_ping_at, last_ping
        if session:
            for name in ('relay', 'runtime'):
                path = session.path / f'{name}.log'
                if not path.exists():
                    continue
                with path.open('rb') as log:
                    log.seek(offsets.get(name, 0)); chunk = log.read(65536); offsets[name] = log.tell()
                text = fragments.get(name, '') + chunk.decode('utf-8', errors='replace')
                lines = text.split('\n'); fragments[name] = lines.pop()
                for line in lines:
                    append(f'{name}: {line.rstrip()}')
                    if name != 'runtime':
                        continue
                    if 'transport connected; awaiting verified roster' in line:
                        network_status = 'Transport connected; awaiting roster'
                    if '[Runtime] Network: SessionState session=' in line:
                        network_status = 'Connected (roster verified)'
                    if '[Runtime] Network: closed ' in line or 'refused by relay:' in line:
                        network_status = line.strip(); last_ping_at = 0; last_ping = ''
                    match = re.search(r'\[Runtime\] Net: rtt=(\d+)ms enet_rtt=(\d+)ms', line)
                    if match:
                        app_rtt = int(match[1]); last_ping_at = time.monotonic()
                        last_ping = f'Ping: {app_rtt} ms (application RTT)' if app_rtt != 0xFFFFFFFF else 'Ping: awaiting application sample'
            if session.done.is_set():
                status.set(session.error or f'Disconnected: {session.stop_reason}. Game left running.')
                if any(row.get('forced') or not row.get('gracefulShutdownObserved') for row in session.exits.values()):
                    append('Graceful helper cleanup was not established. Inspect exit.json and native state before reconnecting.')
                ping.set('Ping: unavailable')
                for widget in controls:
                    widget.configure(state='readonly' if isinstance(widget, ttk.Combobox) else 'normal')
                refresh_button.configure(state='normal'); start_button.configure(state='normal'); stop_button.configure(state='disabled')
                acknowledgement.set(False)
                session = None
                if pending_close:
                    root.destroy(); return
            elif session.stopping.is_set():
                status.set('Disconnecting owned helpers…')
            elif session.started.is_set():
                # No process is rediscovered or killed by name/PID here.
                if not win.alive(session.game) or any(p.poll() is not None for p in session.processes.values()):
                    threading.Thread(target=session.stop, args=('game/helper exited',), daemon=True).start()
                seconds = max(0, int(session.deadline - time.monotonic()))
                status.set(f'{network_status} · limit {seconds}s')
                age = time.monotonic() - last_ping_at
                ping.set(last_ping + (f' · stale ({int(age)}s)' if age > 5 else '') if last_ping_at else 'Ping: unavailable')
            else:
                status.set('Starting owned helpers…')
        root.after(500, tick)

    refresh_button = ttk.Button(buttons, text='Refresh games', command=refresh); refresh_button.pack(side='left')
    start_button = ttk.Button(buttons, text='Connect', command=start); start_button.pack(side='left', padx=8)
    stop_button = ttk.Button(buttons, text='Disconnect', command=stop, state='disabled'); stop_button.pack(side='left')
    root.protocol('WM_DELETE_WINDOW', close)
    root.after(500, tick)
    try:
        root.mainloop()
    finally:
        if session:
            session.stop('launcher closing')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', action='store_true', help='Print argv/config only. No GUI, processes, or files.')
    parser.add_argument('--pid', type=int, default=1)
    parser.add_argument('--mode', choices=('host', 'join'), default='join')
    parser.add_argument('--endpoint', default='')
    parser.add_argument('--port', type=int, default=7782)
    parser.add_argument('--peer-id', default='player-1')
    parser.add_argument('--slot', choices=('friend1', 'friend2'), default='friend1')
    parser.add_argument('--local-relay', action='store_true')
    parser.add_argument('--seconds', type=int, default=600)
    parser.add_argument('--runtime-exe', type=Path, default=ROOT/'build/Release/kh2coop_runtime_scaffold.exe')
    parser.add_argument('--server-exe', type=Path, default=ROOT/'build/Release/kh2coop_server.exe')
    parser.add_argument('--kh2ctl-exe', type=Path, default=ROOT/'build/tools/kh2ctl/Release/kh2ctl.exe')
    args = parser.parse_args()
    for name in ('runtime_exe', 'server_exe', 'kh2ctl_exe'):
        setattr(args, name, getattr(args, name).expanduser().resolve())
    try:
        if args.plan:
            options = Options(args.pid, args.mode, args.endpoint, args.port, args.peer_id, args.slot, args.local_relay, args.seconds)
            print(json.dumps(make_plan(options, LOCAL/'runs/PLAN_ONLY', runtime=args.runtime_exe, server=args.server_exe), indent=2))
        else:
            gui(args)
    except Exception as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
