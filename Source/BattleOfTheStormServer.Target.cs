using UnrealBuildTool;

// Eigenstaendiger dedizierter Server. Laesst sich nur mit einer aus dem Quellcode gebauten Engine bauen (TDD Abschnitt 3).
public class BattleOfTheStormServerTarget : TargetRules
{
	public BattleOfTheStormServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("BattleOfTheStorm");
	}
}
