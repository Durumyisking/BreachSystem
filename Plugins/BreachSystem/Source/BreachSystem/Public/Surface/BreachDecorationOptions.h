#pragma once

#include "CoreMinimal.h"
#include "BreachDecorationOptions.generated.h"

class UMaterialInterface;

/** 피격면은 표면 로컬 Y의 최솟값/최댓값으로 구분한다. */
enum class EBreachSurfaceSide : uint8
{
	Front,
	Back
};

USTRUCT(BlueprintType)
struct BREACHSYSTEM_API FBreachCutDecorationOptions
{
	GENERATED_BODY()

	/** 일반 Surface 머티리얼. UV 0~1에 흔적과 투명한 외곽을 준비한다. 미지정이면 생성하지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Decoration")
	TObjectPtr<UMaterialInterface> Material;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Decoration", meta=(ClampMin="0", Units="cm"))
	float Padding = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Decoration", meta=(ClampMin="0.001", Units="cm"))
	float SurfaceOffset = 0.05f;
};

USTRUCT(BlueprintType)
struct BREACHSYSTEM_API FBreachFeatureDecorationOptions
{
	GENERATED_BODY()

	/** 일반 Surface 머티리얼. 섬유의 빈 부분은 Masked/Opacity Mask로 표현한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Decoration")
	TObjectPtr<UMaterialInterface> Material;

	/** 연결된 실제 제거 영역의 월드 면적(cm²). 이동 충돌의 구멍 기준과 독립적이다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Decoration", meta=(ClampMin="0"))
	float MinHoleArea = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Decoration", meta=(ClampMin="0.1", Units="cm"))
	float Spacing = 19.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Decoration", meta=(ClampMin="0.1", Units="cm"))
	float Width = 22.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Decoration", meta=(ClampMin="0.1", Units="cm"))
	float Length = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Decoration", meta=(ClampMin="0.01", Units="cm"))
	float Thickness = 1.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Decoration", meta=(ClampMin="0", ClampMax="1"))
	float Roughness = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Decoration")
	int32 Seed = 42;

	/** 단일 텍스처 한 장의 월드 가로·세로 크기(cm). Wrap 샘플링을 사용한다. 아틀라스는 기존 셀 맞춤을 유지한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Decoration", meta=(ClampMin="0.1", Units="cm"))
	FVector2D TextureWorldSize = FVector2D(20, 20);

	/** 아틀라스의 열·행 수. 각 셀의 V=0을 뿌리, V=1을 자유단으로 제작한다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Decoration", meta=(ClampMin="1"))
	FIntPoint AtlasSize = FIntPoint(1, 1);
};
