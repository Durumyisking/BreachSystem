#include "Data/BreachPattern.h"
#include "Geometry/BreachPatternGeometry.h"
#include "Misc/AutomationTest.h"
#include <limits>

// 테스트 전용: 선 따라 찢기가 패턴 선을 조각 옆면으로 쓰고, 조각 끝만 사용자 옵션대로 움직이는지 검증한다.
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
namespace BreachLineTearTest
{
	using namespace UE::Geometry;

	// Vertical lines every 0.1 UV, offset so a lane boundary never sits on the circle center.
	constexpr double Spacing = .1;
	constexpr double Offset = .05;

	UBreachPattern* MakePattern(double LineStart = 0, double LineEnd = 1)
	{
		UBreachPattern* Pattern = NewObject<UBreachPattern>();
		Pattern->Mode = EBreachPatternMode::GrainGuides;
		for (int32 Index = 0; Index < 10; ++Index)
		{
			FBreachPatternGuide Guide;
			Guide.Points = {{Offset + Index * Spacing, LineStart}, {Offset + Index * Spacing, LineEnd}};
			Pattern->Guides.Add(Guide);
		}
		FBreachLineTearOptions& Tear = Pattern->LineTear;
		Tear.TearLengthMin = 0;
		Tear.TearLengthMax = 0;
		Tear.MinTearLength = 0;
		Tear.MaxSplits = 0;
		Tear.MaxSpikes = 0;
		return Pattern;
	}

	FPolygon2d Circle(const FVector2d& Center, double Radius)
	{
		FPolygon2d Result;
		for (int32 Index = 0; Index < 48; ++Index)
		{
			const double Angle = Index * UE_DOUBLE_TWO_PI / 48;
			Result.AppendVertex(Center + FVector2d(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}
		return Result;
	}

	bool OnLine(double X)
	{
		// Strip sides sit a hair (1e-5 of the cut) outside the line so neighboring strips overlap.
		const double Phase = FMath::Fmod(X - Offset, Spacing);
		return FMath::Min(FMath::Abs(Phase), Spacing - FMath::Abs(Phase)) < 1.e-4;
	}

	FAxisAlignedBox2d Bounds(TConstArrayView<FGeneralPolygon2d> Regions)
	{
		FAxisAlignedBox2d Result = FAxisAlignedBox2d::Empty();
		for (const FGeneralPolygon2d& Region : Regions)
		{
			Result.Contain(Region.GetOuter().Bounds());
		}
		return Result;
	}

	double Area(TConstArrayView<FGeneralPolygon2d> Regions)
	{
		double Result = 0;
		for (const FGeneralPolygon2d& Region : Regions)
		{
			Result += FMath::Abs(Region.SignedArea());
		}
		return Result;
	}

	bool Covers(TConstArrayView<FGeneralPolygon2d> Regions, const FVector2d& Point)
	{
		for (const FGeneralPolygon2d& Region : Regions)
		{
			if (Region.Contains(Point))
			{
				return true;
			}
		}
		return false;
	}

	bool SameRegions(TConstArrayView<FGeneralPolygon2d> A, TConstArrayView<FGeneralPolygon2d> B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (A[Index].GetOuter().GetVertices() != B[Index].GetOuter().GetVertices())
			{
				return false;
			}
		}
		return true;
	}

	FBreachContourOptions PlainContour(int32 Seed)
	{
		FBreachContourOptions Options;
		Options.bEnabled = false;
		Options.Seed = Seed;
		return Options;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachLineTearShapeTest, "BreachSystem.Pattern.LineTear.Shape", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachLineTearShapeTest::RunTest(const FString& Parameters)
{
	using namespace BreachLineTearTest;
	UBreachPattern* Pattern = MakePattern();
	const FPolygon2d Base = Circle({.5, .5}, .2);
	const FBreachPatternMapping Mapping;
	FString Error;

	TArray<FGeneralPolygon2d> Zero;
	TestTrue(TEXT("Influence zero generates"), BreachPatternGeometry::Generate(*Pattern, Base, Mapping, PlainContour(1), 0, Zero, Error));
	TestEqual(TEXT("Influence zero keeps one contour"), Zero.Num(), 1);
	TestTrue(TEXT("Influence zero keeps the contour area"), FMath::IsNearlyEqual(Area(Zero), FMath::Abs(Base.SignedArea()), 1.e-9));

	// All options at zero: every lane becomes a rectangle spanning exactly its own contour cut.
	TArray<FGeneralPolygon2d> Strips;
	if (!TestTrue(TEXT("Plain strips generate"), BreachPatternGeometry::Generate(*Pattern, Base, Mapping, PlainContour(1), 1, Strips, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("Neighboring strips join into one cut"), Strips.Num(), 1);
	bool bAllOnLines = true;
	for (const FVector2d& Point : Strips[0].GetOuter().GetVertices())
	{
		bAllOnLines &= OnLine(Point.X);
	}
	TestTrue(TEXT("Strip sides lie on the lines, so every corner sits on a line"), bAllOnLines);
	const FAxisAlignedBox2d BaseBounds = Base.Bounds();
	const FAxisAlignedBox2d StripBounds = Bounds(Strips);
	TestTrue(TEXT("Zero tear keeps the contour's extent along the lines"), FMath::IsNearlyEqual(StripBounds.Min.Y, BaseBounds.Min.Y, 1.e-7) && FMath::IsNearlyEqual(StripBounds.Max.Y, BaseBounds.Max.Y, 1.e-7));
	TestTrue(TEXT("Strips widen only to the next line"), StripBounds.Min.X > BaseBounds.Min.X - Spacing && StripBounds.Max.X < BaseBounds.Max.X + Spacing);
	TestTrue(TEXT("Strips cover the whole contour"), Area(Strips) > FMath::Abs(Base.SignedArea()));

	// Tear length is a fraction of the hole's length along the lines, scaled by influence, and never widens the cut.
	Pattern->LineTear.TearLengthMin = .5f;
	Pattern->LineTear.TearLengthMax = .5f;
	TArray<FGeneralPolygon2d> Full, Half;
	TestTrue(TEXT("Full tear generates"), BreachPatternGeometry::Generate(*Pattern, Base, Mapping, PlainContour(1), 1, Full, Error));
	TestTrue(TEXT("Half influence generates"), BreachPatternGeometry::Generate(*Pattern, Base, Mapping, PlainContour(1), .5, Half, Error));
	const FAxisAlignedBox2d FullBounds = Bounds(Full);
	const FAxisAlignedBox2d HalfBounds = Bounds(Half);
	TestTrue(TEXT("Tear 0.5 adds half the hole length at each end"), FMath::IsNearlyEqual(FullBounds.Height(), BaseBounds.Height() * 2, BaseBounds.Height() * .05));
	TestTrue(TEXT("Influence scales the tear"), HalfBounds.Height() > StripBounds.Height() * 1.2 && HalfBounds.Height() < FullBounds.Height() * .9);
	TestTrue(TEXT("Tear never widens the cut across the lines"), FMath::IsNearlyEqual(FullBounds.Width(), StripBounds.Width(), 1.e-7));

	// The leftmost lane [0.25, 0.35] only grazes the circle: its own cut spans y 0.368..0.632.
	// A tear of 0.25 hole lengths (0.1) reaches y 0.732; a tear relative to its own cut would stop near 0.698.
	Pattern->LineTear.TearLengthMin = .25f;
	Pattern->LineTear.TearLengthMax = .25f;
	TArray<FGeneralPolygon2d> Edge;
	TestTrue(TEXT("Edge tear generates"), BreachPatternGeometry::Generate(*Pattern, Base, Mapping, PlainContour(1), 1, Edge, Error));
	TestTrue(TEXT("A strip the hit barely touched tears by the same hole-based length"), Covers(Edge, {.3, .72}));

	Pattern->LineTear.TearLengthMin = -.4f;
	Pattern->LineTear.TearLengthMax = -.4f;
	TArray<FGeneralPolygon2d> Short;
	TestTrue(TEXT("Negative tear generates"), BreachPatternGeometry::Generate(*Pattern, Base, Mapping, PlainContour(1), 1, Short, Error));
	TestTrue(TEXT("Negative tear stops strips inside the contour"), Bounds(Short).Height() < BaseBounds.Height() * .5);

	// Strips that would tear shorter than the minimum stay whole; the contour inside them is not cut.
	// With minimum 0.7 (0.28), the grazing outer lanes (cut 0.265) stay and the inner lanes (cut 0.387+) tear.
	Pattern->LineTear.TearLengthMin = 0;
	Pattern->LineTear.TearLengthMax = 0;
	Pattern->LineTear.MinTearLength = .7f;
	TArray<FGeneralPolygon2d> Kept;
	TestTrue(TEXT("Minimum tear length generates"), BreachPatternGeometry::Generate(*Pattern, Base, Mapping, PlainContour(1), 1, Kept, Error));
	TestFalse(TEXT("A strip shorter than the minimum tear length stays whole"), Covers(Kept, {.32, .5}));
	TestTrue(TEXT("Longer strips still tear"), Covers(Kept, {.5, .5}) && Bounds(Kept).Width() < StripBounds.Width() - Spacing);
	Pattern->LineTear.MinTearLength = 0;

	// Spikes and splits add detail between the lines; the same seed repeats exactly.
	Pattern->LineTear.TearLengthMin = -.05f;
	Pattern->LineTear.TearLengthMax = .2f;
	Pattern->LineTear.MaxSpikes = 6;
	Pattern->LineTear.SpikeLengthMin = .3f;
	Pattern->LineTear.SpikeLengthMax = 1;
	TArray<FGeneralPolygon2d> Spikes, Repeat, Other;
	TestTrue(TEXT("Spikes generate"), BreachPatternGeometry::Generate(*Pattern, Base, Mapping, PlainContour(7), 1, Spikes, Error));
	TestTrue(TEXT("Same seed generates"), BreachPatternGeometry::Generate(*Pattern, Base, Mapping, PlainContour(7), 1, Repeat, Error));
	TestTrue(TEXT("Other seed generates"), BreachPatternGeometry::Generate(*Pattern, Base, Mapping, PlainContour(8), 1, Other, Error));
	TestTrue(TEXT("Same seed repeats exactly"), SameRegions(Spikes, Repeat));
	TestFalse(TEXT("A different seed changes the tear"), SameRegions(Spikes, Other));
	int32 OffLine = 0;
	for (const FGeneralPolygon2d& Region : Spikes)
	{
		for (const FVector2d& Point : Region.GetOuter().GetVertices())
		{
			OffLine += OnLine(Point.X) ? 0 : 1;
		}
	}
	TestTrue(TEXT("Spikes put tips between the lines"), OffLine >= 4);
	TestTrue(TEXT("Spike result is simple"), BreachPatternGeometry::ValidateRegions(Spikes, Error));

	Pattern->LineTear.MaxSpikes = 0;
	Pattern->LineTear.MaxSplits = 4;
	bool bStepFound = false;
	for (int32 Seed = 1; Seed <= 4 && !bStepFound; ++Seed)
	{
		TArray<FGeneralPolygon2d> Steps;
		TestTrue(TEXT("Splits generate"), BreachPatternGeometry::Generate(*Pattern, Base, Mapping, PlainContour(Seed), 1, Steps, Error));
		for (const FGeneralPolygon2d& Region : Steps)
		{
			for (const FVector2d& Point : Region.GetOuter().GetVertices())
			{
				bStepFound |= !OnLine(Point.X);
			}
		}
	}
	TestTrue(TEXT("Splits tear one lane at more than one length"), bStepFound);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachLineTearCoverageTest, "BreachSystem.Pattern.LineTear.Coverage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachLineTearCoverageTest::RunTest(const FString& Parameters)
{
	using namespace BreachLineTearTest;
	// Lines only span a band; contour outside every lane must stay as authored.
	UBreachPattern* Pattern = MakePattern(.45, .55);
	Pattern->LineTear.TearLengthMin = -.4f;
	Pattern->LineTear.TearLengthMax = -.4f;
	const FPolygon2d Base = Circle({.5, .5}, .2);
	FString Error;
	TArray<FGeneralPolygon2d> Result;
	if (!TestTrue(TEXT("Partial lines generate"), BreachPatternGeometry::Generate(*Pattern, Base, FBreachPatternMapping(), PlainContour(1), 1, Result, Error)))
	{
		AddError(Error);
		return false;
	}
	const FAxisAlignedBox2d BaseBounds = Base.Bounds();
	TestTrue(TEXT("Contour beyond the lines keeps its extent"), FMath::IsNearlyEqual(Bounds(Result).Min.Y, BaseBounds.Min.Y, 1.e-7) && FMath::IsNearlyEqual(Bounds(Result).Max.Y, BaseBounds.Max.Y, 1.e-7));
	TestTrue(TEXT("Partial lanes still cut back inside the band"), Area(Result) < FMath::Abs(Base.SignedArea()));
	TestTrue(TEXT("Partial result is simple"), BreachPatternGeometry::ValidateRegions(Result, Error));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachLineTearOrientationTest, "BreachSystem.Pattern.LineTear.Orientation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachLineTearOrientationTest::RunTest(const FString& Parameters)
{
	using namespace BreachLineTearTest;
	UBreachPattern* Pattern = MakePattern();
	for (FBreachPatternGuide& Guide : Pattern->Guides)
	{
		for (FVector2D& Point : Guide.Points)
		{
			Point = FVector2D(Point.Y, Point.X);
		}
	}
	Pattern->LineTear.TearLengthMin = .5f;
	Pattern->LineTear.TearLengthMax = .5f;
	const FPolygon2d Base = Circle({.5, .5}, .2);
	FString Error;
	TArray<FGeneralPolygon2d> Horizontal;
	TestTrue(TEXT("Horizontal lines generate"), BreachPatternGeometry::Generate(*Pattern, Base, FBreachPatternMapping(), PlainContour(1), 1, Horizontal, Error));
	const FAxisAlignedBox2d HorizontalBounds = Bounds(Horizontal);
	TestTrue(TEXT("Horizontal lines extend the cut horizontally"), HorizontalBounds.Width() > HorizontalBounds.Height() * 1.5);

	FBreachPatternMapping Rotated;
	Rotated.UVPerX = FVector2d(.7, -.7);
	Rotated.UVPerY = FVector2d(.7, .7);
	Pattern->LineTear.MaxSpikes = 4;
	Pattern->LineTear.MaxSplits = 2;
	TArray<FGeneralPolygon2d> Diagonal;
	if (!TestTrue(TEXT("Rotated mapping generates"), BreachPatternGeometry::Generate(*Pattern, Base, Rotated, PlainContour(3), 1, Diagonal, Error)))
	{
		AddError(Error);
	}
	TestTrue(TEXT("Rotated result is simple"), BreachPatternGeometry::ValidateRegions(Diagonal, Error));

	// Crossing lines cannot bound a strip, but generation must still succeed.
	UBreachPattern* Crossing = NewObject<UBreachPattern>();
	Crossing->Mode = EBreachPatternMode::GrainGuides;
	FBreachPatternGuide Guide;
	Guide.Points = {{.3, 0}, {.7, 1}};
	Crossing->Guides.Add(Guide);
	Guide.Points = {{.7, 0}, {.3, 1}};
	Crossing->Guides.Add(Guide);
	TArray<FGeneralPolygon2d> Crossed;
	TestTrue(TEXT("Crossing lines generate"), BreachPatternGeometry::Generate(*Crossing, Base, FBreachPatternMapping(), PlainContour(1), 1, Crossed, Error));
	TestTrue(TEXT("Crossing result is simple"), BreachPatternGeometry::ValidateRegions(Crossed, Error));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachLineTearValidationTest, "BreachSystem.Pattern.LineTear.Validation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBreachLineTearValidationTest::RunTest(const FString& Parameters)
{
	using namespace BreachLineTearTest;
	UBreachPattern* Pattern = MakePattern();
	FString Error;
	TestTrue(TEXT("Default line tear options validate"), BreachPatternGeometry::Validate(*NewObject<UBreachPattern>(), Error));
	const auto Rejects = [&](const TCHAR* What, TFunctionRef<void(FBreachLineTearOptions&)> Change)
	{
		const FBreachLineTearOptions Saved = Pattern->LineTear;
		Change(Pattern->LineTear);
		TestFalse(What, BreachPatternGeometry::Validate(*Pattern, Error));
		Pattern->LineTear = Saved;
	};
	Rejects(TEXT("Tear length minimum above maximum rejected"), [](FBreachLineTearOptions& Tear) { Tear.TearLengthMin = .5f; Tear.TearLengthMax = .1f; });
	Rejects(TEXT("Tear length below -0.5 rejected"), [](FBreachLineTearOptions& Tear) { Tear.TearLengthMin = -.6f; });
	Rejects(TEXT("Tear length above 1.5 rejected"), [](FBreachLineTearOptions& Tear) { Tear.TearLengthMax = 1.6f; });
	Rejects(TEXT("Non-finite tear length rejected"), [](FBreachLineTearOptions& Tear) { Tear.TearLengthMax = std::numeric_limits<float>::quiet_NaN(); });
	Rejects(TEXT("Negative minimum tear length rejected"), [](FBreachLineTearOptions& Tear) { Tear.MinTearLength = -.1f; });
	Rejects(TEXT("Minimum tear length above one rejected"), [](FBreachLineTearOptions& Tear) { Tear.MinTearLength = 1.1f; });
	Rejects(TEXT("Five splits rejected"), [](FBreachLineTearOptions& Tear) { Tear.MaxSplits = 5; });
	Rejects(TEXT("Nine spikes rejected"), [](FBreachLineTearOptions& Tear) { Tear.MaxSpikes = 9; });
	Rejects(TEXT("Negative spike length rejected"), [](FBreachLineTearOptions& Tear) { Tear.SpikeLengthMin = -.1f; });
	Rejects(TEXT("Spike length minimum above maximum rejected"), [](FBreachLineTearOptions& Tear) { Tear.SpikeLengthMin = 1; Tear.SpikeLengthMax = .5f; });

	Pattern->LineTear.MaxSpikes = 9;
	Pattern->Mode = EBreachPatternMode::None;
	TestTrue(TEXT("None mode ignores unused line tear options"), BreachPatternGeometry::Validate(*Pattern, Error));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachLineTearLargeCutTest, "BreachSystem.Pattern.LineTear.LargeCut", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 테스트 전용: 여러 타일에 걸친 큰 구멍의 결과를 고정해, 결 찢김 계산을 빠르게 바꿔도 같은 모양이 나오는지 검증한다.
// 기댓값은 최적화 전 코드(dde7b82)로 측정했다. 모양 규칙을 의도적으로 바꾸면 다시 측정해 갱신한다.
bool FBreachLineTearLargeCutTest::RunTest(const FString& Parameters)
{
	using namespace BreachLineTearTest;
	// Wavy guides like the wood pattern; each end misses its next tile copy slightly, so tiles must be joined into long lines.
	UBreachPattern* Pattern = NewObject<UBreachPattern>();
	Pattern->Mode = EBreachPatternMode::GrainGuides;
	for (int32 Line = 0; Line < 10; ++Line)
	{
		FBreachPatternGuide Guide;
		for (int32 Point = 0; Point < 9; ++Point)
		{
			const double V = .003 + Point * (.994 / 8);
			Guide.Points.Add({Offset + Line * Spacing + .02 * FMath::Sin(UE_DOUBLE_TWO_PI * V) + .004 * V, V});
		}
		Pattern->Guides.Add(Guide);
	}
	Pattern->LineTear.MaxSplits = 1;

	// 100-unit tiles, as with the world-size UV of the wood wall.
	FBreachPatternMapping Straight;
	Straight.UVPerX = FVector2d(.01, 0);
	Straight.UVPerY = FVector2d(0, .01);
	FBreachPatternMapping Rotated;
	Rotated.UVPerX = FVector2d(.007, -.007);
	Rotated.UVPerY = FVector2d(.007, .007);

	struct FCase
	{
		const TCHAR* Name;
		double Radius;
		const FBreachPatternMapping* Mapping;
		int32 Regions;
		int32 Vertices;
		double Area;
		FVector2d Min;
		FVector2d Max;
	};
	const FCase Cases[] = {
		{TEXT("Large"), 108, &Straight, 1, 726, 46269.693535989936, FVector2d(-73.036767063763889, -96.070428333080514), FVector2d(149.8584076531335, 215.06150268816324)},
		{TEXT("Small"), 27, &Straight, 1, 150, 2997.8127474171206, FVector2d(3.8704572513953295, 19.895195857928798), FVector2d(66.821471665650819, 96.261654614583264)},
		{TEXT("Rotated"), 108, &Rotated, 1, 763, 46563.443400977594, FVector2d(-100.51980344430721, -85.764489987801113), FVector2d(178.44668247458122, 203.53468958182188)},
	};
	for (const FCase& Case : Cases)
	{
		FString Error;
		TArray<FGeneralPolygon2d> Result;
		if (!TestTrue(FString::Printf(TEXT("%s cut generates"), Case.Name), BreachPatternGeometry::Generate(*Pattern, Circle({37, 61}, Case.Radius), *Case.Mapping, PlainContour(7), 1, Result, Error)))
		{
			AddError(Error);
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s cut is simple"), Case.Name), BreachPatternGeometry::ValidateRegions(Result, Error));

		int32 Vertices = 0;
		for (const FGeneralPolygon2d& Region : Result)
		{
			Vertices += Region.GetOuter().VertexCount();
			for (const FPolygon2d& Hole : Region.GetHoles())
			{
				Vertices += Hole.VertexCount();
			}
		}
		const double CutArea = Area(Result);
		const FAxisAlignedBox2d CutBounds = Bounds(Result);
		AddInfo(FString::Printf(TEXT("%s regions=%d vertices=%d area=%.17g min=(%.17g, %.17g) max=(%.17g, %.17g)"), Case.Name, Result.Num(), Vertices, CutArea, CutBounds.Min.X, CutBounds.Min.Y, CutBounds.Max.X, CutBounds.Max.Y));
		TestEqual(FString::Printf(TEXT("%s keeps its region count"), Case.Name), Result.Num(), Case.Regions);
		TestEqual(FString::Printf(TEXT("%s keeps its vertex count"), Case.Name), Vertices, Case.Vertices);
		TestTrue(FString::Printf(TEXT("%s keeps its area"), Case.Name), FMath::IsNearlyEqual(CutArea, Case.Area, Case.Area * 1.e-9));
		TestTrue(FString::Printf(TEXT("%s keeps its bounds"), Case.Name), CutBounds.Min.Equals(Case.Min, 1.e-6) && CutBounds.Max.Equals(Case.Max, 1.e-6));
	}
	return !HasAnyErrors();
}
#endif
