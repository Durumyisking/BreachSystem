#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
// 테스트 전용: 파단 끝의 실제 예각, 방향 독립성, 기존 윤곽 보존과 생성 비용을 검증한다.
#include "Surface/BreachPattern.h"
#include "Surface/BreachPatternGeometry.h"
#include "Surface/BreachGrainFracture.h"
#include "Shape/BreachContour.h"
#include "Algo/Reverse.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include <limits>

namespace
{
	using namespace UE::Geometry;
	bool SameEndBoundary(const FPolygon2d& A, const FPolygon2d& B)
	{
		if (A.VertexCount() != B.VertexCount())
		{
			return false;
		}
		for (int32 I = 0; I < A.VertexCount(); ++I)
		{
			if (!A[I].Equals(B[I], 1.e-9))
			{
				return false;
			}
		}
		return true;
	}

	double EndTipScore(const FPolygon2d& P, FVector2d Grain)
	{
		double Score = 0;
		for (int32 I = 0; I < P.VertexCount(); ++I)
		{
			const FVector2d A = P[(I + P.VertexCount() - 1) % P.VertexCount()] - P[I];
			const FVector2d B = P[(I + 1) % P.VertexCount()] - P[I];
			const double Length = A.Length() * B.Length();
			if (Length > 1.e-10 && FMath::Abs((A + B).GetSafeNormal().Dot(Grain)) > .7)
			{
				Score += FMath::Max(0., A.Dot(B) / Length);
			}
		}
		return Score;
	}

	TArray<TPair<FVector2d,FVector2d>> LongGrainEdges(const FPolygon2d& P, FVector2d Grain)
	{
		TArray<TPair<FVector2d,FVector2d>> Edges;
		for (int32 I = 0; I < P.VertexCount(); ++I)
		{
			const FVector2d A = P[I], B = P[(I + 1) % P.VertexCount()];
			if ((B - A).Length() > 2 && FMath::Abs((B - A).GetSafeNormal().Dot(Grain)) > .995)
			{
				Edges.Emplace(A,B);
			}
		}
		return Edges;
	}

	FString EndPointsJSON(const FPolygon2d& P)
	{
		FString Text = TEXT("[");
		for (int32 I = 0; I < P.VertexCount(); ++I)
		{
			Text += FString::Printf(TEXT("%s[%.9f,%.9f]"), I ? TEXT(",") : TEXT(""), P[I].X, P[I].Y);
		}
		return Text + TEXT("]");
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternEndSharpnessTest, "BreachSystem.Pattern.EndSharpness.Shape", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternEndSharpnessTest::RunTest(const FString& Parameters)
{
	using namespace UE::Geometry;
	FString Export = TEXT("[");
	for (int32 Seed : {31,77})
	{
		for (double Angle : {0.,90.,37.})
		{
			const double A = FMath::DegreesToRadians(Angle);
			const FVector2d Grain(FMath::Cos(A),FMath::Sin(A));
			auto Rotate = [Grain](FVector2d P) { return FVector2d(P.X * Grain.X - P.Y * Grain.Y,P.X * Grain.Y + P.Y * Grain.X); };
			FPolygon2d Base;
			for (FVector2d P : TArray<FVector2d>{{-30,-45},{30,-45},{30,45},{-30,45}})
			{
				Base.AppendVertex(Rotate(P));
			}
			UBreachPattern* Pattern = NewObject<UBreachPattern>();
			Pattern->Mode = EBreachPatternMode::GrainGuides;
			for (double Y : {-36.,-12.,12.,36.})
			{
				Pattern->Guides.AddDefaulted_GetRef().Points = {Rotate(FVector2d(-100,Y))/200,Rotate(FVector2d(100,Y))/200};
			}
			FBreachPatternMapping Mapping;
			Mapping.UVPerX = FVector2d(.005,0);
			Mapping.UVPerY = FVector2d(0,.005);
			FBreachContourOptions Options;
			Options.bEnabled = false;
			Options.VertexCount = 16;
			Options.MacroDepthRatio = Options.MediumDepthRatio = Options.MicroDepthRatio = 0;
			Options.Seed = Seed;
			FString Error;
			TArray<FGeneralPolygon2d> Plain, Sharp, Repeated;
			TestTrue(TEXT("Blunt grain fracture generates without contour noise"),BreachPatternGeometry::Generate(*Pattern,Base,Mapping,Options,1,Plain,Error));
			Pattern->GrainEndSharpness = 1;
			TestTrue(TEXT("Grain-end fracture generates"),BreachPatternGeometry::Generate(*Pattern,Base,Mapping,Options,1,Sharp,Error));
			TestTrue(TEXT("Same seed repeats"),BreachPatternGeometry::Generate(*Pattern,Base,Mapping,Options,1,Repeated,Error));
			if (Plain.Num() != 1 || Sharp.Num() != 1 || Repeated.Num() != 1)
			{
				AddError(Error);
				continue;
			}
			const auto& Before = Plain[0].GetOuter();
			const auto& After = Sharp[0].GetOuter();
			const auto Shelves = LongGrainEdges(Before,Grain);
			TestTrue(TEXT("Each grain interval gets a staggered end and visible long shelf even at sharpness zero"),Shelves.Num() >= 6);
			for (const auto& Edge : Shelves)
			{
				bool bPreserved = false;
				for (int32 I = 0; I < After.VertexCount(); ++I)
				{
					bPreserved |= After[I].Equals(Edge.Key,1.e-7) && After[(I + 1) % After.VertexCount()].Equals(Edge.Value,1.e-7);
				}
				TestTrue(TEXT("Sharpness changes short ends only; long tears keep their endpoints"),bPreserved);
			}
			const double BeforeScore = EndTipScore(Before,Grain), AfterScore = EndTipScore(After,Grain);
			AddInfo(FString::Printf(TEXT("Seed%d angle%.0f: %d long shelves, end-tip score %.5f -> %.5f, vertices %d -> %d"),Seed,Angle,Shelves.Num(),BeforeScore,AfterScore,Before.VertexCount(),After.VertexCount()));
			TestTrue(TEXT("Only the short fracture ends become acute"),AfterScore > BeforeScore + .5);
			TestTrue(TEXT("Contour has a bounded vertex budget"),After.VertexCount() <= 128);
			TestTrue(TEXT("Seed remains deterministic"),SameEndBoundary(After,Repeated[0].GetOuter()));
			TestTrue(TEXT("Assembled contour is simple"),BreachPatternGeometry::ValidateRegions(Sharp,Error));
			const double Cap = FMath::Min(Base.Bounds().Width(),Base.Bounds().Height()) * Options.MaxDisplacementFraction;
			for (const FVector2d& P : After.GetVertices())
			{
				TestTrue(TEXT("Every added vertex remains in the existing physical displacement band"),FMath::Sqrt(Base.DistanceSquared(P)) <= Cap + 1.e-7);
			}
			TArray<FGeneralPolygon2d> Zero;
			TestTrue(TEXT("Influence zero generates"),BreachPatternGeometry::Generate(*Pattern,Base,Mapping,Options,0,Zero,Error));
			if (Zero.Num() == 1)
			{
				TestTrue(TEXT("Influence zero retains the exact no-noise base"),SameEndBoundary(Zero[0].GetOuter(),Base));
			}
			Export += FString::Printf(TEXT("%s{\"seed\":%d,\"angle\":%.0f,\"before\":%s,\"after\":%s}"),Export.Len() > 1 ? TEXT(",") : TEXT(""),Seed,Angle,*EndPointsJSON(Before),*EndPointsJSON(After));
		}
	}
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/PatternGrainEnds");
	IFileManager::Get().MakeDirectory(*Directory,true);
	TestTrue(TEXT("Export native grain-end contours"),FFileHelper::SaveStringToFile(Export + TEXT("]"),*(Directory / TEXT("comparison.json"))));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternEndSharpnessInvalidTest, "BreachSystem.Pattern.EndSharpness.Invalid", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternEndSharpnessInvalidTest::RunTest(const FString& Parameters)
{
	UBreachPattern* Pattern = NewObject<UBreachPattern>();
	Pattern->Mode = EBreachPatternMode::GrainGuides;
	Pattern->Guides.AddDefaulted_GetRef().Points = {{0,0},{1,0}};
	FString Error;
	for (float Value : {-1.f, 1.1f, std::numeric_limits<float>::quiet_NaN()})
	{
		Pattern->GrainEndSharpness = Value;
		TestFalse(TEXT("Invalid active grain sharpness is rejected"), BreachPatternGeometry::Validate(*Pattern,Error));
	}
	Pattern->Mode = EBreachPatternMode::None;
	TestTrue(TEXT("None ignores unused grain settings"), BreachPatternGeometry::Validate(*Pattern,Error));
	Pattern->Mode = EBreachPatternMode::Regions;
	Pattern->Regions.AddDefaulted_GetRef().Outer = {{0,0},{1,0},{1,1},{0,1}};
	TestTrue(TEXT("Regions ignores unused grain settings"), BreachPatternGeometry::Validate(*Pattern,Error));
	return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternGrainEndsLimitsTest, "BreachSystem.Pattern.EndSharpness.Limits", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternGrainEndsLimitsTest::RunTest(const FString& Parameters)
{
	using namespace UE::Geometry;
	const FPolygon2d Base(TArray<FVector2d>{{-30,-45},{30,-45},{30,45},{-30,45}});
	UBreachPattern* Pattern = NewObject<UBreachPattern>();
	Pattern->Mode = EBreachPatternMode::GrainGuides;
	Pattern->GrainEndSharpness = 1;
	FBreachPatternMapping Mapping;
	Mapping.UVPerX = FVector2d(.005,0);
	Mapping.UVPerY = FVector2d(0,.005);
	FBreachContourOptions Options;
	Options.bEnabled = true;
	Options.VertexCount = 128;
	Options.Seed = 77;
	Options.bRandomizeVertexSpacing = true;
	FString Error;
	for (int32 Case = 0; Case < 3; ++Case)
	{
		Pattern->Guides.Reset();
		for (double Y : {-36.,-12.,12.,36.})
		{
			auto& Points = Pattern->Guides.AddDefaulted_GetRef().Points;
			for (double X : {-100.,-40.,-10.,10.,40.,100.})
			{
				Points.Add(FVector2D(X/200,(Y + FMath::Sin(X*.05)*1.2)/200));
			}
			if (Case == 1)
			{
				Algo::Reverse(Points);
			}
		}
		Options.bMoveCorners = Case != 2;
		TArray<FGeneralPolygon2d> Result;
		TestTrue(TEXT("Curved/reversed guides and strong jitter generate"),BreachPatternGeometry::Generate(*Pattern,Base,Mapping,Options,1,Result,Error));
		if (Result.Num() == 1)
		{
			TestTrue(TEXT("Curved guide fracture validates"),BreachPatternGeometry::ValidateRegions(Result,Error));
			TestTrue(TEXT("Dense input still respects vertex budget"),Result[0].GetOuter().VertexCount() <= 128);
			if (Case == 2)
			{
				for (FVector2d Corner : Base.GetVertices())
				{
					bool bFound = false;
					for (FVector2d P : Result[0].GetOuter().GetVertices())
					{
						bFound |= Corner.Equals(P,1.e-8);
					}
					TestTrue(TEXT("Locked original corners remain present"),bFound);
				}
			}
		}
	}
	Pattern->Guides.Reset();
	for (int32 I = 0; I < 80; ++I)
	{
		const double Y = -40 + I;
		Pattern->Guides.AddDefaulted_GetRef().Points = {{-.5,Y/200},{.5,Y/200}};
	}
	TArray<FGeneralPolygon2d> Preserved{FGeneralPolygon2d(Base)};
	TestFalse(TEXT("Too many authored fracture junctions reject rather than silently changing material rules"),BreachPatternGeometry::Generate(*Pattern,Base,Mapping,Options,1,Preserved,Error));
	TestTrue(TEXT("Rejected dense pattern preserves caller output"),Preserved.Num() == 1 && SameEndBoundary(Preserved[0].GetOuter(),Base));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternGrainEndsSeamTest, "BreachSystem.Pattern.EndSharpness.Seam", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternGrainEndsSeamTest::RunTest(const FString& Parameters)
{
	using namespace UE::Geometry;
	UBreachPattern* Pattern = NewObject<UBreachPattern>();
	Pattern->Mode = EBreachPatternMode::GrainGuides;
	Pattern->GrainEndSharpness = 1;
	for (double Y : {.1,.3,.5,.7,.9})
	{
		Pattern->Guides.AddDefaulted_GetRef().Points = {{0,Y},{1,Y}};
	}
	FBreachPatternMapping Mapping;
	Mapping.UVPerX = FVector2d(.01,0);
	Mapping.UVPerY = FVector2d(0,.01);
	FBreachContourOptions Options;
	Options.bEnabled = false;
	Options.VertexCount = 48;
	Options.Seed = 31;
	TArray<FGeneralPolygon2d> First;
	FString Error;
	FString Export = TEXT("[");
	for (double X : {50.,100.})
	{
		FPolygon2d Base;
		for (int32 I = 0; I < 32; ++I)
		{
			const double A = 2 * UE_PI * I / 32;
			Base.AppendVertex(FVector2d(X + 26*FMath::Cos(A),50 + 26*FMath::Sin(A)));
		}
		TArray<FGeneralPolygon2d> Result;
		TestTrue(TEXT("Periodic constant grain field generates"),BreachPatternGeometry::Generate(*Pattern,Base,Mapping,Options,1,Result,Error));
		if (Result.Num() != 1)
		{
			continue;
		}
		Export += FString::Printf(TEXT("%s{\"x\":%.1f,\"points\":%s}"),Export.Len() > 1 ? TEXT(",") : TEXT(""),X,*EndPointsJSON(Result[0].GetOuter()));
		if (First.IsEmpty())
		{
			First = Result;
		}
		else
		{
			const auto& A = First[0].GetOuter();
			const auto& B = Result[0].GetOuter();
			TestTrue(TEXT("Both seam cases retain the vertex ceiling"),A.VertexCount() <= 128 && B.VertexCount() <= 128);
			FPolygon2d Shifted;
			for (FVector2d P : B.GetVertices())
			{
				Shifted.AppendVertex(P - FVector2d(50,0));
			}
			double Drift = 0;
			for (int32 Direction = 0; Direction < 2; ++Direction)
			{
				const FPolygon2d& From = Direction == 0 ? A : Shifted;
				const FPolygon2d& To = Direction == 0 ? Shifted : A;
				for (int32 I = 0; I < From.VertexCount(); ++I)
				{
					Drift = FMath::Max(Drift,FMath::Sqrt(To.DistanceSquared(From[I])));
					Drift = FMath::Max(Drift,FMath::Sqrt(To.DistanceSquared((From[I] + From[(I + 1) % From.VertexCount()]) * .5)));
				}
			}
			AddInfo(FString::Printf(TEXT("Periodic seam: two-sided boundary deviation %.9f cm"),Drift));
			TestTrue(TEXT("Connected periodic guide ends do not shorten or round the tear"),Drift < 1.e-6);

		}
	}
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/PatternGrainEnds");
	IFileManager::Get().MakeDirectory(*Directory,true);
	TestTrue(TEXT("Export native seam contours"),FFileHelper::SaveStringToFile(Export + TEXT("]"),*(Directory / TEXT("seam.json"))));

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternGrainEndsInfluenceTest, "BreachSystem.Pattern.EndSharpness.Influence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternGrainEndsInfluenceTest::RunTest(const FString& Parameters)
{
	using namespace UE::Geometry;
	UBreachPattern* Pattern = NewObject<UBreachPattern>();
	Pattern->Mode = EBreachPatternMode::GrainGuides;
	Pattern->GrainEndSharpness = 1;
	for (double Y : {-36.,-12.,12.,36.})
	{
		Pattern->Guides.AddDefaulted_GetRef().Points = {{-.5,Y/200},{.5,Y/200}};
	}
	const FPolygon2d Base(TArray<FVector2d>{{-30,-45},{30,-45},{30,45},{-30,45}});
	FBreachPatternMapping Mapping;
	Mapping.UVPerX = FVector2d(.005,0);
	Mapping.UVPerY = FVector2d(0,.005);
	FBreachContourOptions Options;
	Options.bEnabled = true;
	Options.VertexCount = 64;
	Options.Seed = 31;
	for (int32 Fixture = 0; Fixture < 3; ++Fixture)
	{
		Options.VertexCount = Fixture == 2 ? 128 : 64;
		Options.bRandomizeVertexSpacing = Fixture == 0;
		TArray<FGeneralPolygon2d> Plain;
		FString Error;
		TestTrue(TEXT("Zero influence noisy reference"),BreachPatternGeometry::Generate(*Pattern,Base,Mapping,Options,0,Plain,Error));
		double PreviousDrift = TNumericLimits<double>::Max();
		for (double Influence : {1.e-4,1.e-5})
		{
			TArray<FGeneralPolygon2d> Faint;
			TestTrue(TEXT("Tiny positive influence generates"),BreachPatternGeometry::Generate(*Pattern,Base,Mapping,Options,Influence,Faint,Error));
			if (Plain.Num() != 1 || Faint.Num() != 1)
			{
				continue;
			}
			double Drift = 0;
			for (int32 Direction = 0; Direction < 2; ++Direction)
			{
				const FPolygon2d& From = Direction == 0 ? Plain[0].GetOuter() : Faint[0].GetOuter();
				const FPolygon2d& To = Direction == 0 ? Faint[0].GetOuter() : Plain[0].GetOuter();
				for (int32 I = 0; I < From.VertexCount(); ++I)
				{
					Drift = FMath::Max(Drift,FMath::Sqrt(To.DistanceSquared(From[I])));
					Drift = FMath::Max(Drift,FMath::Sqrt(To.DistanceSquared((From[I] + From[(I + 1) % From.VertexCount()]) * .5)));
				}
			}
			const double Cap = 60 * Options.MaxDisplacementFraction;
			// 테스트 전용: 최대 초기 응답 기울기6과 양쪽 변위 한계2*Cap에서 도출한 기하 오차 상한이다.
			TestTrue(TEXT("Tiny influence deviation is bounded proportionally to influence"),Drift <= 12 * Cap * Influence + 1.e-8);
			TestTrue(TEXT("Reducing influence tenfold reduces boundary deviation by at least fivefold"),Drift <= PreviousDrift * .2);
			AddInfo(FString::Printf(TEXT("Fixture%d, influence%.6f: two-sided noisy-boundary deviation %.9f cm"),Fixture,Influence,Drift));
			PreviousDrift = Drift;
		}
	}

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternGrainEndsBendsTest, "BreachSystem.Pattern.EndSharpness.AuthoredBends", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternGrainEndsBendsTest::RunTest(const FString& Parameters)
{
	using namespace BreachGrainFracture;
	const TArray<FGuideSegment> Turn = {{{-10,0},{0,0},0},{{0,0},{0,10},0}};
	FVector2d End;
	TArray<FVector2d> Bends;
	TestTrue(TEXT("Forward traversal follows a legal authored90degree bend"),TraceGuideForTest(Turn,0,.9,2,End,Bends));
	TestTrue(TEXT("Forward traversal reaches beyond the bend"),End.Equals(FVector2d(0,1),1.e-8));
	Bends.Reset();
	TestTrue(TEXT("Reverse traversal follows the authored bend"),TraceGuideForTest(Turn,1,.1,-2,End,Bends));
	TestTrue(TEXT("Reverse traversal reaches beyond the bend"),End.Equals(FVector2d(-1,0),1.e-8));
	UBreachPattern* Pattern = NewObject<UBreachPattern>();
	Pattern->Mode = EBreachPatternMode::GrainGuides;
	Pattern->GrainEndSharpness = 1;
	for (double Y : {-36.,-12.,12.})
	{
		Pattern->Guides.AddDefaulted_GetRef().Points = {{-.5,Y/200},{.5,Y/200}};
	}
	Pattern->Guides.AddDefaulted_GetRef().Points = {{-.5,.18},{-.15,.18},{.015,.195},{.04,.18},{.5,.18}};
	const UE::Geometry::FPolygon2d Base(TArray<FVector2d>{{-30,-45},{30,-45},{30,45},{-30,45}});
	FBreachPatternMapping Mapping;
	Mapping.UVPerX = FVector2d(.005,0);
	Mapping.UVPerY = FVector2d(0,.005);
	FBreachContourOptions Options;
	Options.bEnabled = false;
	Options.VertexCount = 16;
	Options.Seed = 31;
	FString Error;
	TArray<UE::Geometry::FGeneralPolygon2d> Result;
	TestTrue(TEXT("A crown with an authored narrow bend generates"),BreachPatternGeometry::Generate(*Pattern,Base,Mapping,Options,1,Result,Error));
	if (Result.Num() == 1)
	{
		bool bBendPresent = false;
		for (FVector2d P : Result[0].GetOuter().GetVertices())
		{
			bBendPresent |= FMath::Abs(P.X - 3) < 1.e-6 && P.Y > 38;
		}
		TestTrue(TEXT("A long tear retains an authored bend between original sample fractions"),bBendPresent);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBreachPatternEndSharpnessCostTest, "BreachSystem.Pattern.EndSharpness.Cost", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FBreachPatternEndSharpnessCostTest::RunTest(const FString& Parameters)
{
	using namespace UE::Geometry;
	UBreachPattern* Pattern = NewObject<UBreachPattern>();
	Pattern->Mode = EBreachPatternMode::GrainGuides;
	Pattern->GrainMaxEndSplinters = 8;
	for (int32 Y = -60; Y <= 60; Y += 12)
	{
		Pattern->Guides.AddDefaulted_GetRef().Points = {{-.5,Y/200.},{.5,Y/200.}};
	}
	FPolygon2d Base;
	for (FVector2d P : TArray<FVector2d>{{-30,-45},{30,-45},{30,45},{-30,45}})
	{
		Base.AppendVertex(P);
	}
	FBreachPatternMapping Mapping;
	Mapping.UVPerX = FVector2d(.005,0);
	Mapping.UVPerY = FVector2d(0,.005);
	FBreachContourOptions Options;
	Options.bEnabled = true;
	Options.Seed = 31;
	TArray<FGeneralPolygon2d> Out;
	FString Error;
	for (int32 Count : {64,128})
	{
		Options.VertexCount = Count;
		TArray<double> Times[2];
		for (int32 Batch = 0; Batch < 7; ++Batch)
		{
			for (int32 Step = 0; Step < 2; ++Step)
			{
				const int32 Setting = (Batch + Step) % 2;
				Pattern->GrainEndSharpness = Setting;
				const double Start = FPlatformTime::Seconds();
				bool bSuccess = true;
				for (int32 Repeat = 0; Repeat < 80; ++Repeat)
				{
					bSuccess &= BreachPatternGeometry::Generate(*Pattern,Base,Mapping,Options,1,Out,Error);
				}
				Times[Setting].Add((FPlatformTime::Seconds() - Start) * 1.e6 / 80);
				TestTrue(TEXT("Timed complete generator remains valid"),bSuccess);
			}
		}
		Times[0].Sort();
		Times[1].Sort();
		AddInfo(FString::Printf(TEXT("Complete generator including validation (max8 splinters), %d samples: median %.3f us -> %.3f us; delta %.3f us"),Count,Times[0][3],Times[1][3],Times[1][3]-Times[0][3]));
	}
	return !HasAnyErrors();
}
#endif
