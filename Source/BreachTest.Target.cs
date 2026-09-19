// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class BreachTestTarget : TargetRules
{
	public BreachTestTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		bOverrideBuildEnvironment = true;
		ExtraModuleNames.Add("BreachTest");
	}
}
