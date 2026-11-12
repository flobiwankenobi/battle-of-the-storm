using UnrealBuildTool;

public class BattleOfTheStorm : ModuleRules
{
	public BattleOfTheStorm(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"GameplayAbilities", "GameplayTags", "GameplayTasks",
			"AIModule", "Json", "JsonUtilities", "NetCore"
		});

		PublicIncludePaths.Add("BattleOfTheStorm");
	}
}
