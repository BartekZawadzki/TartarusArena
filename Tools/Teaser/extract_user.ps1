# the operator's screen recordings (2558x1438, 30 fps, with the HUD) cut into PNG takes at 1920x1080 for edit.py:
# a centre crop zoomed 1.3x hides the HUD (the score bar off the top, the ability bar and minimap under the 2.39:1 bars)
$ff = if ($env:FFMPEG) { $env:FFMPEG } else { 'ffmpeg' }   # ffmpeg on the PATH, or its full path in FFMPEG
$root = if ($env:TEASER_DIR) { $env:TEASER_DIR } else { Join-Path $env:TEMP 'ParagonTeaser' }
# the two screen recordings to cut from (2558x1438, 30 fps): $env:TEASER_REC1, $env:TEASER_REC2
$r1 = $env:TEASER_REC1
$r2 = $env:TEASER_REC2
if (-not $r1 -or -not $r2 -or -not (Test-Path $r1) -or -not (Test-Path $r2)) { Write-Output 'set TEASER_REC1 and TEASER_REC2 to the recordings'; exit 2 }
$Z13 = 'crop=1968:1106:295:166,scale=1920:1080:flags=lanczos'
$Z10 = 'scale=1920:1080:flags=lanczos'
$cuts = @(
  @('u1_roster', $r1, 86.5, 2.6, $Z10),
  @('u1_lineup', $r1, 91.5, 3.0, $Z13),
  @('u1_purple', $r1, 18.5, 3.0, $Z13),
  @('u1_dkill',  $r1, 21.5, 3.5, $Z13),
  @('u1_cube',   $r1, 50.0, 3.0, $Z13),
  @('u1_fire',   $r1, 101.5, 3.5, $Z13),
  @('u1_wraith', $r1, 107.0, 3.0, $Z13),
  @('u2_beast',  $r2, 202.5, 3.5, $Z13),
  @('u2_bolt',   $r2, 210.5, 3.0, $Z13),
  @('u2_bolt2',  $r2, 240.5, 3.0, $Z13),
  @('u2_beast2', $r2, 243.5, 3.5, $Z13),
  @('u2_beam',   $r2, 103.5, 2.5, $Z13),
  @('u2_laser',  $r2, 106.5, 3.5, $Z13)
)
foreach ($c in $cuts) {
  $out = Join-Path $root "frames\$($c[0])"
  if (Test-Path $out) { Remove-Item $out -Recurse -Force }
  New-Item -ItemType Directory -Force $out | Out-Null
  & $ff -v error -y -threads 4 -ss $c[2] -t $c[3] -i $c[1] -vf $c[4] -start_number 0 "$out\MovieFrame%05d.png"
  Write-Output ("CUT {0} frames={1}" -f $c[0], (Get-ChildItem $out -Filter *.png | Measure-Object).Count)
}
