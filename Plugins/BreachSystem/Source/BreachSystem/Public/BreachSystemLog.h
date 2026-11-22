// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// 플러그인의 모든 로그 카테고리를 여기서 선언하고 BreachSystemLog.cpp에서 정의한다. 로그는 UE_LOG로만 남긴다.
// 테스트가 LogBreachableSurface 이름으로 로그를 걸러 보거나 Verbose를 켜므로 이름을 바꾸면 테스트도 함께 고친다.

DECLARE_LOG_CATEGORY_EXTERN(LogBreachableSurface, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogBreachSource, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogBreachSimpleCollision, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogBreachDecoration, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogBreachDecorationGeometry, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogBreachFeatureGeometry, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogBreachDebris, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogBreachSystemSettings, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogBreachDebug, Log, All);
