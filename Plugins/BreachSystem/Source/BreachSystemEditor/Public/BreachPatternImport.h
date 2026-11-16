#pragma once

#include "CoreMinimal.h"
#include "Surface/BreachPattern.h"

class UTexture2D;

enum class EBreachPatternImportMode : uint8
{
    IdMap,
    GuideMap,
    Photo
};

struct FBreachPatternImportOptions
{
    EBreachPatternImportMode Mode = EBreachPatternImportMode::IdMap;
    EBreachPatternMode TargetMode = EBreachPatternMode::Regions;
    double Threshold = 0.5;
    int32 MinFeaturePixels = 4;
    double Simplification = 0.002;
};

struct FBreachPatternImportResult
{
    EBreachPatternMode Mode = EBreachPatternMode::None;
    TArray<FBreachPatternRegion> Regions;
    TArray<FBreachPatternGuide> Guides;
    TArray<FString> Warnings;
    FString Error;
};

namespace BreachPatternImport
{
    BREACHSYSTEMEDITOR_API bool Preview(UTexture2D* Texture, const FBreachPatternImportOptions& Options, FBreachPatternImportResult& OutResult);
    BREACHSYSTEMEDITOR_API bool FromPixels(int32 Width, int32 Height, const TArray<FColor>& Pixels, const FBreachPatternImportOptions& Options, FBreachPatternImportResult& OutResult);
}
