# Windows plumbing for KH2 packets

Copy this directory as a group into each fresh packet's Python module path.
Import `winplumb`; do not regenerate copies of these helpers. Frozen packets
and spent runs remain immutable. This module launches no game, takes no rig
lock, connects to no machine and establishes no process ownership at import.
It never relaxes native/save/session/admission/STOP checks.

* `text.read_text/read_json`: strict UTF-8, one optional BOM; refuse double or
  locale-decoded BOMs. `write_text/write_json`: exclusive, BOM-free UTF-8.
  `encoded_command`: UTF-16LE/base64 transport for already verified UTF-8 PS
  source; never decode a PS file with the locale default. `utf8_env` extends
  the full inherited environment with PYTHONUTF8/PYTHONUNBUFFERED.
* Dot-source `utf8.ps1` after the parameter block and before reads/children.
  It sets PowerShell's pipeline and Console input/output encoding to UTF-8
  without a BOM, sets both Python flags, and runs chcp65001 when a console is
  attached. There is no console codepage to change in a no-console process.
  PS5 Read-WinPlumbText/Json accept one BOM; Write-WinPlumbText creates new
  BOM-free output. Never emit a mojibake BOM prefix to make PS5 read Unicode.
* `atomic.replacement_script(source,target,predecessor=None,retry_journal=None)`:
  same-volume first Move, later Replace with a unique retained predecessor.
  Refuses an existing predecessor; retries ONLY native32/33 sharing/lock errors,
  every25ms for at most750ms measured by Stopwatch. Other failures are immediate.
  Caller's existing owned transport executes this script inside its deadline;
  this helper performs no retry of transport, GO, input, or game launch.
* `bootstrap.bootstrap(fresh_operator_log)` before any logging/worker import:
  stdio None/invalid detection, replacement UTF-8 line-buffered file streams,
  Win32 standard handles, unbuffered environment. `configure_logging` installs
  FileHandler only, using that owned file-backed stream; no console stderr
  StreamHandler. `child_stdio(log)` supplies DEVNULL/file/file. Operator logs
  belong outside packet/stage. Preserve original worker PID and runtime/STOP
  Event. Call the bootstrap only once and retain its returned log for life.
* `bundled.prepare(packet,fresh_runtime_root)`: verify the pinned ZIP and full
  Python311 group, extract outside packet, verify all pinned dependencies and
  prove real PIL.Image/PIL._imaging import under the bundled interpreter before
  worker GO. It creates no game; the one import child is bounded20s and journaled.
  `desktop.ps1` requires explicit absolute Worker/OperatorLogDir/RuntimeRoot/Packet,
  rehashes the admission group/dependencies/pins, then runs that interpreter
  (never PATH Python). Its unchanged embedded `_pth` needs an explicit worker
  directory insertion before same-process runpy. System Python3.12 cannot load
  a pinned cp311 Pillow module. This is a staging control, not game admission.
* `desktop.worker_argv` returns the same pinned-interpreter command for callers
  that already own an atomic job/journal spawn wrapper. `overlay_off_argv`
  routes exactly `overlay off --pid ORIGINAL` to portable bin/kh2ctl.exe, with
  cwd=package. The internal diagnostic intentionally rejects overlay. Caller
  MUST check its retained original PID+creation/image handle and product pins
  immediately before dispatch; this command builder never adopts a process.

Children use explicit file stdio inside the existing ownership/job wrapper;
do not replace that wrapper with a bare Popen to gain these helpers. A receipt
or command builder's existence does not establish closure. Preserve original
PID/creation/image/argv and fresh independent descendant/port/file checks.

`python -B tools/rig/winplumb/controls.py --out <fresh-directory> --packet <verified-reference-packet>` runs actual
PS5 (console and no-console), native32 sharing conflicts, Pythonw None streams
and invalid native handles. All child processes are owned/bounded/reaped, and
all stdout/stderr/raw files are retained. It never launches KH2 or contacts a
machine. Copy the group and run that SAME entrypoint through the authorized
SSH path for remote qualification. The optional packet argument also exercises
fresh full ZIP runtime extraction and actual bundled311/Pillow desktop launch,
without game boot. No CMake/native build is required.

All PowerShell child/control calls use `text.powershell_argv`, which explicitly supplies process-scoped `-ExecutionPolicy Bypass`. Never change CurrentUser or LocalMachine policy. The restrictive-parent control sets only its own Process policy to Restricted, invokes the module child, and confirms the parent remains Restricted.
