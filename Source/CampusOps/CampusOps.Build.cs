using System.IO;
using System.Linq;
using UnrealBuildTool;

public class CampusOps : ModuleRules
{
	public CampusOps(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Files keep their helpers in anonymous namespaces with short names (H, White, Hold...). A unity build glues
		// several files together, so a local in one file then "hides" another file's helper; that is harmless, but it
		// is an error by default and breaks clean builds (adaptive unity only hides it for files with local changes).
		// The engine's shared PCHs are compiled with the error switched on and carry it into every file using them, so
		// the module has its own PCH.
		CppCompileWarningSettings.ShadowVariableWarningLevel = WarningLevel.Warning;
		PrivatePCHHeaderFile = "CampusOpsPCH.h";

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"NavigationSystem",
			"AnimGraphRuntime",
			"AnimationCore",
			"PhysicsCore",
			"RenderCore",
			"RHI",
			"NetCore",
			"DeveloperSettings",
			"UMG",
			"Slate",
			"SlateCore",
			"ProceduralMeshComponent"
		});

		PublicIncludePaths.AddRange(new string[]
		{
			"CampusOps"
		});

		// NVIDIA DLSS super resolution, DLSS frame generation and Reflex (optional project plugins, Windows only; the public
		// repository does not ship them).
		string PluginsDir = Path.Combine(ModuleDirectory, "..", "..", "Plugins");
		bool bHasNvidia = new string[] { "DLSS", "StreamlineCore", "StreamlineDLSSG", "StreamlineReflex", "StreamlineNGXCommon" }
			.All(Name => Directory.Exists(Path.Combine(PluginsDir, Name)));
		if (Target.Platform == UnrealTargetPlatform.Win64 && bHasNvidia)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"DLSSBlueprint",
				"StreamlineDLSSGBlueprint",
				"StreamlineReflexBlueprint"
			});
			PublicDefinitions.Add("CAMPUS_WITH_DLSS=1");
		}
		else
		{
			PublicDefinitions.Add("CAMPUS_WITH_DLSS=0");
		}
	}
}
