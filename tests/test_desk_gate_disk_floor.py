"""VUH-2617 desk gate disk floor: tools/rig/desk_gate/agent.ps1's real Get-MinFreeGB and Get-Refusals, run in
Windows PowerShell 5.1 against config.json text as the agent parses it. Absent minFreeGB keeps 10 GB; a finite positive
number sets the floor; anything else refuses every launch (disk-config), even with the disk empty or full (Sage,
VUH-2617 61a794d6). Everything else the refusal check reads is stubbed clean, so a case's refusals are only the disk's.

    python -B -m unittest discover -s tests -p test_desk_gate_disk_floor.py -v
"""
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AGENT = ROOT / "tools" / "rig" / "desk_gate" / "agent.ps1"
GB = 1 << 30

HARNESS = r"""
param([string]$Agent, [string]$Cases, [string]$Dir)
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
namespace Kh2Desk {
  public static class Gate {
    public static bool HooksOk = true;
    public static long Now() { return 100000000; }
    public static int ActiveConsoleSession() { return 1; }
    public static string InputDesktop() { return "Default"; }
    public static int NotificationState() { return 5; }
    public static bool Under(string dir) { return false; }
  }
}
'@
$ast = [Management.Automation.Language.Parser]::ParseFile($Agent, [ref]$null, [ref]$null)
foreach ($name in 'Get-MinFreeGB', 'Get-Refusals') {
  $fn = $ast.FindAll({ param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name }, $true)
  if (@($fn).Count -ne 1) { throw "agent.ps1 has $(@($fn).Count) $name" }
  . ([scriptblock]::Create($fn[0].Extent.Text))
}
function Get-IdleBase { 0 }
function Get-Owned { @() }
function Get-ForeignActivity($Job) { $null }
function Expand([string]$Path) { $Path }
function Log([string]$Text) { }
function Get-ItemProperty { [pscustomobject]@{ pid = 4242; ActiveUser = 249968895 } }
function Get-Process { [pscustomobject]@{ Id = 4242; ProcessName = 'steam' } }
function Get-PSDrive { [pscustomobject]@{ Free = $script:free } }
$session = 1; $idleMs = 0; $gameDir = 'C:\nowhere\'
$stopPath = "$Dir\stop"; $leasePath = "$Dir\lease.json"; $verifyPath = "$Dir\needs-verify.json"
$out = foreach ($case in (Get-Content -Raw $Cases | ConvertFrom-Json)) {
  $config = $case.config | ConvertFrom-Json   # the agent's own parse of config.json
  $config | Add-Member steamAccountId 249968895 -Force
  $config | Add-Member laneLocks @("$Dir\no-lock") -Force
  $config | Add-Member requireUnlocked $true -Force
  $minFreeGB = Get-MinFreeGB $config
  $script:free = [long]$case.free
  [pscustomobject]@{ config = $case.config; free = $case.free; floor = $minFreeGB
                     launch = @(Get-Refusals 'launch'); desk = @(Get-Refusals 'desk') }
}
ConvertTo-Json @($out) -Depth 4 -Compress
"""

PS = ["powershell", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File"]


def run(cases):
    with tempfile.TemporaryDirectory() as d:
        harness = Path(d) / "harness.ps1"
        harness.write_text(HARNESS, encoding="utf-8-sig")
        cases_path = Path(d) / "cases.json"
        cases_path.write_text(json.dumps([{"config": c, "free": f} for c, f in cases]), encoding="utf-8")
        out = subprocess.run(PS + [str(harness), str(AGENT), str(cases_path), d], capture_output=True, text=True)
        if out.returncode:
            raise AssertionError(out.stderr or out.stdout)
        return json.loads(out.stdout)


class DiskFloor(unittest.TestCase):
    def check(self, cases, launch, floor=None):
        for r, (config, free) in zip(run(cases), cases):
            with self.subTest(config=config, free=free):
                self.assertEqual(r["launch"], launch(free), r)
                self.assertEqual(r["desk"], [], r)  # desk jobs never look at the disk
                if floor is not None:
                    self.assertEqual(r["floor"], floor, r)

    def test_absent_key_keeps_ten(self):
        self.check([("{}", 0), ("{}", int(9.99 * GB)), ("{}", 10 * GB), ("{}", 500 * GB)],
                   lambda free: ["disk"] if free < 10 * GB else [], floor=10)

    def test_pc1_profile_twenty(self):
        pc1 = (ROOT / "tools" / "rig" / "desk_gate" / "config.pc1.json").read_text(encoding="utf-8")
        self.check([(pc1, 14 * GB), (pc1, int(19.99 * GB)), (pc1, 20 * GB), (pc1, 42 * GB)],
                   lambda free: ["disk"] if free < 20 * GB else [], floor=20)

    def test_fractional_and_integer_forms(self):
        self.check([('{"minFreeGB": 12.5}', 12 * GB), ('{"minFreeGB": 12.5}', 13 * GB)],
                   lambda free: ["disk"] if free < 12.5 * GB else [], floor=12.5)
        self.check([('{"minFreeGB": 30}', 29 * GB), ('{"minFreeGB": 30}', 31 * GB)],
                   lambda free: ["disk"] if free < 30 * GB else [], floor=30)

    def test_malformed_values_fail_closed(self):
        bad = ['-1', '0', '-0.5', '"NaN"', '"Infinity"', '"bad"', '"20"', 'true', 'false', 'null', '[]', '[20]',
               '{}', '{"gb": 20}']
        cases = [(f'{{"minFreeGB": {v}}}', free) for v in bad for free in (0, 2000 * GB)]
        self.check(cases, lambda free: ["disk-config"], floor=None)
        for r in run(cases):
            self.assertIsNone(r["floor"], r)


class MinFreeGBDirect(unittest.TestCase):
    """Values JSON can't carry (NaN, infinities) reach Get-MinFreeGB only from a hand-built object."""

    def test_non_finite_doubles(self):
        script = (f"$a = [Management.Automation.Language.Parser]::ParseFile('{AGENT}', [ref]$null, [ref]$null)\n"
                  "$f = $a.FindAll({ param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and "
                  "$n.Name -eq 'Get-MinFreeGB' }, $true)[0]\n"
                  ". ([scriptblock]::Create($f.Extent.Text))\n"
                  "foreach ($v in [double]::NaN, [double]::PositiveInfinity, [double]::NegativeInfinity, [double]20) {\n"
                  "  $r = Get-MinFreeGB ([pscustomobject]@{ minFreeGB = $v }); if ($null -eq $r) { 'null' } else { $r } }\n")
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "direct.ps1"
            path.write_text(script, encoding="utf-8-sig")
            out = subprocess.run(PS + [str(path)], capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stderr)
        self.assertEqual(out.stdout.split(), ["null", "null", "null", "20"])


if __name__ == "__main__":
    unittest.main()
