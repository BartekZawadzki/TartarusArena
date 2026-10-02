using UnrealBuildTool;

public class ParagonArena : ModuleRules
{
	public ParagonArena(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"GameplayAbilities", "GameplayTags", "GameplayTasks",
			"AIModule", "NavigationSystem", "Niagara", "Json", "JsonUtilities", "RenderCore", "RHI", "Slate", "SlateCore", "Sockets", "NetCore"
		});

		PublicIncludePaths.Add(ModuleDirectory);
	}
}
