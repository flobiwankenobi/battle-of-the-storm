using UnrealBuildTool;

public class BattleOfTheStormTarget : TargetRules
{
	public BattleOfTheStormTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("BattleOfTheStorm");
	}
}
