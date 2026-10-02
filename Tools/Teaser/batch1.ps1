$tools = Split-Path -Parent $MyInvocation.MyCommand.Path
$sp = if ($env:TEASER_DIR) { $env:TEASER_DIR } else { Join-Path $env:TEMP 'ParagonTeaser' }
$ff = if ($env:FFMPEG) { $env:FFMPEG } else { 'ffmpeg' }   # ffmpeg on the PATH, or its full path in FFMPEG
$takes = @(
  @{ T='fight_a';  S='fight';  A='-ArenaBotMatch -Minutes=20 -Seed=7 -TeaserSeconds=12 -TeaserPreroll=30' },
  @{ T='hero_a';   S='hero';   A='-ArenaBotMatch -Minutes=20 -Seed=7 -TeaserSeconds=8 -TeaserPreroll=40' },
  @{ T='follow_a'; S='follow'; A='-ArenaBotMatch -Minutes=20 -Seed=11 -TeaserSeconds=10 -TeaserPreroll=45' },
  @{ T='fight_b';  S='fight';  A='-ArenaBotMatch -Minutes=20 -Seed=21 -TeaserSeconds=12 -TeaserPreroll=70' },
  @{ T='hero_b';   S='hero';   A='-ArenaBotMatch -Minutes=20 -Seed=33 -TeaserSeconds=8 -TeaserPreroll=60' },
  @{ T='tower_a';  S='tower';  A='-ArenaStartMap=Conquest -ArenaBotMatch -Minutes=30 -Seed=5 -TeaserSeconds=12 -TeaserPreroll=150 -TeaserWait=600' },
  @{ T='aerial_c'; S='aerial'; A='-ArenaStartMap=Conquest -ArenaBotMatch -Minutes=30 -Seed=5 -TeaserSeconds=10 -TeaserPreroll=25' },
  @{ T='fight_c';  S='fight';  A='-ArenaStartMap=Conquest -ArenaBotMatch -Minutes=30 -Seed=9 -TeaserSeconds=12 -TeaserPreroll=90 -TeaserWait=600' }
)
foreach ($k in $takes) {
  Remove-Item "$env:LOCALAPPDATA\ParagonArena\Saved\Teaser.txt" -ErrorAction SilentlyContinue
  & "$tools\run_scene.ps1" -Name $k.S -Take $k.T -GameArgs $k.A -TimeoutSec 1500
  if (Test-Path "$env:LOCALAPPDATA\ParagonArena\Saved\Teaser.txt") { Get-Content "$env:LOCALAPPDATA\ParagonArena\Saved\Teaser.txt" | Select-Object -Last 2 }
  & $ff -v error -y -framerate 30 -i "$sp\frames\$($k.T)\MovieFrame%05d.png" -vf "select='not(mod(n\,45))',scale=480:-1,tile=4x2" -frames:v 1 "$sp\sheet_$($k.T).jpg"
}
Write-Output 'BATCH DONE'
