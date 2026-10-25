// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Tharrn_Game : ModuleRules
{
	public Tharrn_Game(ReadOnlyTargetRules Target) : base(Target)
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
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Tharrn_Game",
			"Tharrn_Game/Variant_Platforming",
			"Tharrn_Game/Variant_Platforming/Animation",
			"Tharrn_Game/Variant_Combat",
			"Tharrn_Game/Variant_Combat/AI",
			"Tharrn_Game/Variant_Combat/Animation",
			"Tharrn_Game/Variant_Combat/Gameplay",
			"Tharrn_Game/Variant_Combat/Interfaces",
			"Tharrn_Game/Variant_Combat/UI",
			"Tharrn_Game/Variant_SideScrolling",
			"Tharrn_Game/Variant_SideScrolling/AI",
			"Tharrn_Game/Variant_SideScrolling/Gameplay",
			"Tharrn_Game/Variant_SideScrolling/Interfaces",
			"Tharrn_Game/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
