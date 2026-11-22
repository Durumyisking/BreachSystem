#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "BreachDebrisSubsystem.generated.h"

class AStaticMeshActor;
class UBreachSurfaceMaterial;

/** 월드별 물리 파편 수와 수명을 관리한다. */
UCLASS()
class UBreachDebrisSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void Spawn(const UBreachSurfaceMaterial& Material, const FVector& ImpactPoint, const FVector& Outward, double RemovedAreaCm2);

private:
	TArray<TWeakObjectPtr<AStaticMeshActor>> ActivePieces;
};
