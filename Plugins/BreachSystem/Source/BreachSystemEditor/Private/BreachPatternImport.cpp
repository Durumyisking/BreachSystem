#include "BreachPatternImport.h"
#include "Engine/Texture2D.h"
#include "ImageCore.h"
#include "Surface/BreachPatternGeometry.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    const FIntPoint Cardinal[] = { FIntPoint(1, 0), FIntPoint(0, 1), FIntPoint(-1, 0), FIntPoint(0, -1) };

    int32 Wrap(int32 Value, int32 Period)
    {
        return (Value % Period + Period) % Period;
    }

    int32 PixelIndex(FIntPoint Point, int32 Width, int32 Height)
    {
        return Wrap(Point.Y, Height) * Width + Wrap(Point.X, Width);
    }

    uint32 ColorId(const FColor& Color)
    {
        return (uint32(Color.R) << 16) | (uint32(Color.G) << 8) | uint32(Color.B);
    }

    double Brightness(const FColor& Color)
    {
        return (299.0 * Color.R + 587.0 * Color.G + 114.0 * Color.B) / 255000.0;
    }

    double Cross(const FVector2D& A, const FVector2D& B)
    {
        return A.X * B.Y - A.Y * B.X;
    }

    void RemoveCollinear(TArray<FVector2D>& Points, bool bClosed)
    {
        bool bChanged = true;
        while (bChanged && Points.Num() > (bClosed ? 3 : 2))
        {
            bChanged = false;
            const int32 Start = bClosed ? 0 : 1;
            const int32 End = bClosed ? Points.Num() : Points.Num() - 1;
            for (int32 Index = Start; Index < End; ++Index)
            {
                const FVector2D Previous = Points[(Index + Points.Num() - 1) % Points.Num()];
                const FVector2D Next = Points[(Index + 1) % Points.Num()];
                if (FMath::Abs(Cross(Points[Index] - Previous, Next - Points[Index])) < UE_DOUBLE_KINDA_SMALL_NUMBER * UE_DOUBLE_KINDA_SMALL_NUMBER && FVector2D::DotProduct(Points[Index] - Previous, Next - Points[Index]) >= 0)
                {
                    Points.RemoveAt(Index);
                    bChanged = true;
                    break;
                }
            }
        }
    }

    bool MakeRegion(const TArray<FIntPoint>& Cells, int32 Width, int32 Height, FBreachPatternImportResult& Result)
    {
        TSet<FIntPoint> Occupied;
        FVector2D Center = FVector2D::ZeroVector;
        for (const FIntPoint& Cell : Cells)
        {
            Occupied.Add(Cell);
            Center += FVector2D(Cell.X + .5, Cell.Y + .5);
        }
        Center /= Cells.Num();
        const FIntPoint TileShift(FMath::FloorToInt(Center.X / Width) * Width, FMath::FloorToInt(Center.Y / Height) * Height);
        TMap<FIntPoint, FIntPoint> Edges;
        auto AddEdge = [&Edges, &Result](FIntPoint A, FIntPoint B)
        {
            if (Edges.Contains(A))
            {
                Result.Error = TEXT("An ID boundary touches itself at a pixel corner. Separate the touching regions by a pixel before importing.");
                return false;
            }
            Edges.Add(A, B);
            return true;
        };
        for (const FIntPoint& Cell : Cells)
        {
            const FIntPoint Right = Cell + FIntPoint(1, 0);
            const FIntPoint Bottom = Cell + FIntPoint(0, 1);
            const FIntPoint Corner = Cell + FIntPoint(1, 1);
            if (!Occupied.Contains(Cell + FIntPoint(0, -1)))
            {
                if (!AddEdge(Cell, Right))
                {
                    return false;
                }
            }
            if (!Occupied.Contains(Right))
            {
                if (!AddEdge(Right, Corner))
                {
                    return false;
                }
            }
            if (!Occupied.Contains(Bottom))
            {
                if (!AddEdge(Corner, Bottom))
                {
                    return false;
                }
            }
            if (!Occupied.Contains(Cell + FIntPoint(-1, 0)))
            {
                if (!AddEdge(Bottom, Cell))
                {
                    return false;
                }
            }
        }

        FBreachPatternRegion Region;
        while (Edges.Num() > 0)
        {
            FIntPoint Start = Edges.CreateConstIterator().Key();
            // Stable loop start makes preview and reimport independent of hash iteration order.
            for (const TPair<FIntPoint, FIntPoint>& Edge : Edges)
            {
                if (Edge.Key.Y < Start.Y || (Edge.Key.Y == Start.Y && Edge.Key.X < Start.X))
                {
                    Start = Edge.Key;
                }
            }
            FIntPoint Current = Start;
            TArray<FVector2D> Loop;
            do
            {
                const FIntPoint* Next = Edges.Find(Current);
                if (!Next)
                {
                    Result.Error = TEXT("ID boundary could not be closed without an ambiguous corner.");
                    return false;
                }
                const FIntPoint NextPoint = *Next;
                Loop.Add(FVector2D(double(Current.X - TileShift.X) / Width, double(Current.Y - TileShift.Y) / Height));
                Edges.Remove(Current);
                Current = NextPoint;
            } while (Current != Start);
            RemoveCollinear(Loop, true);
            double Area = 0;
            for (int32 Index = 0; Index < Loop.Num(); ++Index)
            {
                Area += Cross(Loop[Index], Loop[(Index + 1) % Loop.Num()]);
            }
            if (Area > 0)
            {
                if (!Region.Outer.IsEmpty())
                {
                    Result.Error = TEXT("One connected ID component produced multiple outer boundaries.");
                    return false;
                }
                Region.Outer = MoveTemp(Loop);
            }
            else
            {
                FBreachPatternLoop Hole;
                Hole.Vertices = MoveTemp(Loop);
                Region.Holes.Add(MoveTemp(Hole));
            }
        }
        if (Region.Outer.IsEmpty())
        {
            Result.Error = TEXT("ID component has no bounded outer boundary.");
            return false;
        }
        Result.Regions.Add(MoveTemp(Region));
        return true;
    }

    bool ImportRegions(int32 Width, int32 Height, const TArray<FColor>& Pixels, const FBreachPatternImportOptions& Options, FBreachPatternImportResult& Result)
    {
        TArray<int32> Labels;
        Labels.Init(INDEX_NONE, Pixels.Num());
        TArray<FIntPoint> Lifted;
        Lifted.SetNumUninitialized(Pixels.Num());
        int32 Component = 0;
        for (int32 Seed = 0; Seed < Pixels.Num(); ++Seed)
        {
            if (Labels[Seed] != INDEX_NONE || Pixels[Seed].A == 0)
            {
                continue;
            }
            const uint32 Id = ColorId(Pixels[Seed]);
            TArray<int32> Queue;
            Queue.Add(Seed);
            Labels[Seed] = Component;
            Lifted[Seed] = FIntPoint(Seed % Width, Seed / Width);
            bool bWindingX = false;
            bool bWindingY = false;
            for (int32 Cursor = 0; Cursor < Queue.Num(); ++Cursor)
            {
                const int32 Index = Queue[Cursor];
                for (const FIntPoint& Delta : Cardinal)
                {
                    const FIntPoint Position = Lifted[Index] + Delta;
                    const int32 Neighbor = PixelIndex(Position, Width, Height);
                    if (Pixels[Neighbor].A == 0 || ColorId(Pixels[Neighbor]) != Id)
                    {
                        continue;
                    }
                    if (Labels[Neighbor] == INDEX_NONE)
                    {
                        Labels[Neighbor] = Component;
                        Lifted[Neighbor] = Position;
                        Queue.Add(Neighbor);
                    }
                    else if (Labels[Neighbor] == Component && Lifted[Neighbor] != Position)
                    {
                        bWindingX |= Lifted[Neighbor].X != Position.X;
                        bWindingY |= Lifted[Neighbor].Y != Position.Y;
                    }
                }
            }
            ++Component;
            if (Queue.Num() < Options.MinFeaturePixels)
            {
                Result.Warnings.AddUnique(TEXT("An ID component smaller than MinFeaturePixels was omitted."));
                continue;
            }
            if (!bWindingX && !bWindingY)
            {
                TArray<FIntPoint> Cells;
                for (int32 Index : Queue)
                {
                    Cells.Add(Lifted[Index]);
                }
                if (!MakeRegion(Cells, Width, Height, Result))
                {
                    return false;
                }
                continue;
            }

            // Cut only winding axes. A vertical stripe can wind in Y while its X seam
            // still has a finite lift and must remain one region across that seam.
            Result.Warnings.AddUnique(TEXT("A region winds around the repeating tile. Only its winding axes are tile-clipped; these tile-border edges remain visible during per-region damage."));
            TMap<FIntPoint, int32> PlanarPixels;
            for (int32 Index : Queue)
            {
                if (bWindingX)
                {
                    Lifted[Index].X = Index % Width;
                }
                if (bWindingY)
                {
                    Lifted[Index].Y = Index / Width;
                }
                PlanarPixels.Add(Lifted[Index], Index);
            }
            TSet<int32> Remaining(Queue);
            while (!Remaining.IsEmpty())
            {
                int32 PlanarSeed = MAX_int32;
                for (int32 Index : Remaining)
                {
                    PlanarSeed = FMath::Min(PlanarSeed, Index);
                }
                TArray<int32> PlanarQueue;
                PlanarQueue.Add(PlanarSeed);
                Remaining.Remove(PlanarSeed);
                TArray<FIntPoint> Cells;
                for (int32 Cursor = 0; Cursor < PlanarQueue.Num(); ++Cursor)
                {
                    const int32 Index = PlanarQueue[Cursor];
                    const FIntPoint Position = Lifted[Index];
                    Cells.Add(Position);
                    for (const FIntPoint& Delta : Cardinal)
                    {
                        const FIntPoint Neighbor = Position + Delta;
                        const int32* NeighborIndex = PlanarPixels.Find(Neighbor);
                        if (!NeighborIndex)
                        {
                            continue;
                        }
                        if (Remaining.Remove(*NeighborIndex) > 0)
                        {
                            PlanarQueue.Add(*NeighborIndex);
                        }
                    }
                }
                if (!MakeRegion(Cells, Width, Height, Result))
                {
                    return false;
                }
            }
        }
        return !Result.Regions.IsEmpty();
    }

    void Thin(TArray<uint8>& Mask, int32 Width, int32 Height)
    {
        const FIntPoint Ring[] = { FIntPoint(0, -1), FIntPoint(1, -1), FIntPoint(1, 0), FIntPoint(1, 1), FIntPoint(0, 1), FIntPoint(-1, 1), FIntPoint(-1, 0), FIntPoint(-1, -1) };
        bool bChanged = true;
        while (bChanged)
        {
            bChanged = false;
            for (int32 Pass = 0; Pass < 2; ++Pass)
            {
                TArray<int32> Remove;
                for (int32 Index = 0; Index < Mask.Num(); ++Index)
                {
                    if (!Mask[Index])
                    {
                        continue;
                    }
                    int32 Neighbor[8];
                    int32 Count = 0;
                    int32 Transitions = 0;
                    const FIntPoint Point(Index % Width, Index / Width);
                    for (int32 N = 0; N < 8; ++N)
                    {
                        Neighbor[N] = Mask[PixelIndex(Point + Ring[N], Width, Height)];
                        Count += Neighbor[N];
                    }
                    for (int32 N = 0; N < 8; ++N)
                    {
                        if (!Neighbor[N] && Neighbor[(N + 1) % 8])
                        {
                            ++Transitions;
                        }
                    }
                    if (Count < 2 || Count > 6 || Transitions != 1)
                    {
                        continue;
                    }
                    const bool bFirstBlocked = Pass == 0 ? Neighbor[0] && Neighbor[2] && Neighbor[4] : Neighbor[0] && Neighbor[2] && Neighbor[6];
                    const bool bSecondBlocked = Pass == 0 ? Neighbor[2] && Neighbor[4] && Neighbor[6] : Neighbor[0] && Neighbor[4] && Neighbor[6];
                    if (!bFirstBlocked && !bSecondBlocked)
                    {
                        Remove.Add(Index);
                    }
                }
                for (int32 Index : Remove)
                {
                    Mask[Index] = 0;
                }
                bChanged |= !Remove.IsEmpty();
            }
        }
    }

    void SimplifyGuide(TArray<FVector2D>& Points, double Tolerance)
    {
        RemoveCollinear(Points, false);
        if (Tolerance <= 0 || Points.Num() < 3)
        {
            return;
        }
        TArray<uint8> Keep;
        Keep.Init(0, Points.Num());
        Keep[0] = 1;
        Keep.Last() = 1;
        TArray<FIntPoint> Ranges;
        Ranges.Add(FIntPoint(0, Points.Num() - 1));
        while (!Ranges.IsEmpty())
        {
            const FIntPoint Range = Ranges.Pop(EAllowShrinking::No);
            const FVector2D A = Points[Range.X];
            const FVector2D Direction = Points[Range.Y] - A;
            const double LengthSquared = Direction.SizeSquared();
            double MaxDistance = Tolerance * Tolerance;
            int32 Furthest = INDEX_NONE;
            for (int32 Index = Range.X + 1; Index < Range.Y; ++Index)
            {
                const double Alpha = LengthSquared > 0 ? FMath::Clamp(FVector2D::DotProduct(Points[Index] - A, Direction) / LengthSquared, 0.0, 1.0) : 0;
                const double Distance = FVector2D::DistSquared(Points[Index], A + Alpha * Direction);
                if (Distance > MaxDistance)
                {
                    MaxDistance = Distance;
                    Furthest = Index;
                }
            }
            if (Furthest != INDEX_NONE)
            {
                Keep[Furthest] = 1;
                Ranges.Add(FIntPoint(Range.X, Furthest));
                Ranges.Add(FIntPoint(Furthest, Range.Y));
            }
        }
        for (int32 Index = Points.Num() - 1; Index >= 0; --Index)
        {
            if (!Keep[Index])
            {
                Points.RemoveAt(Index);
            }
        }
    }

    uint64 EdgeKey(int32 A, int32 B)
    {
        return (uint64(uint32(FMath::Min(A, B))) << 32) | uint32(FMath::Max(A, B));
    }

    bool ImportGuides(int32 Width, int32 Height, TArray<uint8> Mask, const FBreachPatternImportOptions& Options, FBreachPatternImportResult& Result)
    {
        // Remove tiny authored marks before thinning, so the filter measures source area.
        TArray<uint8> Seen;
        Seen.Init(0, Mask.Num());
        for (int32 Seed = 0; Seed < Mask.Num(); ++Seed)
        {
            if (!Mask[Seed] || Seen[Seed])
            {
                continue;
            }
            TArray<int32> Queue;
            Queue.Add(Seed);
            Seen[Seed] = 1;
            for (int32 Cursor = 0; Cursor < Queue.Num(); ++Cursor)
            {
                const FIntPoint Point(Queue[Cursor] % Width, Queue[Cursor] / Width);
                for (int32 DY = -1; DY <= 1; ++DY)
                {
                    for (int32 DX = -1; DX <= 1; ++DX)
                    {
                        const int32 Next = PixelIndex(Point + FIntPoint(DX, DY), Width, Height);
                        if (Mask[Next] && !Seen[Next])
                        {
                            Seen[Next] = 1;
                            Queue.Add(Next);
                        }
                    }
                }
            }
            if (Queue.Num() < Options.MinFeaturePixels)
            {
                for (int32 Index : Queue)
                {
                    Mask[Index] = 0;
                }
                Result.Warnings.AddUnique(TEXT("A guide mark smaller than MinFeaturePixels was omitted."));
            }
        }
        Thin(Mask, Width, Height);
        for (int32 Y = 0; Y < Height; ++Y)
        {
            for (int32 X = 0; X < Width; ++X)
            {
                if (Mask[PixelIndex(FIntPoint(X, Y), Width, Height)] && Mask[PixelIndex(FIntPoint(X + 1, Y), Width, Height)] && Mask[PixelIndex(FIntPoint(X, Y + 1), Width, Height)] && Mask[PixelIndex(FIntPoint(X + 1, Y + 1), Width, Height)])
                {
                    Result.Error = TEXT("The guide image remains filled after thinning. Draw separated bright strokes with a dark background to define an unambiguous centerline.");
                    return false;
                }
            }
        }
        TArray<TArray<int32>> Neighbors;
        Neighbors.SetNum(Mask.Num());
        for (int32 Index = 0; Index < Mask.Num(); ++Index)
        {
            if (!Mask[Index])
            {
                continue;
            }
            const FIntPoint Point(Index % Width, Index / Width);
            for (int32 DY = -1; DY <= 1; ++DY)
            {
                for (int32 DX = -1; DX <= 1; ++DX)
                {
                    if (DX == 0 && DY == 0)
                    {
                        continue;
                    }
                    const int32 Next = PixelIndex(Point + FIntPoint(DX, DY), Width, Height);
                    if (!Mask[Next] || Next == Index)
                    {
                        continue;
                    }
                    // A diagonal next to an orthogonal connection would create a false triangle.
                    if (DX != 0 && DY != 0)
                    {
                        if (Mask[PixelIndex(Point + FIntPoint(DX, 0), Width, Height)] || Mask[PixelIndex(Point + FIntPoint(0, DY), Width, Height)])
                        {
                            continue;
                        }
                    }
                    Neighbors[Index].AddUnique(Next);
                }
            }
        }
        TSet<uint64> Used;
        auto Trace = [&](int32 Start, int32 First)
        {
            FBreachPatternGuide Guide;
            FIntPoint Position(Start % Width, Start / Width);
            Guide.Points.Add(FVector2D((Position.X + .5) / Width, (Position.Y + .5) / Height));
            int32 Previous = Start;
            int32 Current = First;
            for (;;)
            {
                Used.Add(EdgeKey(Previous, Current));
                FIntPoint Delta(Current % Width - Previous % Width, Current / Width - Previous / Width);
                if (Delta.X > 1)
                {
                    Delta.X -= Width;
                }
                if (Delta.X < -1)
                {
                    Delta.X += Width;
                }
                if (Delta.Y > 1)
                {
                    Delta.Y -= Height;
                }
                if (Delta.Y < -1)
                {
                    Delta.Y += Height;
                }
                Position += Delta;
                Guide.Points.Add(FVector2D((Position.X + .5) / Width, (Position.Y + .5) / Height));
                if (Current == Start || Neighbors[Current].Num() != 2)
                {
                    break;
                }
                const int32 Next = Neighbors[Current][0] == Previous ? Neighbors[Current][1] : Neighbors[Current][0];
                if (Used.Contains(EdgeKey(Current, Next)))
                {
                    Result.Error = TEXT("A guide trace revisits an edge; simplify the ambiguous guide junction.");
                    return false;
                }
                Previous = Current;
                Current = Next;
            }
            if (Guide.Points[0].Equals(Guide.Points.Last(), UE_DOUBLE_KINDA_SMALL_NUMBER))
            {
                Result.Error = TEXT("Closed guide loops are unsupported. Break the loop to make an open grain guide; it will not be converted into a filled region.");
                return false;
            }
            RemoveCollinear(Guide.Points, false);
            Result.Guides.Add(MoveTemp(Guide));
            return true;
        };
        for (int32 Index = 0; Index < Mask.Num(); ++Index)
        {
            if (!Mask[Index] || Neighbors[Index].Num() == 2)
            {
                continue;
            }
            if (Neighbors[Index].IsEmpty())
            {
                Result.Warnings.AddUnique(TEXT("An isolated guide pixel has no line direction and was omitted."));
            }
            for (int32 Next : Neighbors[Index])
            {
                if (!Used.Contains(EdgeKey(Index, Next)))
                {
                    if (!Trace(Index, Next))
                    {
                        return false;
                    }
                }
            }
        }
        // Remaining degree-two components are loops, possibly open in the unwrapped tile.
        for (int32 Index = 0; Index < Mask.Num(); ++Index)
        {
            for (int32 Next : Neighbors[Index])
            {
                if (!Used.Contains(EdgeKey(Index, Next)))
                {
                    if (!Trace(Index, Next))
                    {
                        return false;
                    }
                }
            }
        }
        return !Result.Guides.IsEmpty();
    }

    bool NewGuideCrossing(const TArray<FBreachPatternGuide>& Guides)
    {
        for (int32 A = 0; A < Guides.Num(); ++A)
        {
            for (int32 B = A + 1; B < Guides.Num(); ++B)
            {
                for (int32 I = 1; I < Guides[A].Points.Num(); ++I)
                {
                    const FVector2D P = Guides[A].Points[I - 1];
                    const FVector2D Q = Guides[A].Points[I];
                    for (int32 J = 1; J < Guides[B].Points.Num(); ++J)
                    {
                        const FVector2D R = Guides[B].Points[J - 1];
                        const FVector2D S = Guides[B].Points[J];
                        if (FMath::Max(P.X, Q.X) < FMath::Min(R.X, S.X) - UE_DOUBLE_KINDA_SMALL_NUMBER || FMath::Max(R.X, S.X) < FMath::Min(P.X, Q.X) - UE_DOUBLE_KINDA_SMALL_NUMBER || FMath::Max(P.Y, Q.Y) < FMath::Min(R.Y, S.Y) - UE_DOUBLE_KINDA_SMALL_NUMBER || FMath::Max(R.Y, S.Y) < FMath::Min(P.Y, Q.Y) - UE_DOUBLE_KINDA_SMALL_NUMBER)
                        {
                            continue;
                        }
                        const double C1 = Cross(Q - P, R - P);
                        const double C2 = Cross(Q - P, S - P);
                        const double C3 = Cross(S - R, P - R);
                        const double C4 = Cross(S - R, Q - R);
                        if ((C1 > UE_DOUBLE_KINDA_SMALL_NUMBER * UE_DOUBLE_KINDA_SMALL_NUMBER && C2 > UE_DOUBLE_KINDA_SMALL_NUMBER * UE_DOUBLE_KINDA_SMALL_NUMBER) || (C1 < -UE_DOUBLE_KINDA_SMALL_NUMBER * UE_DOUBLE_KINDA_SMALL_NUMBER && C2 < -UE_DOUBLE_KINDA_SMALL_NUMBER * UE_DOUBLE_KINDA_SMALL_NUMBER) || (C3 > UE_DOUBLE_KINDA_SMALL_NUMBER * UE_DOUBLE_KINDA_SMALL_NUMBER && C4 > UE_DOUBLE_KINDA_SMALL_NUMBER * UE_DOUBLE_KINDA_SMALL_NUMBER) || (C3 < -UE_DOUBLE_KINDA_SMALL_NUMBER * UE_DOUBLE_KINDA_SMALL_NUMBER && C4 < -UE_DOUBLE_KINDA_SMALL_NUMBER * UE_DOUBLE_KINDA_SMALL_NUMBER))
                        {
                            continue;
                        }
                        const bool bSharedEndpoint = P.Equals(R, UE_DOUBLE_KINDA_SMALL_NUMBER) || P.Equals(S, UE_DOUBLE_KINDA_SMALL_NUMBER) || Q.Equals(R, UE_DOUBLE_KINDA_SMALL_NUMBER) || Q.Equals(S, UE_DOUBLE_KINDA_SMALL_NUMBER);
                        if (!bSharedEndpoint)
                        {
                            return true;
                        }
                        if (FMath::Abs(C1) < UE_DOUBLE_KINDA_SMALL_NUMBER * UE_DOUBLE_KINDA_SMALL_NUMBER && FMath::Abs(C2) < UE_DOUBLE_KINDA_SMALL_NUMBER * UE_DOUBLE_KINDA_SMALL_NUMBER)
                        {
                            const FVector2D Axis = (Q - P).GetSafeNormal();
                            const double RProjection = FVector2D::DotProduct(R - P, Axis);
                            const double SProjection = FVector2D::DotProduct(S - P, Axis);
                            const double Overlap = FMath::Min((Q - P).Size(), FMath::Max(RProjection, SProjection)) - FMath::Max(0.0, FMath::Min(RProjection, SProjection));
                            if (Overlap > UE_DOUBLE_KINDA_SMALL_NUMBER)
                            {
                                return true;
                            }
                        }
                    }
                }
            }
        }
        return false;
    }

    bool ValidateStaged(const FBreachPatternImportResult& Result, FString& Error)
    {
        UBreachPattern* Staged = NewObject<UBreachPattern>(GetTransientPackage());
        check(Staged);
        Staged->Mode = Result.Mode;
        Staged->Regions = Result.Regions;
        Staged->Guides = Result.Guides;
        return BreachPatternGeometry::Validate(*Staged, Error);
    }
}

bool BreachPatternImport::Preview(UTexture2D* Texture, const FBreachPatternImportOptions& Options, FBreachPatternImportResult& OutResult)
{
    OutResult = FBreachPatternImportResult();
    if (!Texture)
    {
        OutResult.Error = TEXT("Select a reference Texture2D before creating an import preview.");
        return false;
    }
    if (!Texture->Source.IsValid())
    {
        OutResult.Error = TEXT("The texture has no Editor source image. Reimport the original source file before tracing.");
        return false;
    }
    if (Texture->Source.GetNumBlocks() != 1 || Texture->Source.GetNumLayers() != 1)
    {
        OutResult.Error = TEXT("Import requires a single-block, single-layer source image. UDIM and layered textures are unsupported.");
        return false;
    }
    FImage Image;
    if (!Texture->Source.GetMipImage(Image, 0))
    {
        OutResult.Error = TEXT("The texture has no readable Editor source mip. Reimport its source image; runtime compressed mips are not used.");
        return false;
    }
    if (Image.NumSlices != 1)
    {
        OutResult.Error = TEXT("Import requires one source image slice.");
        return false;
    }
    if (Options.Mode == EBreachPatternImportMode::IdMap && Image.Format != ERawImageFormat::BGRA8 && Image.Format != ERawImageFormat::G8)
    {
        OutResult.Error = TEXT("Exact ID maps require an 8-bit RGB or grayscale source. HDR or higher-bit IDs cannot be quantized without changing region identity.");
        return false;
    }
    if (Image.Format != ERawImageFormat::BGRA8)
    {
        FImage Converted;
        Image.CopyTo(Converted, ERawImageFormat::BGRA8, Image.GammaSpace);
        Image = MoveTemp(Converted);
    }
    TArray<FColor> Pixels;
    if (int64(Image.SizeX) * Image.SizeY > MAX_int32)
    {
        OutResult.Error = TEXT("The source image is too large to import.");
        return false;
    }
    Pixels.Append(Image.AsBGRA8().GetData(), int32(Image.AsBGRA8().Num()));
    return FromPixels(Image.SizeX, Image.SizeY, Pixels, Options, OutResult);
}

bool BreachPatternImport::FromPixels(int32 Width, int32 Height, const TArray<FColor>& Pixels, const FBreachPatternImportOptions& Options, FBreachPatternImportResult& OutResult)
{
    OutResult = FBreachPatternImportResult();
    if (Width < 1 || Height < 1 || int64(Width) * Height != Pixels.Num())
    {
        OutResult.Error = TEXT("Source pixels must match a source image with positive dimensions.");
        return false;
    }
    if (!FMath::IsFinite(Options.Threshold) || Options.Threshold < 0 || Options.Threshold > 1 || Options.MinFeaturePixels < 1 || !FMath::IsFinite(Options.Simplification) || Options.Simplification < 0)
    {
        OutResult.Error = TEXT("Threshold must be in [0,1], MinFeaturePixels at least one, and Simplification finite and nonnegative.");
        return false;
    }
    FBreachPatternImportResult Staged;
    Staged.Mode = Options.Mode == EBreachPatternImportMode::IdMap ? EBreachPatternMode::Regions : Options.Mode == EBreachPatternImportMode::GuideMap ? EBreachPatternMode::GrainGuides : Options.TargetMode;
    if (Staged.Mode != EBreachPatternMode::Regions && Staged.Mode != EBreachPatternMode::GrainGuides)
    {
        OutResult.Error = TEXT("Photo imports require either Regions or GrainGuides as the target mode.");
        return false;
    }
    if (Staged.Mode == EBreachPatternMode::GrainGuides && (Width < 3 || Height < 3))
    {
        OutResult.Error = TEXT("Guide tracing requires a source image at least 3 by 3 pixels in size.");
        return false;
    }
    bool bGenerated = false;
    if (Staged.Mode == EBreachPatternMode::Regions)
    {
        TArray<FColor> RegionPixels = Pixels;
        if (Options.Mode == EBreachPatternImportMode::Photo)
        {
            for (FColor& Pixel : RegionPixels)
            {
                Pixel = Pixel.A > 0 && Brightness(Pixel) >= Options.Threshold ? FColor::White : FColor(0, 0, 0, 0);
            }
            Staged.Warnings.AddUnique(TEXT("Photo region draft uses a luminance threshold. Review and edit the traced regions before Apply."));
        }
        bGenerated = ImportRegions(Width, Height, RegionPixels, Options, Staged);
        if (Options.Simplification > 0)
        {
            Staged.Warnings.AddUnique(TEXT("Region simplification removes only collinear pixel-edge vertices to keep neighboring ID boundaries exact and crack-free."));
        }
    }
    else
    {
        TArray<uint8> Mask;
        Mask.SetNumUninitialized(Pixels.Num());
        for (int32 Index = 0; Index < Pixels.Num(); ++Index)
        {
            double Value = Brightness(Pixels[Index]);
            if (Options.Mode == EBreachPatternImportMode::Photo)
            {
                const FIntPoint Point(Index % Width, Index / Width);
                const double DX = FMath::Abs(Brightness(Pixels[PixelIndex(Point + FIntPoint(1, 0), Width, Height)]) - Brightness(Pixels[PixelIndex(Point - FIntPoint(1, 0), Width, Height)]));
                const double DY = FMath::Abs(Brightness(Pixels[PixelIndex(Point + FIntPoint(0, 1), Width, Height)]) - Brightness(Pixels[PixelIndex(Point - FIntPoint(0, 1), Width, Height)]));
                Value = FMath::Max(DX, DY);
            }
            Mask[Index] = Pixels[Index].A > 0 && Value >= Options.Threshold ? 1 : 0;
        }
        bGenerated = ImportGuides(Width, Height, MoveTemp(Mask), Options, Staged);
        if (Options.Mode == EBreachPatternImportMode::Photo)
        {
            Staged.Warnings.AddUnique(TEXT("Photo guide draft uses a luminance-edge threshold and thinning. Review open paths and junctions before Apply."));
        }
    }
    if (!bGenerated)
    {
        OutResult.Error = Staged.Error.IsEmpty() ? TEXT("No usable features remain. Adjust Threshold or MinFeaturePixels.") : Staged.Error;
        OutResult.Warnings = MoveTemp(Staged.Warnings);
        return false;
    }
    if (!ValidateStaged(Staged, Staged.Error))
    {
        OutResult.Error = Staged.Error;
        OutResult.Warnings = MoveTemp(Staged.Warnings);
        return false;
    }
    if (Staged.Mode == EBreachPatternMode::GrainGuides && Options.Simplification > 0)
    {
        const TArray<FBreachPatternGuide> Original = Staged.Guides;
        for (FBreachPatternGuide& Guide : Staged.Guides)
        {
            SimplifyGuide(Guide.Points, Options.Simplification);
        }
        bool bSimplificationValid = !NewGuideCrossing(Staged.Guides);
        if (bSimplificationValid)
        {
            bSimplificationValid = ValidateStaged(Staged, Staged.Error);
        }
        if (!bSimplificationValid)
        {
            Staged.Guides = Original;
            Staged.Error.Empty();
            Staged.Warnings.AddUnique(TEXT("Guide simplification would change valid path topology; the exact traced paths were kept."));
        }
    }
    OutResult = MoveTemp(Staged);
    return true;
}
