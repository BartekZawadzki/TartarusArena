using UnrealBuildTool;

public class ParagonArenaTarget : TargetRules
{
	public ParagonArenaTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("ParagonArena");
	}
}
