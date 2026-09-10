using UnrealBuildTool;

public class Inkpot : ModuleRules
{
	public Inkpot(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// LOCAL PATCH (not upstream) — ECLIPSE 5.8 upgrade.
		// BuildSettingsVersion.V7 promotes -Wunreachable-code to an error.
		// ToGameplayTag() uses a deliberate `for (...) { ...; break; }` to take
		// only the first list entry, which clang flags as a loop whose increment
		// never runs. Scoped to this module so the check stays on everywhere else.
		// Re-apply after any Inkpot update, or drop it if upstream restructures.
		CppCompileWarningSettings.UnreachableCodeWarningLevel = WarningLevel.Warning;

		PublicIncludePaths.AddRange(
			new string[] 
            {
			}
		);
		
		PrivateIncludePaths.AddRange(
			new string[] 
            {
			}
		);
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
                "Core",
                "CoreUObject",
                "Engine",

                "InkPlusPlus",

                "DeveloperSettings",
                "GameplayTags"
            }
        );
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"UMG"
			}
		);
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
			);
		
		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd",
				}
			);
		}
	}
}
