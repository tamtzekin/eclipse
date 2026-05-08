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

		// Editor-only dependencies — only linked when building the editor target
		// so we can populate WBP designer trees from C++ via UEclipseUiBuilder.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] {
				"UMGEditor",       // UWidgetBlueprint
				"UnrealEd",        // BlueprintEditor utilities
				"AssetTools",
				"Kismet"
			});
		}

		PublicIncludePaths.AddRange(new string[] {
			"eclipse",
			// Eclipse port (Justin)
			"eclipse/Player",
			"eclipse/NPC",
			"eclipse/Room",
			"eclipse/Subsystems",
			"eclipse/Data",
			"eclipse/Save",
			"eclipse/Items",
			"eclipse/UI",
			// UE template Combat variant — kept for now as a future-features
			// reference (melee, AI enemies, hit reactions, health UI).
			// Not referenced by gameplay; safe to remove later if unused.
			"eclipse/Variant_Combat",
			"eclipse/Variant_Combat/AI",
			"eclipse/Variant_Combat/Animation",
			"eclipse/Variant_Combat/Gameplay",
			"eclipse/Variant_Combat/Interfaces",
			"eclipse/Variant_Combat/UI"
		});

		// Articy plugin runtime — uncomment after the Articy Importer plugin is
		// installed locally (Marketplace) and confirmed in the .uproject.
		// PublicDependencyModuleNames.Add("ArticyRuntime");

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
