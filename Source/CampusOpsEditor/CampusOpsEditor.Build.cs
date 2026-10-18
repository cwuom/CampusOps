using UnrealBuildTool;

public class CampusOpsEditor : ModuleRules
{
	public CampusOpsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"AssetTools",
			"AssetRegistry",
			"MaterialEditor",
			"NavigationSystem",
			"ImageCore",
			"PhysicsCore",
			"SlateCore",
			"MeshDescription",
			"StaticMeshDescription",
			"CampusOps"
		});
	}
}
