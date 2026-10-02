using UnrealBuildTool;

public class ParagonArenaEditorTarget : TargetRules
{
	public ParagonArenaEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("ParagonArena");
	}
}
