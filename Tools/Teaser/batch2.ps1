$tools = Split-Path -Parent $MyInvocation.MyCommand.Path
$sp = if ($env:TEASER_DIR) { $env:TEASER_DIR } else { Join-Path $env:TEMP 'ParagonTeaser' }
$ff = if ($env:FFMPEG) { $env:FFMPEG } else { 'ffmpeg' }   # ffmpeg on the PATH, or its full path in FFMPEG
$takes = @(
  @{ T='hero_c';   S='hero';   A='-ArenaBotMatch -Minutes=20 -Seed=3 -TeaserSeconds=8 -TeaserPreroll=50 -TeaserSlomo=0.5' },
  @{ T='hero_d';   S='hero';   A='-ArenaBotMatch -Minutes=20 -Seed=17 -TeaserSeconds=8 -TeaserPreroll=80 -TeaserSlomo=0.5' },
  @{ T='follow_b'; S='follow'; A='-ArenaBotMatch -Minutes=20 -Seed=21 -TeaserSeconds=10 -TeaserPreroll=60' },
  @{ T='tower_b';  S='tower';  A='-ArenaStartMap=Conquest -ArenaBotMatch -Minutes=30 -Seed=12 -TeaserSeconds=12 -TeaserPreroll=200 -TeaserWait=700' },
  @{ T='fight_d';  S='fight';  A='-ArenaStartMap=Conquest -ArenaBotMatch -Minutes=30 -Seed=4 -TeaserSeconds=10 -TeaserPreroll=120 -TeaserWait=600 -TeaserSlomo=0.6' }
)
foreach ($k in $takes) {
  Remove-Item "$env:LOCALAPPDATA\ParagonArena\Saved\Teaser.txt" -ErrorAction SilentlyContinue
  & "$tools\run_scene.ps1" -Name $k.S -Take $k.T -GameArgs $k.A -TimeoutSec 1500
  if (Test-Path "$env:LOCALAPPDATA\ParagonArena\Saved\Teaser.txt") { Get-Content "$env:LOCALAPPDATA\ParagonArena\Saved\Teaser.txt" | Select-Object -Last 2 }
  & $ff -v error -y -framerate 30 -i "$sp\frames\$($k.T)\MovieFrame%05d.png" -vf "select='not(mod(n\,45))',scale=480:-1,tile=4x2" -frames:v 1 "$sp\sheet_$($k.T).jpg"
}
Write-Output 'BATCH DONE'
