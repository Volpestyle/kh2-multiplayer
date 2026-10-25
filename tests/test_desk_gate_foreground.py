"""VUH-2617 desk gate: fullscreen and foreign-game reclaim receipts name what was in front, and busy/fullscreen states
caused by the rig's own focus-less KH2 are ignored but recorded (bld03c: Discord in front, our game owned). Runs
agent.ps1's real Get-ProcessDetail, Get-ForeignActivity, Add-ForeignDetail, Add-IgnoredBusy and Finish-Job in Windows
PowerShell 5.1, with Kh2Desk.Gate stubbed. An owned or in-job foreground never counts; a foreign game window always
counts; states 2/3 count only when no owned game is live; presentation (4) and quiet time (7) always count.

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
  }
}
'@
$ast = [Management.Automation.Language.Parser]::ParseFile($Agent, [ref]$null, [ref]$null)
foreach ($name in 'Get-ProcessDetail', 'Get-ForeignActivity', 'Add-ForeignDetail', 'Add-IgnoredBusy', 'Finish-Job') {
  $fn = $ast.FindAll({ param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name }, $true)
  if (@($fn).Count -ne 1) { throw "agent.ps1 has $(@($fn).Count) $name" }
  . ([scriptblock]::Create($fn[0].Extent.Text))
}
$script:owned = @()
function Get-Owned { $script:owned }
$script:logs = @()
function Log([string]$Text) { $script:logs += $Text }
$root = Join-Path $env:TEMP ('gatefg-' + [guid]::NewGuid()); New-Item -ItemType Directory -Force "$root\done" | Out-Null
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
# Owned-game busy states (bld03c): our KH2 owned and live, someone else's window in front.
[Kh2Desk.Gate]::Windows.Clear(); [Kh2Desk.Gate]::Front = $me
$kh2 = [pscustomobject]@{ pid = 5840; unreadable = $false }
$busy = @()
function BusyCase($label, $state, $owned) { [Kh2Desk.Gate]::State = $state; $script:owned = $owned
  $a = Get-ForeignActivity $job; [pscustomobject]@{ label = $label; activity = $a; detail = $script:foreignDetail; ignored = $script:ignoredBusy } }
$busy += BusyCase 'owned-busy-2' 2 @($kh2)
$busy += BusyCase 'owned-d3d-3' 3 @($kh2)
$busy += BusyCase 'owned-presentation-4' 4 @($kh2)
$busy += BusyCase 'owned-quiet-7' 7 @($kh2)
$busy += BusyCase 'unowned-busy-2' 2 @()
$busy += BusyCase 'unreadable-only-busy-2' 2 @([pscustomobject]@{ pid = 0; unreadable = $true })
[Kh2Desk.Gate]::Windows.Add((Win 4242 'r5apex' 'C:\Games\Apex\r5apex.exe'))
$busy += BusyCase 'owned-busy-with-foreign-game' 2 @($kh2)
[Kh2Desk.Gate]::Windows.Clear()
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
        for label, state in (("owned-busy-2", 2), ("owned-d3d-3", 3)):
            with self.subTest(label=label):
                c = self.busy(label)
                self.assertIsNone(c["activity"])
                self.assertIsNone(c["detail"])
                self.assertEqual(c["ignored"]["activity"], "ignored-busy")
                self.assertEqual(c["ignored"]["notificationState"], state)
                self.assertEqual(c["ignored"]["foreground"]["pid"], self.out["me"])
                self.assertEqual(c["ignored"]["ownedPids"], [5840])

    def test_still_reclaims(self):
        expect = {"owned-presentation-4": "fullscreen", "owned-quiet-7": "fullscreen", "unowned-busy-2": "fullscreen",
                  "unreadable-only-busy-2": "fullscreen", "owned-busy-with-foreign-game": "foreign-game"}
        for label, activity in expect.items():
            with self.subTest(label=label):
                c = self.busy(label)
                self.assertEqual(c["activity"], activity)
                self.assertIsNone(c["ignored"])
                self.assertEqual(c["detail"]["activity"], activity)

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


if __name__ == "__main__":
    unittest.main()
