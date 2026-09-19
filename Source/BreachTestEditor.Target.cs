// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class BreachTestEditorTarget : TargetRules
{
	public BreachTestEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		bOverrideBuildEnvironment = true;
		ExtraModuleNames.Add("BreachTest");
	}
}
