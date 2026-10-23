param([string]$Commit = '')
# bld (VUH-2339): put PC1's governed products for one commit into C:/kh2rig/wt/build, unchanged.
# Run over SSH after copying C:/kh2rig/products/<commit>/ from PC1 and checking out that commit:
#   powershell -NoProfile -File C:/kh2rig/ops/stage.ps1 -Commit <full sha>
# Refuses while the desk lease is held or any owned game from this checkout is running.
$ErrorActionPreference='Stop'
$root='C:/kh2rig/wt'
if($Commit -notmatch '^[0-9a-f]{40}$'){throw 'full commit sha required'}
if(Test-Path -LiteralPath 'C:/desk/gate/lease.json'){throw 'desk lease held; no staging during a run'}
$head=& 'C:/Program Files/Git/cmd/git.exe' -C $root rev-parse HEAD
if($head -ne $Commit){throw "checkout is at $head, not $Commit"}
if(@(& 'C:/Program Files/Git/cmd/git.exe' -C $root status --porcelain --untracked-files=no).Count){throw 'tracked files modified'}
$src="C:/kh2rig/products/$Commit"
$built=Get-Content -Raw -LiteralPath "$src/products.json" | ConvertFrom-Json
if($built.commit -ne $Commit){throw 'products.json names another commit'}
$owned="$root/build/rig/owned.txt"
if(Test-Path -LiteralPath $owned){
 foreach($row in @(Get-Content -LiteralPath $owned)){ $f=$row -split ' '; $p=Get-Process -Id ([int]$f[0]) -ErrorAction SilentlyContinue
  if($p -and $p.StartTime.ToFileTimeUtc() -eq [long]$f[1]){throw "owned game $($f[0]) running"} }
}
foreach($row in $built.products){
 $from=Join-Path $src $row.path
 if((Get-FileHash -LiteralPath $from -Algorithm SHA256).Hash.ToLowerInvariant() -ne $row.sha256){throw "shipped copy differs from PC1's build: $($row.path)"}
 $to=Join-Path $root $row.path
 New-Item -ItemType Directory -Force (Split-Path -Parent $to) | Out-Null
 Copy-Item -LiteralPath $from -Destination $to -Force
 if((Get-FileHash -LiteralPath $to -Algorithm SHA256).Hash.ToLowerInvariant() -ne $row.sha256){throw "staged copy differs: $($row.path)"}
}
"staged $($built.products.Count) products for $Commit"
