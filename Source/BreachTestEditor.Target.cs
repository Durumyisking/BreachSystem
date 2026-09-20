// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class BreachTestEditorTarget : TargetRules
{
	public BreachTestEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

        // Disable specific warning C4459 (variable name hiding in UE 5.6 engine code)
        bWarningsAsErrors = false;
        AdditionalCompilerArguments = "/wd4459 /wd4996";
        bOverrideBuildEnvironment = true;


        ExtraModuleNames.Add("BreachTest");
	}
}
