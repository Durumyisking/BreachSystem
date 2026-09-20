// Fill out your copyright notice in the Description page of Project Settings.


#include "BreachSourceComponent.h"

#include "BreachSystemLog.h"
#include "Shape/BreachShape.h"

void UBreachSourceComponent::CacheCutterMesh()
{
	if (BreachSourceOption.ShapeClass)
	{
		if (UBreachShape* BreachShapeCDO = BreachSourceOption.ShapeClass->GetDefaultObject<UBreachShape>())
		{
			BreachShapeCDO->GenerateMesh(BreachSourceOption, CachedCutterMesh);
		}
		else
		{
			SWARN(TEXT("BreachSourceOption.ShapeCDO is Not Valid"));
		}
	}
	else
	{
		SWARN(TEXT("BreachSourceOption.Shape is Not Valid"));
	}
}

