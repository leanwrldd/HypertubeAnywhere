using UnrealBuildTool;

public class HypertubeAnywhere : ModuleRules
{
	public HypertubeAnywhere(ReadOnlyTargetRules Target) : base(Target)
	{
		CppStandard = CppStandardVersion.Cpp20;
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bLegacyPublicIncludePaths = false;

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"EnhancedInput",
				"NetCore",
				"FactoryGame",
				"SML"
			}
		);
	}
}
