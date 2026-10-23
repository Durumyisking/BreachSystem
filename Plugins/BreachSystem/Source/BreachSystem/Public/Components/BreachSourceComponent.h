#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BreachSourceOption.h"
#include "Curve/GeneralPolygon2.h"
#include "BreachSourceComponent.generated.h"

UCLASS(ClassGroup=(Breach), meta=(BlueprintSpawnableComponent))
class BREACHSYSTEM_API UBreachSourceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Breach|Binding")
	void BindCircle(float Radius, int32 Segments = 32, float MicroDepth = 0.f, int32 Seed = 0);

	UFUNCTION(BlueprintCallable, Category="Breach|Binding")
	void BindBox(FVector2D Size, float CornerRadius = 0.f, int32 CornerSegments = 8, float MicroDepth = 0.f, int32 Seed = 0);

	UFUNCTION(BlueprintCallable, Category="Breach")
	void UpdateBreachSourceOption(const FBreachSourceOption& InBreachSourceOption);

	bool ResolveCutterPolygon(const FTransform& SurfaceTransform, const FVector& HitPosition, UE::Geometry::FGeneralPolygon2d& OutPolygon);

protected:
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintCallable, Category="Breach")
	bool CacheCutterPolygon();

	UPROPERTY(EditAnywhere, Category="Breach")
	FBreachSourceOption BreachSourceOption;

	UE::Geometry::FPolygon2d CachedCutterPolygon;

private:
	bool ShouldCacheCutterPolygon() const;
	bool GenerateCutterPolygon(UE::Geometry::FPolygon2d& OutPolygon) const;
};
