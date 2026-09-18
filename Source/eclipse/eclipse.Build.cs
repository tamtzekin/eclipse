// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class eclipse : ModuleRules
{
	public eclipse(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {"Inkpot",
			"InkPlusPlus",
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
			"RenderCore",
			// UDeveloperSettings — backs UEclipseDemoSettings, which puts the
			// demo start/end flow switches in Project Settings -> Game.
			"DeveloperSettings",
			// Quartz music clock for the dance battle
			"AudioMixer"
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

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
