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
	/** Wall-damage weight per valid impact; zero keeps direct cutting only. Independent of HP damage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Breach|Accumulation", meta=(ClampMin="0"))
	float BreachDamage = 0.f;

	/** 현재 채택된 커터의 윤곽 변형 설정을 반환한다. 누적 피해 커터도 이 설정을 사용한다. */
	const FBreachContourOptions& GetContourOptions() const
	{
		return BreachSourceOption.Contour;
	}

	/** 원형 커터를 설정한다. MicroDepth는 cm 단위이며, 0이면 윤곽 변형을 끈다. Seed 0은 매번 새 윤곽을 만든다. */
	UFUNCTION(BlueprintCallable, Category="Breach|Binding")
	void BindCircle(float Radius, int32 Segments = 32, float MicroDepth = 0.f, int32 Seed = 0);

	/** 전체 폭·높이와 모서리 반경으로 사각 커터를 설정한다. MicroDepth는 cm 단위이며, Seed 0은 매번 새 윤곽을 만든다. */
	UFUNCTION(BlueprintCallable, Category="Breach|Binding")
	void BindBox(FVector2D Size, float CornerRadius = 0.f, int32 CornerSegments = 8, float MicroDepth = 0.f, int32 Seed = 0);

	/** 새 형상과 윤곽을 준비한 뒤 설정을 교체한다. 잘못된 형상은 거부하고, 윤곽 준비 실패 시 기본 형상을 사용한다. */
	UFUNCTION(BlueprintCallable, Category="Breach")
	void UpdateBreachSourceOption(const FBreachSourceOption& InBreachSourceOption);

	/**
	 * 월드 피격 위치에 커터를 배치하고 표면 로컬 XZ 다각형으로 반환한다. 표면 스케일을 보정해 커터의 월드 크기를 유지한다.
	 * bApplyContour가 false이면 변형 전 형상을 사용한다. X 또는 Z 스케일이 0이거나 커터 준비에 실패하면 false를 반환한다.
	 */
	bool ResolveCutterPolygon(const FTransform& SurfaceTransform, const FVector& HitPosition, UE::Geometry::FGeneralPolygon2d& OutPolygon, bool bApplyContour = true);

protected:
	/** 직렬화된 설정으로 런타임 커터 캐시를 준비한다. */
	virtual void BeginPlay() override;

#if WITH_EDITOR
	/** 에디터에서 로드한 윤곽 설정의 정점 간격 변형 주기를 갱신한다. */
	virtual void PostLoad() override;
	/** 속성 변경을 커터 캐시에 반영한 뒤 부모의 컴포넌트 재구성 처리를 호출한다. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** 현재 설정의 커터를 준비한다. 최초 준비 실패 시 기본 원형을 시도하며, 기존 커터가 있으면 유지하고 false를 반환한다. */
	UFUNCTION(BlueprintCallable, Category="Breach")
	bool CacheCutterPolygon();

	UPROPERTY(EditAnywhere, Category="Breach")
	FBreachSourceOption BreachSourceOption;

	UE::Geometry::FPolygon2d CachedCutterPolygon;

private:
	/** 형상·윤곽을 검증하고 준비 성공 시에만 설정과 캐시를 교체한다. 고정 윤곽은 캐시하고 무작위 윤곽은 생성 준비 상태를 보관한다. */
	bool PrepareCutterPolygon(const FBreachSourceOption& InOption);

	BreachContour::FPreparedContour PreparedContour;
	bool bCutterPrepared = false;
	bool bRandomizeCutter = false;
};
