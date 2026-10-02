# the short teaser's extra windows from the operator's first recording (2558x1438, 30 fps, with the HUD)
$ff = if ($env:FFMPEG) { $env:FFMPEG } else { 'ffmpeg' }   # ffmpeg on the PATH, or its full path in FFMPEG
$root = if ($env:TEASER_DIR) { $env:TEASER_DIR } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
# the screen recording to cut from (2558x1438, 30 fps): $env:TEASER_REC1
$r1 = $env:TEASER_REC1
if (-not $r1 -or -not (Test-Path $r1)) { Write-Output 'set TEASER_REC1 to the recording'; exit 2 }
$Z13 = 'crop=1968:1106:295:166,scale=1920:1080:flags=lanczos'
$Z10 = 'scale=1920:1080:flags=lanczos'
$cuts = @(
  @('s_count', 89.0, 3.0, $Z13),   # the line-up during "Fight in 3, 2, 1"
  @('s_ring',  101.5, 6.0, $Z13),  # the fire ring, the wraith, the fire beam
  @('s_shop',  30.8, 3.1, $Z10),   # the shop open mid-match (a panel: uncropped)
  @('s_g2a',   37.2, 3.2, $Z13),   # a fight with effects
  @('s_g2b',   68.6, 3.2, $Z13),   # the beam
  @('s_g2c',   72.4, 2.8, $Z13)    # the purple lightning
)
foreach ($c in $cuts) {
  $out = Join-Path $root "frames\$($c[0])"
  if (Test-Path $out) { Get-ChildItem $out -Filter *.png | Remove-Item -Force } else { New-Item -ItemType Directory -Force $out | Out-Null }
  & $ff -v error -y -threads 4 -ss $c[1] -t $c[2] -i $r1 -vf $c[3] -start_number 0 "$out\MovieFrame%05d.png"
  Write-Output ("CUT {0} frames={1}" -f $c[0], (Get-ChildItem $out -Filter *.png | Measure-Object).Count)
}
