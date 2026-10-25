"""VUH-2617 desk gate: fullscreen and foreign-game reclaim receipts name what was in front. Runs agent.ps1's real
Get-ProcessDetail, Get-ForeignActivity and Add-ForeignDetail in Windows PowerShell 5.1, with Kh2Desk.Gate stubbed. The
reclaim rule itself is unchanged: an owned or in-job foreground never counts, and a foreign game or a fullscreen app
that isn't the rig's still does.

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
  }
}
'@
$ast = [Management.Automation.Language.Parser]::ParseFile($Agent, [ref]$null, [ref]$null)
foreach ($name in 'Get-ProcessDetail', 'Get-ForeignActivity', 'Add-ForeignDetail') {
  $fn = $ast.FindAll({ param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name }, $true)
  if (@($fn).Count -ne 1) { throw "agent.ps1 has $(@($fn).Count) $name" }
  . ([scriptblock]::Create($fn[0].Extent.Text))
}
$script:owned = @()
function Get-Owned { $script:owned }
$config = [pscustomobject]@{ foreignGameNames = 'Marvel-Win64-Shipping|KINGDOM HEARTS|r5apex'; foreignGamePaths = '\\steamapps\\common\\'
                             foreignGameExempt = '^RiotClient' }
$job = [pscustomobject]@{ name = 'j'; jobObject = [IntPtr]1; foreignSeconds = 0; foreignDetail = $null }
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
[ordered]@{ me = $me; cases = $out; receipts = $receipts } | ConvertTo-Json -Depth 8 -Compress
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


if __name__ == "__main__":
    unittest.main()
