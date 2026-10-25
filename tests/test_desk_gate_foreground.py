"""VUH-2617 desk gate: fullscreen and foreign-game reclaim receipts name what was in front, and busy/fullscreen states
caused by the rig's own focus-less KH2 are ignored but recorded (bld03c: Discord in front, our game owned). Runs
agent.ps1's real Get-ProcessDetail, Get-ForeignActivity, Add-ForeignDetail, Add-IgnoredBusy and Finish-Job in Windows
PowerShell 5.1, with Kh2Desk.Gate stubbed, and Gate.cs's own Covers/ProvenAlive/Covering compiled for real. An owned
or in-job foreground never counts; a foreign game window always counts; states 2/3 are ignored only when every
owned.txt row is proven live, an owned (or in-job) window covers its monitor and no other window does (r2, Sage:
a foreign fullscreen video still reclaims, and an unreadable or unmatched inventory never exempts); presentation (4)
and quiet time (7) always count.

    python -B -m unittest discover -s tests -p test_desk_gate_foreground.py -v
"""
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AGENT = ROOT / "tools" / "rig" / "desk_gate" / "agent.ps1"
PS = ["powershell", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File"]

HARNESS = r"""
param([string]$Agent)
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
namespace Kh2Desk {
  public class Proc { public int Pid; public int Parent; public long Created; public string Path; public string Name; }
  public static class Gate {
    public static List<Proc> Windows = new List<Proc>();
    public static int Front, State = 5, JobPid = -1;
    public static List<Proc> Windowed() { return Windows; }
    public static int ForegroundPid() { return Front; }
    public static int NotificationState() { return State; }
    public static bool InJob(IntPtr job, int pid) { return pid == JobPid; }
    public static void CloseJob(IntPtr job) { }
    public class Cover { public int Pid; public string Class; public long Monitor; public bool Owned; }
    public static List<Cover> Covers = new List<Cover>();
    public static List<Cover> Covering() { return Covers; }
    public static HashSet<string> AliveSet = new HashSet<string>();
    public static bool ProvenAlive(int pid, long created) { return AliveSet.Contains(pid + " " + created); }
  }
}
'@
$ast = [Management.Automation.Language.Parser]::ParseFile($Agent, [ref]$null, [ref]$null)
foreach ($name in 'Get-ProcessDetail', 'Get-OwnedProof', 'Get-ForeignActivity', 'Add-ForeignDetail', 'Add-IgnoredBusy', 'Finish-Job') {
  $fn = $ast.FindAll({ param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name }, $true)
  if (@($fn).Count -ne 1) { throw "agent.ps1 has $(@($fn).Count) $name" }
  . ([scriptblock]::Create($fn[0].Extent.Text))
}
$script:owned = @()
function Get-Owned { $script:owned }
$script:logs = @()
function Log([string]$Text) { $script:logs += $Text }
$root = Join-Path $env:TEMP ('gatefg-' + [guid]::NewGuid()); New-Item -ItemType Directory -Force "$root\done" | Out-Null
# Two rig roots, each with its own build\rig\owned.txt (absent unless a case writes it).
$rigA = Join-Path $root 'rigA'; $rigB = Join-Path $root 'rigB'
foreach ($r in $rigA, $rigB) { New-Item -ItemType Directory -Force "$r\build\rig" | Out-Null }
function Get-RigRoots { @($rigA, $rigB) }
function Set-OwnedTxt($rig, $lines) { $f = "$rig\build\rig\owned.txt"; if ($null -eq $lines) { Remove-Item $f -ErrorAction SilentlyContinue } else { Set-Content -LiteralPath $f -Value $lines -Encoding ascii } }
function Cov($procId, $class, $owned = $false) { $c = New-Object Kh2Desk.Gate+Cover; $c.Pid = $procId; $c.Class = $class; $c.Monitor = 1; $c.Owned = $owned; $c }
$config = [pscustomobject]@{ foreignGameNames = 'Marvel-Win64-Shipping|KINGDOM HEARTS|r5apex'; foreignGamePaths = '\\steamapps\\common\\'
                             foreignGameExempt = '^RiotClient' }
$job = [pscustomobject]@{ name = 'j'; base = 'j'; file = "$root\j.ps1"; jobObject = [IntPtr]1; foreignSeconds = 0; foreignDetail = $null; ignoredBusy = $null; stdout = $null; stderr = $null; warnings = $null }
function Win($procId, $name, $path) { $w = New-Object Kh2Desk.Proc; $w.Pid = $procId; $w.Name = $name; $w.Path = $path; $w }
function Case($label) {
  $activity = Get-ForeignActivity $job
  [pscustomobject]@{ label = $label; activity = $activity; detail = $script:foreignDetail }
}
$me = $PID
$out = @()
[Kh2Desk.Gate]::State = 5; [Kh2Desk.Gate]::Front = $me
$out += Case 'quiet'
[Kh2Desk.Gate]::State = 3
$out += Case 'fullscreen-foreign-front'
$script:owned = @([pscustomobject]@{ pid = $me })
$out += Case 'fullscreen-owned-front'
$script:owned = @()
[Kh2Desk.Gate]::JobPid = $me
$out += Case 'fullscreen-in-job-front'
[Kh2Desk.Gate]::JobPid = -1
[Kh2Desk.Gate]::Front = 999999
$out += Case 'fullscreen-exited-front'
[Kh2Desk.Gate]::State = 2; [Kh2Desk.Gate]::Front = $me
$out += Case 'busy-state'
[Kh2Desk.Gate]::State = 5
[Kh2Desk.Gate]::Windows.Add((Win 4242 'r5apex' 'C:\Games\Apex\r5apex.exe'))
$out += Case 'foreign-game'
$script:owned = @([pscustomobject]@{ pid = 4242 })
$out += Case 'owned-game-window'
$script:owned = @()
[Kh2Desk.Gate]::Windows.Clear()
$out += Case 'quiet-again'
# Receipts.
[Kh2Desk.Gate]::State = 3
$null = Get-ForeignActivity $job
$job.foreignSeconds = 10; $job.foreignDetail = $script:foreignDetail
$receipts = [ordered]@{}
$receipts.fullscreen = Add-ForeignDetail ([ordered]@{ job = 'j'; reason = 'fullscreen' }) $job 'fullscreen'
$receipts.physical = Add-ForeignDetail ([ordered]@{ job = 'j'; reason = 'physical-input' }) $job 'physical-input'
$orphan = [pscustomobject]@{ name = 'old'; jobObject = [IntPtr]::Zero }
$receipts.orphan = Add-ForeignDetail ([ordered]@{ job = 'old'; reason = 'fullscreen' }) $orphan 'fullscreen'
$receipts.nojob = Add-ForeignDetail ([ordered]@{ job = 'x'; reason = 'fullscreen' }) $null 'fullscreen'
# Owned-game busy states (bld03c): our KH2 (5840) owned, proven live and covering its monitor; Discord ($me) in front.
[Kh2Desk.Gate]::Windows.Clear(); [Kh2Desk.Gate]::Front = $me
$kh2 = [pscustomobject]@{ pid = 5840; unreadable = $false }
[void][Kh2Desk.Gate]::AliveSet.Add('5840 111'); [void][Kh2Desk.Gate]::AliveSet.Add('5841 222')
$busy = @()
function BusyCase($label, $state, $txtA, $txtB, $covers) {
  [Kh2Desk.Gate]::State = $state; $script:owned = @($kh2)
  Set-OwnedTxt $rigA $txtA; if ($txtB -ne 'keep') { Set-OwnedTxt $rigB $txtB }   # 'keep': leave rigB as the case set it
  [Kh2Desk.Gate]::Covers.Clear(); foreach ($c in $covers) { [Kh2Desk.Gate]::Covers.Add($c) }
  $a = Get-ForeignActivity $job
  [pscustomobject]@{ label = $label; activity = $a; detail = $script:foreignDetail; ignored = $script:ignoredBusy } }
$ours = @(Cov 5840 'UnityWndClass')
$busy += BusyCase 'owned-busy-2' 2 @('5840 111') $null $ours
$busy += BusyCase 'owned-d3d-3' 3 @('5840 111') $null $ours
$busy += BusyCase 'two-roots-both-proven-2' 2 @('5840 111') @('5841 222') $ours
$job2 = [Kh2Desk.Gate]::JobPid; [Kh2Desk.Gate]::JobPid = 7777
$busy += BusyCase 'in-job-cover-counts-as-ours-2' 2 @('5840 111') $null @((Cov 5840 'K'), (Cov 7777 'helper'))
$busy += BusyCase 'in-job-cover-no-proof-2' 2 $null $null @((Cov 7777 'helper'))
[Kh2Desk.Gate]::JobPid = $job2
$busy += BusyCase 'owned-presentation-4' 4 @('5840 111') $null $ours
$busy += BusyCase 'owned-quiet-7' 7 @('5840 111') $null $ours
$busy += BusyCase 'foreign-video-covering-3' 3 @('5840 111') $null @((Cov 5840 'K'), (Cov 31337 'Qt5QWindowIcon'))
$busy += BusyCase 'foreign-video-covering-2' 2 @('5840 111') $null @((Cov 5840 'K'), (Cov $me 'Chrome_WidgetWin_1'))
$busy += BusyCase 'owned-but-not-covering-2' 2 @('5840 111') $null @()
$busy += BusyCase 'foreign-owned-popup-over-ours-2' 2 @('5840 111') $null @((Cov 5840 'K'), (Cov 31338 '#32770' $true))
$busy += BusyCase 'our-owned-popup-2' 2 @('5840 111') $null @((Cov 5840 'K'), (Cov 5840 '#32770' $true))
$busy += BusyCase 'unowned-busy-2' 2 $null $null $ours
$busy += BusyCase 'unmatched-row-2' 2 @('5840 111', '6000 999') $null $ours
$busy += BusyCase 'unparseable-row-2' 2 @('5840 111', 'garbage') $null $ours
$busy += BusyCase 'stale-creation-2' 2 @('5840 112') $null $ours
# An unreadable inventory: valid rigA plus a rigB owned.txt held open exclusively.
Set-OwnedTxt $rigB @('5841 222')
$lock = [IO.File]::Open("$rigB\build\rig\owned.txt", 'Open', 'ReadWrite', 'None')
$busy += BusyCase 'unreadable-inventory-2' 2 @('5840 111') 'keep' $ours
$lock.Close()
Set-OwnedTxt $rigB $null
[Kh2Desk.Gate]::Windows.Add((Win 4242 'r5apex' 'C:\Games\Apex\r5apex.exe'))
$busy += BusyCase 'owned-busy-with-foreign-game' 2 @('5840 111') $null $ours
[Kh2Desk.Gate]::Windows.Clear()
# The ignored-record cases below run on a proven, covering inventory.
Set-OwnedTxt $rigA @('5840 111'); [Kh2Desk.Gate]::Covers.Clear(); [Kh2Desk.Gate]::Covers.Add((Cov 5840 'K'))
# The job's ignored record, its log lines (first, on a foreground change, capped), receipts and the done file.
$script:logs = @(); $job.ignoredBusy = $null
[Kh2Desk.Gate]::State = 2; $script:owned = @($kh2)
for ($i = 0; $i -lt 3; $i++) { $null = Get-ForeignActivity $job; Add-IgnoredBusy $job $script:ignoredBusy }
$afterSame = @{ checks = $job.ignoredBusy.checks; logs = $script:logs.Count }
[Kh2Desk.Gate]::Front = 999999
$null = Get-ForeignActivity $job; Add-IgnoredBusy $job $script:ignoredBusy
$afterChange = @{ checks = $job.ignoredBusy.checks; logs = $script:logs.Count; last = $job.ignoredBusy.last.foreground.pid; first = $job.ignoredBusy.first.foreground.pid }
for ($i = 0; $i -lt 60; $i++) { [Kh2Desk.Gate]::Front = $(if ($i % 2) { $me } else { 999999 }); $null = Get-ForeignActivity $job; Add-IgnoredBusy $job $script:ignoredBusy }
$capped = @{ logs = $script:logs.Count; checks = $job.ignoredBusy.checks }
$receipts.ignoredOnPhysical = Add-ForeignDetail ([ordered]@{ job = 'j'; reason = 'physical-input' }) $job 'physical-input'
Finish-Job $job 'RECLAIMED gate: physical-input'
$doneText = [string](Get-Content -Raw "$root\done\j.out")   # a plain string: Get-Content's PS* notes explode ConvertTo-Json -Depth 8
Remove-Item -Recurse -Force $root
[ordered]@{ me = $me; cases = $out; receipts = $receipts; busy = $busy; afterSame = $afterSame; afterChange = $afterChange; capped = $capped; logs = @($script:logs | Select-Object -First 2); done = $doneText } | ConvertTo-Json -Depth 8 -Compress
"""


class ForegroundDetail(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with tempfile.TemporaryDirectory() as d:
            h = Path(d) / "harness.ps1"
            h.write_text(HARNESS, encoding="utf-8-sig")
            r = subprocess.run(PS + [str(h), str(AGENT)], capture_output=True, text=True)
        if r.returncode:
            raise AssertionError(r.stderr or r.stdout)
        cls.out = json.loads(r.stdout)
        cls.case = {c["label"]: c for c in cls.out["cases"]}

    def test_rule_unchanged(self):
        expect = {"quiet": None, "fullscreen-foreign-front": "fullscreen", "fullscreen-owned-front": None,
                  "fullscreen-in-job-front": None, "fullscreen-exited-front": "fullscreen", "busy-state": "fullscreen",
                  "foreign-game": "foreign-game", "owned-game-window": None, "quiet-again": None}
        for label, activity in expect.items():
            with self.subTest(label=label):
                self.assertEqual(self.case[label]["activity"], activity)

    def test_fullscreen_detail_names_the_foreground(self):
        d = self.case["fullscreen-foreign-front"]["detail"]
        self.assertEqual(d["activity"], "fullscreen")
        self.assertEqual(d["notificationState"], 3)
        self.assertEqual(d["foreground"]["pid"], self.out["me"])
        self.assertEqual(d["foreground"]["name"].lower(), "powershell")
        self.assertTrue(d["foreground"]["path"].lower().endswith("powershell.exe"))
        self.assertEqual(d["ownedPids"], [])

    def test_exited_foreground_is_null_not_an_error(self):
        d = self.case["fullscreen-exited-front"]["detail"]
        self.assertEqual(d["foreground"], {"pid": 999999, "name": None, "path": None})

    def test_foreign_game_detail_lists_windows(self):
        d = self.case["foreign-game"]["detail"]
        self.assertEqual(d["activity"], "foreign-game")
        self.assertEqual(d["windows"], [{"pid": 4242, "name": "r5apex", "path": "C:\\Games\\Apex\\r5apex.exe"}])

    def test_detail_is_cleared_when_nothing_is_foreign(self):
        for label in ("quiet", "fullscreen-owned-front", "fullscreen-in-job-front", "owned-game-window", "quiet-again"):
            with self.subTest(label=label):
                self.assertIsNone(self.case[label]["detail"])

    def test_receipts(self):
        r = self.out["receipts"]
        self.assertEqual(r["fullscreen"]["foreign"]["activity"], "fullscreen")
        self.assertEqual(r["fullscreen"]["foreign"]["seconds"], 10)
        self.assertEqual(r["fullscreen"]["foreign"]["foreground"]["pid"], self.out["me"])
        self.assertNotIn("foreign", r["physical"])          # other reasons carry no foreign detail
        self.assertNotIn("foreign", r["orphan"])            # an agent-restart orphan has none, and doesn't throw
        self.assertNotIn("foreign", r["nojob"])

    def busy(self, label):
        return next(c for c in self.out["busy"] if c["label"] == label)

    def test_owned_game_busy_states_are_ignored_and_recorded(self):
        for label, state in (("owned-busy-2", 2), ("owned-d3d-3", 3), ("two-roots-both-proven-2", 2),
                             ("in-job-cover-counts-as-ours-2", 2), ("our-owned-popup-2", 2)):
            with self.subTest(label=label):
                c = self.busy(label)
                self.assertIsNone(c["activity"])
                self.assertIsNone(c["detail"])
                self.assertEqual(c["ignored"]["activity"], "ignored-busy")
                self.assertEqual(c["ignored"]["notificationState"], state)
                self.assertEqual(c["ignored"]["foreground"]["pid"], self.out["me"])
                self.assertTrue(all(x["ours"] for x in c["ignored"]["covering"]))
        self.assertEqual(self.busy("owned-busy-2")["ignored"]["provenOwned"], [5840])
        self.assertEqual(self.busy("two-roots-both-proven-2")["ignored"]["provenOwned"], [5840, 5841])

    def test_still_reclaims(self):
        expect = {"owned-presentation-4": "fullscreen", "owned-quiet-7": "fullscreen",
                  "foreign-video-covering-3": "fullscreen", "foreign-video-covering-2": "fullscreen",
                  "owned-but-not-covering-2": "fullscreen", "unowned-busy-2": "fullscreen",
                  "unmatched-row-2": "fullscreen", "unparseable-row-2": "fullscreen", "stale-creation-2": "fullscreen",
                  "unreadable-inventory-2": "fullscreen", "owned-busy-with-foreign-game": "foreign-game",
                  "in-job-cover-no-proof-2": "fullscreen", "foreign-owned-popup-over-ours-2": "fullscreen"}
        for label, activity in expect.items():
            with self.subTest(label=label):
                c = self.busy(label)
                self.assertEqual(c["activity"], activity)
                self.assertIsNone(c["ignored"])
                self.assertEqual(c["detail"]["activity"], activity)

    def test_reclaim_detail_shows_the_evidence(self):
        d = self.busy("foreign-video-covering-3")["detail"]       # Sage's case: foreign VLC-like window covering, state 3
        self.assertEqual(d["provenOwned"], [5840])
        self.assertEqual([(x["pid"], x["ours"]) for x in d["covering"]], [(5840, True), (31337, False)])
        d = self.busy("foreign-owned-popup-over-ours-2")["detail"]   # Sage r2: a foreign owned fullscreen popup
        self.assertEqual([(x["pid"], x["ownedPopup"], x["ours"]) for x in d["covering"]], [(5840, False, True), (31338, True, False)])
        i = self.busy("our-owned-popup-2")["ignored"]
        self.assertEqual([(x["pid"], x["ownedPopup"], x["ours"]) for x in i["covering"]], [(5840, False, True), (5840, True, True)])
        for label in ("unmatched-row-2", "unparseable-row-2", "stale-creation-2", "unreadable-inventory-2", "unowned-busy-2"):
            with self.subTest(label=label):
                self.assertIsNone(self.busy(label)["detail"]["provenOwned"])   # no proof: no exemption

    def test_ignored_record_logs_and_done_file(self):
        self.assertEqual(self.out["afterSame"], {"checks": 3, "logs": 1})        # one line for a steady foreground
        a = self.out["afterChange"]
        self.assertEqual((a["checks"], a["logs"], a["first"], a["last"]), (4, 2, self.out["me"], 999999))
        self.assertEqual(self.out["capped"], {"logs": 20, "checks": 64})          # at most 20 lines a job
        self.assertTrue(self.out["logs"][0].startswith("ignored-busy job=j foreign={"))
        r = self.out["receipts"]["ignoredOnPhysical"]
        self.assertEqual(r["ignoredBusy"]["checks"], 64)                          # on any reason's receipt
        self.assertNotIn("foreign", r)
        self.assertTrue(self.out["done"].startswith("IGNORED-BUSY gate: 64 busy/fullscreen checks ignored"))
        self.assertIn("RECLAIMED gate: physical-input", self.out["done"])


NATIVE = r"""
param([string]$GateCs)
$ErrorActionPreference = 'Stop'
Add-Type -Path $GateCs
$p = Get-Process -Id $PID
$created = $p.StartTime.ToFileTimeUtc()
$cases = [ordered]@{
  exact = [Kh2Desk.Gate]::Covers(0, 0, 1920, 1080, 0, 0, 1920, 1080)
  larger = [Kh2Desk.Gate]::Covers(-8, -8, 1928, 1088, 0, 0, 1920, 1080)
  shortRight = [Kh2Desk.Gate]::Covers(0, 0, 1919, 1080, 0, 0, 1920, 1080)
  offsetLeft = [Kh2Desk.Gate]::Covers(1, 0, 1920, 1080, 0, 0, 1920, 1080)
  secondMonitor = [Kh2Desk.Gate]::Covers(1920, 0, 3840, 1080, 1920, 0, 3840, 1080)
  emptyMonitor = [Kh2Desk.Gate]::Covers(0, 0, 10, 10, 0, 0, 0, 0)
  aliveSelf = [Kh2Desk.Gate]::ProvenAlive($PID, $created)
  aliveWrongCreation = [Kh2Desk.Gate]::ProvenAlive($PID, $created + 1)
  aliveZero = [Kh2Desk.Gate]::ProvenAlive(0, 0)
  aliveMissing = [Kh2Desk.Gate]::ProvenAlive(999999, $created)
  coveringRuns = (@([Kh2Desk.Gate]::Covering()).Count -ge 0)
  candOwnedPopup = [Kh2Desk.Gate]::CoverCandidate($true, $false, $false, $true, '#32770')
  candPlain = [Kh2Desk.Gate]::CoverCandidate($true, $false, $false, $false, 'UnityWndClass')
  candHidden = [Kh2Desk.Gate]::CoverCandidate($false, $false, $false, $false, 'UnityWndClass')
  candMinimized = [Kh2Desk.Gate]::CoverCandidate($true, $true, $false, $false, 'UnityWndClass')
  candCloaked = [Kh2Desk.Gate]::CoverCandidate($true, $false, $true, $true, '#32770')
  candProgman = [Kh2Desk.Gate]::CoverCandidate($true, $false, $false, $false, 'Progman')
  candWorkerW = [Kh2Desk.Gate]::CoverCandidate($true, $false, $false, $true, 'WorkerW')
}
# CoverFrom: the whole per-window decision, on hand-built facts (a 1920x1080 monitor).
function Facts($over) {
  $f = New-Object Kh2Desk.Gate+WindowFacts
  $f.Visible = $true; $f.HasRect = $true; $f.HasMonitor = $true; $f.Class = 'UnityWndClass'; $f.Pid = 5840; $f.Monitor = 65537
  $f.Left = 0; $f.Top = 0; $f.Right = 1920; $f.Bottom = 1080; $f.MonLeft = 0; $f.MonTop = 0; $f.MonRight = 1920; $f.MonBottom = 1080
  foreach ($k in $over.Keys) { $f.$k = $over[$k] }
  $f
}
function Row($over) { $c = [Kh2Desk.Gate]::CoverFrom((Facts $over)); if ($c) { [ordered]@{ pid = $c.Pid; owned = $c.Owned; class = $c.Class; monitor = $c.Monitor } } else { $null } }
$cases.fromOwnedForeignPopup = Row @{ HasOwner = $true; Pid = 31338; Class = '#32770' }
$cases.fromPlain = Row @{}
$cases.fromNotCovering = Row @{ Right = 1900 }
$cases.fromNoMonitor = Row @{ HasMonitor = $false }
$cases.fromNoRect = Row @{ HasRect = $false }
$cases.fromHidden = Row @{ Visible = $false }
$cases.fromMinimized = Row @{ Iconic = $true }
$cases.fromCloaked = Row @{ Cloaked = $true; HasOwner = $true }
$cases.fromWorkerW = Row @{ Class = 'WorkerW' }
$cases.fromNull = ($null -eq [Kh2Desk.Gate]::CoverFrom($null))
# CoversFrom over a desktop: our covering KH2, a foreign owned fullscreen popup, a hidden window and the shell's WorkerW.
$desk = New-Object 'System.Collections.Generic.List[Kh2Desk.Gate+WindowFacts]'
$desk.Add((Facts @{})); $desk.Add((Facts @{ HasOwner = $true; Pid = 31338; Class = '#32770' }))
$desk.Add((Facts @{ Visible = $false; Pid = 9 })); $desk.Add((Facts @{ Class = 'WorkerW'; Pid = 4 }))
$cases.desktop = @([Kh2Desk.Gate]::CoversFrom($desk) | ForEach-Object { "$($_.Pid):$($_.Owned)" }) -join ','
$cases.emptyDesktop = @([Kh2Desk.Gate]::CoversFrom((New-Object 'System.Collections.Generic.List[Kh2Desk.Gate+WindowFacts]'))).Count
$cases | ConvertTo-Json -Compress -Depth 4
"""


class NativeGate(unittest.TestCase):
    """Gate.cs compiled for real: the coverage geometry, strict liveness, and Covering() enumerating without error."""

    def test_native_helpers(self):
        with tempfile.TemporaryDirectory() as d:
            h = Path(d) / "native.ps1"
            h.write_text(NATIVE, encoding="utf-8-sig")
            r = subprocess.run(PS + [str(h), str(AGENT.with_name("Gate.cs"))], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        got = json.loads(r.stdout)
        self.assertEqual(got, {"exact": True, "larger": True, "shortRight": False, "offsetLeft": False,
                               "secondMonitor": True, "emptyMonitor": False, "aliveSelf": True,
                               "aliveWrongCreation": False, "aliveZero": False, "aliveMissing": False,
                               "coveringRuns": True, "candOwnedPopup": True, "candPlain": True, "candHidden": False,
                               "candMinimized": False, "candCloaked": False, "candProgman": False, "candWorkerW": False,
                               "fromOwnedForeignPopup": {"pid": 31338, "owned": True, "class": "#32770", "monitor": 65537},
                               "fromPlain": {"pid": 5840, "owned": False, "class": "UnityWndClass", "monitor": 65537},
                               "fromNotCovering": None, "fromNoMonitor": None, "fromNoRect": None, "fromHidden": None,
                               "fromMinimized": None, "fromCloaked": None, "fromWorkerW": None, "fromNull": True,
                               "desktop": "5840:False,31338:True", "emptyDesktop": 0})


if __name__ == "__main__":
    unittest.main()
