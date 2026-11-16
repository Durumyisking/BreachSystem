using UnrealBuildTool;

public class BreachSystemEditor : ModuleRules
{
	public BreachSystemEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "BreachSystem", "GeometryCore" });
		PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "AssetTools", "AssetRegistry", "Slate", "SlateCore", "InputCore", "PropertyEditor", "EditorFramework", "ImageCore" });
	}
}
