using UnrealBuildTool;

public class BattleOfTheStormEditorTarget : TargetRules
{
	public BattleOfTheStormEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("BattleOfTheStorm");
	}
}
