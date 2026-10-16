using UnrealBuildTool;

public class InfoScreens : ModuleRules
{
    public InfoScreens(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        PublicDependencyModuleNames.AddRange(new[] {
            "Core", "CoreUObject", "Engine", "FactoryGame", "SML",
            "UMG", "Slate", "SlateCore", "InputCore", "NetCore", "GameplayTags",
            "PhysicsCore", "DeveloperSettings", "GeometryCollectionEngine",
            "AnimGraphRuntime", "AssetRegistry", "NavigationSystem", "AIModule",
            "GameplayTasks", "RenderCore", "CinematicCamera", "Foliage", "AbstractInstance"
        });
    }
}
