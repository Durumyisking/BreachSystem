#include "BreachPatternImport.h"
#include "Misc/AutomationTest.h"
#include "Engine/Texture2D.h"
#include "UObject/UObjectGlobals.h"

// 테스트 전용: raw source 픽셀에서 import topology와 staging을 검증한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternImportTest, "BreachSystem.Pattern.Import", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternImportTest::RunTest(const FString& Parameters)
{
    FBreachPatternImportOptions Options;
    Options.MinFeaturePixels = 1;
    FBreachPatternImportResult Result;
    TArray<FColor> Pixels;
    Pixels.Init(FColor::Red, 25);
    Pixels[12] = FColor::Blue;
    TestTrue(TEXT("Exact RGB map imports"), BreachPatternImport::FromPixels(5, 5, Pixels, Options, Result));
    TestEqual(TEXT("Two region colors"), Result.Regions.Num(), 2);
    int32 Holes = 0;
    for (const FBreachPatternRegion& Region : Result.Regions)
    {
        Holes += Region.Holes.Num();
    }
    TestEqual(TEXT("Surrounded ID retained as hole"), Holes, 1);

    Pixels.Init(FColor(0, 0, 0, 0), 25);
    Pixels[2 * 5] = FColor::Red;
    Pixels[2 * 5 + 4] = FColor::Red;
    TestTrue(TEXT("Periodic region imports"), BreachPatternImport::FromPixels(5, 5, Pixels, Options, Result));
    TestEqual(TEXT("Seam neighbors share one unwrapped region"), Result.Regions.Num(), 1);
    if (Result.Regions.Num() == 1)
    {
        double MinX = 10;
        double MaxX = -10;
        for (const FVector2D& Point : Result.Regions[0].Outer)
        {
            MinX = FMath::Min(MinX, Point.X);
            MaxX = FMath::Max(MaxX, Point.X);
        }
        TestTrue(TEXT("Unwrapped seam remains two pixels wide"), FMath::IsNearlyEqual(MaxX - MinX, .4));
    }

    Pixels.Init(FColor(0, 0, 0, 0), 25);
    for (int32 Y = 0; Y < 5; ++Y)
    {
        Pixels[Y * 5] = FColor::Red;
        Pixels[Y * 5 + 4] = FColor::Red;
    }
    TestTrue(TEXT("A Y-winding stripe imports"), BreachPatternImport::FromPixels(5, 5, Pixels, Options, Result));
    TestEqual(TEXT("Y winding preserves a single X-seam region identity"), Result.Regions.Num(), 1);
    if (Result.Regions.Num() == 1)
    {
        double MinX = 10;
        double MaxX = -10;
        for (const FVector2D& Point : Result.Regions[0].Outer)
        {
            MinX = FMath::Min(MinX, Point.X);
            MaxX = FMath::Max(MaxX, Point.X);
        }
        TestTrue(TEXT("Winding stripe unwraps its bounded axis to two pixels"), FMath::IsNearlyEqual(MaxX - MinX, .4));
    }

    UTexture2D* Texture = NewObject<UTexture2D>();
    check(Texture);
    Texture->Source.Init(5, 5, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(Pixels.GetData()));
    TestTrue(TEXT("Editor source imports without platform compressed mips"), BreachPatternImport::Preview(Texture, Options, Result));
    TestEqual(TEXT("Source mip retains the unwrapped region"), Result.Regions.Num(), 1);
    TestFalse(TEXT("Missing source texture is rejected"), BreachPatternImport::Preview(nullptr, Options, Result));
    TestTrue(TEXT("Failure gives an actionable error"), !Result.Error.IsEmpty());

    Options.Mode = EBreachPatternImportMode::GuideMap;
    Pixels.Init(FColor::Black, 49);
    for (int32 X = 1; X < 6; ++X)
    {
        Pixels[3 * 7 + X] = FColor::White;
    }
    TestTrue(TEXT("Bright line imports"), BreachPatternImport::FromPixels(7, 7, Pixels, Options, Result));
    TestEqual(TEXT("Guide mode"), Result.Mode, EBreachPatternMode::GrainGuides);
    TestEqual(TEXT("One open trace"), Result.Guides.Num(), 1);
    if (Result.Guides.Num() == 1)
    {
        TestEqual(TEXT("Straight trace simplifies to endpoints"), Result.Guides[0].Points.Num(), 2);
    }
    Pixels[2 * 7 + 3] = FColor::White;
    Pixels[1 * 7 + 3] = FColor::White;
    TestTrue(TEXT("Branch imports"), BreachPatternImport::FromPixels(7, 7, Pixels, Options, Result));
    TestEqual(TEXT("Junction splits into three open traces"), Result.Guides.Num(), 3);

    Pixels.Init(FColor::Black, 49);
    for (int32 X = 2; X <= 4; ++X)
    {
        Pixels[2 * 7 + X] = FColor::White;
        Pixels[4 * 7 + X] = FColor::White;
    }
    Pixels[3 * 7 + 2] = FColor::White;
    Pixels[3 * 7 + 4] = FColor::White;
    TestFalse(TEXT("Closed guide rejected, never made into a cell"), BreachPatternImport::FromPixels(7, 7, Pixels, Options, Result));
    TestTrue(TEXT("Closed guide diagnostic explains the unsupported loop"), Result.Error.Contains(TEXT("Closed guide")));
    Pixels.Init(FColor::White, 49);
    TestFalse(TEXT("Filled guide map cannot become a polygon grid"), BreachPatternImport::FromPixels(7, 7, Pixels, Options, Result));
    Pixels.Init(FColor::Black, 81);
    for (int32 Y = 1; Y <= 5; ++Y)
    {
        Pixels[Y * 9 + 1] = FColor::White;
        Pixels[Y * 9 + 5] = FColor::White;
    }
    for (int32 X = 1; X <= 5; ++X)
    {
        Pixels[5 * 9 + X] = FColor::White;
    }
    for (int32 Y = 1; Y <= 3; ++Y)
    {
        Pixels[Y * 9 + 3] = FColor::White;
    }
    Options.Simplification = 1;
    TestTrue(TEXT("Simplification preserves independent guide topology"), BreachPatternImport::FromPixels(9, 9, Pixels, Options, Result));
    TestEqual(TEXT("Independent U and center stroke remain separate"), Result.Guides.Num(), 2);
    TestTrue(TEXT("Topology fallback is disclosed"), Result.Warnings.ContainsByPredicate([](const FString& Warning) { return Warning.Contains(TEXT("simplification")); }));

    Options.Mode = EBreachPatternImportMode::Photo;
    Options.TargetMode = EBreachPatternMode::Regions;
    Pixels.Init(FColor::Black, 25);
    Pixels[12] = FColor::White;
    TestTrue(TEXT("Photo brightness draft imports selected region"), BreachPatternImport::FromPixels(5, 5, Pixels, Options, Result));
    TestEqual(TEXT("Dark photo background is not an ID region"), Result.Regions.Num(), 1);
    Options.MinFeaturePixels = 2;
    TestFalse(TEXT("Feature filter measures source pixels"), BreachPatternImport::FromPixels(5, 5, Pixels, Options, Result));
    TestTrue(TEXT("Failure leaves no staged regions"), Result.Regions.IsEmpty());

    Options.TargetMode = EBreachPatternMode::GrainGuides;
    Options.Simplification = .002;
    Pixels.Init(FColor::Black, 81);
    for (int32 Y = 0; Y < 9; ++Y)
    {
        for (int32 X = 0; X < 5; ++X)
        {
            Pixels[Y * 9 + X] = FColor::White;
        }
    }
    TestTrue(TEXT("Photo edge draft traces periodic open guides"), BreachPatternImport::FromPixels(9, 9, Pixels, Options, Result));
    TestEqual(TEXT("Photo edges produce guides rather than filled regions"), Result.Mode, EBreachPatternMode::GrainGuides);
    TestEqual(TEXT("The two luminance transitions remain separate"), Result.Guides.Num(), 2);
    for (const FBreachPatternGuide& Guide : Result.Guides)
    {
        TestEqual(TEXT("Straight periodic photo edge retains two endpoints"), Guide.Points.Num(), 2);
        TestFalse(TEXT("A winding photo edge is open in its lifted tile"), Guide.Points[0].Equals(Guide.Points.Last(), 1.e-12));
    }
    Options.Threshold = -1;
    TestFalse(TEXT("Out of range threshold rejected"), BreachPatternImport::FromPixels(9, 9, Pixels, Options, Result));
    TestFalse(TEXT("Invalid dimensions rejected"), BreachPatternImport::FromPixels(0, 7, Pixels, Options, Result));
    return true;
}
#endif
