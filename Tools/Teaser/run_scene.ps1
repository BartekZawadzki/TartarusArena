param(
  [Parameter(Mandatory=$true)][string]$Name,
  [Parameter(Mandatory=$true)][string]$GameArgs,
  [int]$TimeoutSec = 900,
  [string]$Take = ""
)
$ErrorActionPreference = 'Stop'
# the working folder (frames, titles, music, the edit): $env:TEASER_DIR, else %TEMP%\ParagonTeaser
$root = if ($env:TEASER_DIR) { $env:TEASER_DIR } else { Join-Path $env:TEMP 'ParagonTeaser' }
# the packaged game: $env:PARAGON_EXE, else the repository's Build folder
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$exe = if ($env:PARAGON_EXE) { $env:PARAGON_EXE } else { Join-Path $repo 'Build\Windows\ParagonArena\Binaries\Win64\ParagonArena-Win64-Shipping.exe' }
$saved = Join-Path $env:LOCALAPPDATA 'ParagonArena\Saved'
$shots = Join-Path $saved 'Screenshots\Windows'
$gus = Join-Path $saved 'Config\Windows\GameUserSettings.ini'
$gusBak = Join-Path $root 'GameUserSettings.ini.bak'
if (-not $Take) { $Take = $Name }
$out = Join-Path $root "frames\$Take"

if (Get-Process ParagonArena-Win64-Shipping -ErrorAction SilentlyContinue) { Write-Output "SCENE $Name SKIP game already running"; exit 2 }
if (Test-Path $shots) { Get-ChildItem $shots -Filter 'MovieFrame*.png' | Remove-Item -Force }
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force $out | Out-Null

$all = "-windowed -ResX=1920 -ResY=1080 -WinX=40 -WinY=40 -benchmark -fps=30 -nosound -ArenaTeaser=$Name $GameArgs"
$t0 = Get-Date
$p = Start-Process -FilePath $exe -ArgumentList $all -PassThru
$done = $p.WaitForExit($TimeoutSec * 1000)
if (-not $done) { Stop-Process -Id $p.Id -Force; Start-Sleep -Milliseconds 500 }
$secs = [int]((Get-Date) - $t0).TotalSeconds

# restore the operator's settings exactly
if (Test-Path $gusBak) { Copy-Item $gusBak $gus -Force }

$frames = @()
if (Test-Path $shots) { $frames = Get-ChildItem $shots -Filter 'MovieFrame*.png' | Sort-Object Name }
foreach ($f in $frames) { Move-Item $f.FullName $out }
Write-Output ("SCENE {0} take=$Take exit={1} timedout={2} secs={3} frames={4}" -f $Name, $(if ($done) { $p.ExitCode } else { 'killed' }), (-not $done), $secs, $frames.Count)
