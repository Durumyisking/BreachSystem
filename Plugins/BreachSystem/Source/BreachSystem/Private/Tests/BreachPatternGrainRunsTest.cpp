#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
// 테스트 전용: 미세 좌표 차이가 아닌 긴 결 방향 절단 구간과 실제 생성 윤곽을 검증한다.
#include "Surface/BreachPattern.h"
#include "Surface/BreachPatternGeometry.h"
#include "Shape/BreachContour.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	using namespace UE::Geometry;

	double LongestGrainRun(const FPolygon2d& Polygon, FVector2d Direction, double Degrees)
	{
		const double Cosine = FMath::Cos(FMath::DegreesToRadians(Degrees));
		double Longest = 0;
		double Current = 0;
		for (int32 I = 0; I < Polygon.VertexCount() * 2; ++I)
		{
			const FVector2d Edge = Polygon[(I + 1) % Polygon.VertexCount()] - Polygon[I % Polygon.VertexCount()];
			const double Length = Edge.Length();
			if (Length > 1.e-10 && FMath::Abs(Edge.Dot(Direction)) >= Cosine * Length)
			{
				Current += Length;
				Longest = FMath::Max(Longest, Current);
			}
			else
			{
				Current = 0;
			}
		}
		return Longest;
	}

	FString PointsJSON(TConstArrayView<FVector2d> Points)
	{
		FString Text = TEXT("[");
		for (int32 I = 0; I < Points.Num(); ++I)
		{
			Text += FString::Printf(TEXT("%s[%.10f,%.10f]"), I ? TEXT(",") : TEXT(""), Points[I].X, Points[I].Y);
		}
		return Text + TEXT("]");
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternGrainRunsTest, "BreachSystem.Pattern.GrainRuns", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternGrainRunsTest::RunTest(const FString& Parameters)
{
	using namespace UE::Geometry;
	FString Export = TEXT("[");
	for (int32 Shape = 0; Shape < 3; ++Shape)
	{
		for (double Angle : {0., 90., 37.})
		{
			const double Radians = FMath::DegreesToRadians(Angle);
			const FVector2d Direction(FMath::Cos(Radians), FMath::Sin(Radians));
			auto Rotate = [Direction](FVector2d P)
			{
				P -= FVector2d(50, 50);
				return FVector2d(Direction.X * P.X - Direction.Y * P.Y, Direction.Y * P.X + Direction.X * P.Y) + FVector2d(50, 50);
			};
			FPolygon2d Base;
			if (Shape == 0)
			{
				for (int32 I = 0; I < 32; ++I)
				{
					const double A = I * 2 * PI / 32;
					Base.AppendVertex(Rotate(FVector2d(50 + 26 * FMath::Cos(A), 50 + 26 * FMath::Sin(A))));
				}
			}
			else
			{
				const TArray<FVector2d> Points = Shape == 1 ? TArray<FVector2d>{{22,18},{78,18},{78,82},{22,82}} : TArray<FVector2d>{{20,20},{80,20},{80,42},{53,42},{53,80},{20,80}};
				for (FVector2d P : Points)
				{
					Base.AppendVertex(Rotate(P));
				}
			}
			UBreachPattern* Pattern = NewObject<UBreachPattern>();
			Pattern->Mode = EBreachPatternMode::GrainGuides;
			for (int32 Y = 10; Y <= 90; Y += 10)
			{
				Pattern->Guides.AddDefaulted_GetRef().Points = {Rotate(FVector2d(-10,Y)) / 100, Rotate(FVector2d(110,Y)) / 100};
			}
			FBreachPatternMapping Mapping;
			Mapping.UVPerX = FVector2d(.01,0);
			Mapping.UVPerY = FVector2d(0,.01);
			FBreachContourOptions Options;
			Options.bEnabled = true;
			Options.VertexCount = 64;
			Options.bRandomizeVertexSpacing = false;
			Options.Seed = 31;
			FString Error;
			TArray<FGeneralPolygon2d> Plain;
			if (!TestTrue(TEXT("Same-input general contour generates"), BreachPatternGeometry::Generate(*Pattern, Base, Mapping, Options, 0, Plain, Error)) || Plain.Num() != 1)
			{
				AddError(Error);
				continue;
			}
			for (double Influence : {.6, 1.})
			{
				TArray<FGeneralPolygon2d> Guided;
				if (!TestTrue(TEXT("Grain contour generates"), BreachPatternGeometry::Generate(*Pattern, Base, Mapping, Options, Influence, Guided, Error)) || Guided.Num() != 1)
				{
					AddError(Error);
					continue;
				}
				const double Run = LongestGrainRun(Guided[0].GetOuter(), Direction, Influence == 1 ? 5 : 10);
				const double PlainRun = LongestGrainRun(Plain[0].GetOuter(), Direction, Influence == 1 ? 5 : 10);
				AddInfo(FString::Printf(TEXT("Shape %d angle %.0f influence %.1f: longest grain run %.3f cm, plain %.3f cm"), Shape, Angle, Influence, Run, PlainRun));
				TestTrue(TEXT("A visible continuous grain run meets the width threshold for its influence"), Run >= (Shape == 0 ? 52 : 56) * (Influence == 1 ? .35 : .25));
				TestTrue(TEXT("Grain run is materially longer than general noisy edges"), Run >= PlainRun * 1.5);
				if (Influence == 1 && Shape == 0)
				{
					bool bGrainEdgeAtBrokenEnd = false;
					const FPolygon2d& P = Guided[0].GetOuter();
					for (int32 I = 0; I < P.VertexCount(); ++I)
					{
						const FVector2d Edge = P[(I + 1) % P.VertexCount()] - P[I];
						const FVector2d Middle = (P[(I + 1) % P.VertexCount()] + P[I]) * .5 - FVector2d(50,50);
						bGrainEdgeAtBrokenEnd |= FMath::Abs(Middle.Dot(Direction)) > 15 && Edge.Length() > 5 && FMath::Abs(Edge.Dot(Direction)) > Edge.Length() * FMath::Cos(FMath::DegreesToRadians(5.));
					}
					TestTrue(TEXT("A broken end retains a grain-aligned splinter edge"), bGrainEdgeAtBrokenEnd);
				}
				TestTrue(TEXT("Guided result remains finite/simple"), BreachPatternGeometry::ValidateRegions(Guided, Error));
				bool bBetweenGuides = false;
				for (FVector2d P : Guided[0].GetOuter().GetVertices())
				{
					const FVector2d Local = P - FVector2d(50,50);
					const double Across = -Direction.Y * Local.X + Direction.X * Local.Y + 50;
					bBetweenGuides |= FMath::Abs(Across / 10 - FMath::RoundToDouble(Across / 10)) > .03;
				}
				TestTrue(TEXT("Cuts between guide lines remain possible"), bBetweenGuides);
				if (Export.Len() > 1)
				{
					Export += TEXT(",");
				}
				Export += FString::Printf(TEXT("{\"shape\":%d,\"angle\":%.0f,\"influence\":%.1f,\"base\":%s,\"plain\":%s,\"grain\":%s}"), Shape, Angle, Influence, *PointsJSON(Base.GetVertices()), *PointsJSON(Plain[0].GetOuter().GetVertices()), *PointsJSON(Guided[0].GetOuter().GetVertices()));
			}
		}
	}
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/PatternGrainRuns");
	IFileManager::Get().MakeDirectory(*Directory, true);
	TestTrue(TEXT("Export native generator comparison"), FFileHelper::SaveStringToFile(Export + TEXT("]"), *(Directory / TEXT("current.json"))));
	return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternGrainRunSeamTest, "BreachSystem.Pattern.GrainRunsSeamAndJitter", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternGrainRunSeamTest::RunTest(const FString& Parameters)
{
	using namespace UE::Geometry;
	FBreachContourOptions Options;
	Options.bEnabled = true;
	Options.VertexCount = 64;
	Options.bRandomizeVertexSpacing = false;
	Options.Seed = 31;
	FBreachPatternMapping Mapping;
	Mapping.UVPerX = FVector2d(.01,0);
	Mapping.UVPerY = FVector2d(0,.01);
	UBreachPattern* Pattern = NewObject<UBreachPattern>();
	Pattern->Mode = EBreachPatternMode::GrainGuides;
	Pattern->Guides.AddDefaulted_GetRef().Points = {{0,.2},{1,.2}};
	Pattern->Guides.AddDefaulted_GetRef().Points = {{0,.8},{1,.8}};
	const FPolygon2d SeamBox(TArray<FVector2d>{{70,20},{130,20},{130,80},{70,80}});
	FString Error;
	TArray<FGeneralPolygon2d> Result;
	if (TestTrue(TEXT("Seam-straddling grain generates"), BreachPatternGeometry::Generate(*Pattern, SeamBox, Mapping, Options, 1, Result, Error)) && Result.Num() == 1)
	{
		const double Longest = LongestGrainRun(Result[0].GetOuter(), FVector2d(1,0), 1.);
		AddInfo(FString::Printf(TEXT("Across tile seam: continuous grain run %.4f cm / 60 cm"), Longest));
		TestTrue(TEXT("Grain continues across the tile boundary without reverting to noise"), Longest >= 48.);
	}
	Pattern->Guides[0].Points = {{.1,.2},{.9,.2}};
	Pattern->Guides[1].Points = {{.1,.8},{.9,.8}};
	BreachContour::FPreparedContour GapPrepared;
	TestTrue(TEXT("Finite-gap fixture prepares"), BreachContour::Prepare(SeamBox, Options, GapPrepared, Error));
	FPolygon2d GapPlain;
	BreachContour::Generate(GapPrepared, GapPlain);
	Result.Reset();
	if (TestTrue(TEXT("Finite authored gaps generate"), BreachPatternGeometry::Generate(*Pattern, SeamBox, Mapping, Options, 1, Result, Error)) && Result.Num() == 1 && GapPrepared.Samples.Num() == Result[0].GetOuter().VertexCount())
	{
		int32 GapSamples = 0;
		for (int32 I = 0; I < GapPrepared.Samples.Num(); ++I)
		{
			const auto& Sample = GapPrepared.Samples[I];
			if (Sample.Point.X > 95 && Sample.Point.X < 105 && FMath::Abs(Sample.Normal.Y) > .99)
			{
				TestTrue(TEXT("Finite fiber gaps keep free fracture instead of jumping to another repeated row"), Result[0].GetOuter()[I].Equals(GapPlain[I], 1.e-8));
				++GapSamples;
			}
		}
		TestTrue(TEXT("Finite-gap check covers actual gap samples"), GapSamples >= 2);
	}
	const double Rise = 60 * FMath::Tan(FMath::DegreesToRadians(20.));
	const FPolygon2d SlopedBox(TArray<FVector2d>{{20,20},{80,20+Rise},{80,70},{20,70-Rise}});
	Pattern->Guides.Reset();
	Pattern->Guides.AddDefaulted_GetRef().Points = {{-1,.6},{2,.6}};
	Options.bRandomizeVertexSpacing = true;
	Options.VertexCount = 128;
	Options.MaxDisplacementFraction = .1;
	BreachContour::FPreparedContour Prepared;
	TestTrue(TEXT("Jittered sample fixture prepares"), BreachContour::Prepare(SlopedBox, Options, Prepared, Error));
	FPolygon2d Noisy;
	BreachContour::Generate(Prepared, Noisy);
	Result.Reset();
	if (TestTrue(TEXT("Jittered grain generates"), BreachPatternGeometry::Generate(*Pattern, SlopedBox, Mapping, Options, 1, Result, Error)) && Result.Num() == 1 && Prepared.Samples.Num() == Result[0].GetOuter().VertexCount())
	{
		double Minimum = TNumericLimits<double>::Max();
		double Maximum = -TNumericLimits<double>::Max();
		int32 Samples = 0;
		double MaximumJitter = 0;
		for (int32 I = 0; I < Prepared.Samples.Num(); ++I)
		{
			const auto& Sample = Prepared.Samples[I];
			if (Sample.Normal.Y > .8 && Sample.Point.X > 45 && Sample.Point.X < 55 && Sample.MaxDisplacement > 3)
			{
				const FVector2d Delta = Noisy[I] - Sample.Point;
				MaximumJitter = FMath::Max(MaximumJitter, (Delta - Sample.Normal * Delta.Dot(Sample.Normal)).Length());
				Minimum = FMath::Min(Minimum, Result[0].GetOuter()[I].Y);
				Maximum = FMath::Max(Maximum, Result[0].GetOuter()[I].Y);
				++Samples;
			}
		}
		AddInfo(FString::Printf(TEXT("Full-follow run with spacing jitter: %d samples, cross-grain drift %.8f cm, tangent jitter %.6f cm"), Samples, Maximum - Minimum, MaximumJitter));
		TestTrue(TEXT("Jitter test covers multiple sloped boundary normals"), Samples >= 4);
		TestTrue(TEXT("Jitter fixture actually moves samples tangentially"), MaximumJitter > .01);
		TestTrue(TEXT("Tangential spacing jitter cannot pull a full-follow run off its guide"), Maximum - Minimum < 1.e-6);
	}
	return !HasAnyErrors();
}
#endif
