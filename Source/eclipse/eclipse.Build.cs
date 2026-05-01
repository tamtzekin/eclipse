// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class eclipse : ModuleRules
{
	public eclipse(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore",
			"NavigationSystem",
			"GameplayTasks"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"RenderCore"
		});

		PublicIncludePaths.AddRange(new string[] {
			"eclipse",
			// Eclipse port (Justin) — added Phase 0
			"eclipse/Player",
			"eclipse/NPC",
			"eclipse/Room",
			"eclipse/Subsystems",
			"eclipse/Data",
			"eclipse/Save",
			// Template variant code — left in for now, can be deleted with the
			// matching .cpp/.h files once we no longer need it as reference.
			"eclipse/Variant_Platforming",
			"eclipse/Variant_Platforming/Animation",
			"eclipse/Variant_Combat",
			"eclipse/Variant_Combat/AI",
			"eclipse/Variant_Combat/Animation",
			"eclipse/Variant_Combat/Gameplay",
			"eclipse/Variant_Combat/Interfaces",
			"eclipse/Variant_Combat/UI",
			"eclipse/Variant_SideScrolling",
			"eclipse/Variant_SideScrolling/AI",
			"eclipse/Variant_SideScrolling/Gameplay",
			"eclipse/Variant_SideScrolling/Interfaces",
			"eclipse/Variant_SideScrolling/UI"
		});

		// Articy plugin runtime — uncomment after the Articy Importer plugin is
		// installed locally (Marketplace) and confirmed in the .uproject.
		// PublicDependencyModuleNames.Add("ArticyRuntime");

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
