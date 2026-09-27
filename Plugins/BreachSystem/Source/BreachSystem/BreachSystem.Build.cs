// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class BreachSystem : ModuleRules
{
	public BreachSystem(ReadOnlyTargetRules Target) : base(Target)
	{
		CppStandard = CppStandardVersion.Cpp20;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core", "CoreUObject", "GeometryCore", "DeveloperSettings",
				// ... add other public dependencies that you statically link with here ...
			}
			);


        PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Engine",
				"Slate",
				"SlateCore",

				"GeometryFramework",
				"GeometryCore",
				"DynamicMesh",
				"MeshDescription",
				"StaticMeshDescription",
				"MeshConversion", // FMeshDescriptionToDynamicMesh 사용에 필요
				"MeshConversionEngineTypes",

			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
