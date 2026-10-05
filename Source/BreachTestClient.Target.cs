// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class BreachTestClientTarget : TargetRules
{
	public BreachTestClientTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Client;

        ExtraModuleNames.Add("BreachTest");
	}
}
