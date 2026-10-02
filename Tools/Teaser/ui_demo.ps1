# the exe's own UI demo (menus, hero browser, settings, match HUD, shop, death, end) as screenshots in <dir>\ui_en;
# the operator's GameUserSettings.ini is restored from <dir>\GameUserSettings.ini.bak (make it first)
$root = if ($env:TEASER_DIR) { $env:TEASER_DIR } else { Join-Path $env:TEMP 'ParagonTeaser' }
# the packaged game: $env:PARAGON_EXE, else the repository's Build folder
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$exe = if ($env:PARAGON_EXE) { $env:PARAGON_EXE } else { Join-Path $repo 'Build\Windows\ParagonArena\Binaries\Win64\ParagonArena-Win64-Shipping.exe' }
$saved = Join-Path $env:LOCALAPPDATA 'ParagonArena\Saved'
$shots = Join-Path $saved 'Screenshots\Windows'
$gus = Join-Path $saved 'Config\Windows\GameUserSettings.ini'
$out = Join-Path $root 'ui_en'
if (Get-Process ParagonArena-Win64-Shipping -ErrorAction SilentlyContinue) { Write-Output 'UI SKIP game running'; exit 2 }
if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Force $out | Out-Null
if (Test-Path $shots) { Get-ChildItem $shots -Filter '*.png' | Remove-Item -Force }
$p = Start-Process -FilePath $exe -ArgumentList '-windowed -ResX=1920 -ResY=1080 -WinX=40 -WinY=40 -nosound -ArenaUIDemo -Seed=4' -PassThru
$done = $p.WaitForExit(600000)
if (-not $done) { Stop-Process -Id $p.Id -Force; Start-Sleep -Milliseconds 500 }
Copy-Item (Join-Path $root 'GameUserSettings.ini.bak') $gus -Force
$n = 0
if (Test-Path $shots) { Get-ChildItem $shots -Filter '*.png' | ForEach-Object { Move-Item $_.FullName $out; $n++ } }
Write-Output ("UI exit={0} shots={1}" -f $(if ($done) { $p.ExitCode } else { 'killed' }), $n)
