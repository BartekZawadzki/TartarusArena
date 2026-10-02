# Registers the DF toolset with the Toolset Registry at editor startup (same pattern as Epic's EditorToolset).
# Guarded: in -game runs of the editor binary the Toolset Registry is not loaded (no unreal.ToolsetDefinition).
import unreal

if hasattr(unreal, "ToolsetDefinition"):
    from df_toolset import toolsets

    toolsets._registration.register()
