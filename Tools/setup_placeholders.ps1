# Copies the engine's own template content used as placeholders until the Paragon packs are added.
# Everything comes from the local UE 5.8 install (no download). Idempotent.
param([string]$Engine = 'C:\Program Files\Epic Games\UE_5.8')
$T = Join-Path $Engine 'Templates'
$C = Join-Path $PSScriptRoot '..\Content'
robocopy "$T\TemplateResources\High\Characters\Content\Mannequins" "$C\Characters\Mannequins" /E /NFL /NDL /NJH /NJS /NP | Out-Null
robocopy "$T\TemplateResources\High\LevelPrototyping\Content" "$C\LevelPrototyping" /E /NFL /NDL /NJH /NJS /NP | Out-Null
foreach ($d in 'Anims','VFX','Materials') { robocopy "$T\TP_ThirdPerson\Content\Variant_Combat\$d" "$C\Variant_Combat\$d" /E /NFL /NDL /NJH /NJS /NP | Out-Null }
# only the VFX: ABP_Manny_Platforming / AM_Dash depend on the template's C++ PlatformingCharacter and break the cook
foreach ($d in @('VFX')) { robocopy "$T\TP_ThirdPerson\Content\Variant_Platforming\$d" "$C\Variant_Platforming\$d" /E /NFL /NDL /NJH /NJS /NP | Out-Null }
"PLACEHOLDERS OK"
