
### 😡 MeshBoolean연산 이후 용접에 실패한  경계 에지가 생기면 Boolean연산에 실패하게 된다.

![[이슈 - MeshCut실패.mkv]]

### 🤨 예상 이유

아래 그림처럼 시작면이 벽과 너무 일치해서 생기는 문제로도 보인다.

![[Pasted image 20260927200350.png]]

### 😊 1차 해결방안

충돌 지점에서 지정한 모양으로 메시를 끝까지 관통시키자
- 총알: 반지름만 지정 → 원형 구멍으로 관통.
- 해머: `300×400`만 지정 → 사각형 구멍으로 관통.

이렇게하면 깊이 설정은 불가능하지만 나는 레인보우식스 시즈 시스템을 만들기 위한거니까 덜 파이는것은 고려하지 않는다.

**대신 단점이 있다** : 기존의 BreachSource들은 사전에 CutMesh를 만들어놓고, 캐시해서 사용하고있었는데.
이러면 충돌되는 CurMesh의 깊이가 BreachableMesh에 따라 CutMesh가 결정나다보니 캐시해놓을수가 없다.

코드는 아래처럼 작성된다.

```cpp
// 대상의 월드 범위에 맞춰 커터를 생성하므로 벽 스케일이 단면 크기를 바꾸지 않는다.
	const FTransformSRT3d MeshTransform(HitComponent->GetComponentTransform()); // 벽의 트랜스폼
	const FAxisAlignedBox3d TargetWorldBounds(SourceMesh->GetBounds(), MeshTransform); // 벽의 바운드를 구해서 정확히 어디서부터 어디까지 Cut해야할지 알아낸다.
	FDynamicMesh3 CutterMesh;
	FTransformSRT3d CutterMeshTransform;
	if (!BreachSourceComponent->GenerateCutterMesh(TargetWorldBounds, Hit.ImpactPoint, Hit.ImpactNormal,
		CutterMesh, CutterMeshTransform))
	{
		return;
	}
	
	FMeshBoolean MeshBoolean(SourceMesh, MeshTransform, &CutterMesh, CutterMeshTransform, &ResultMesh, FMeshBoolean::EBooleanOp::Difference);


```

=> 해결이 되나?
어느정도 완화가 가능하다.

- 바인딩할 때 **단면 크기는 그대로, 길이만 1인 CutMesh**를 만들어 캐시.
- 피격 시 대상 메시를 통과할 길이와 위치 계산.
- Boolean에 넣을 때 **Transform의 길이 축만 늘려서** 사용. 총알 반지름은 유지.

```cpp
const FTransformSRT3d MeshTransform(HitComponent->GetComponentTransform());
	const FAxisAlignedBox3d TargetWorldBounds(SourceMesh->GetBounds(), MeshTransform);
	FTransformSRT3d CutterMeshTransform;
	const FDynamicMesh3* CutterMesh = BreachSourceComponent->ResolveCutterMesh(TargetWorldBounds, Hit.ImpactPoint, Hit.ImpactNormal, CutterMeshTransform);
	if (!CutterMesh)
	{
		return;
	}
	
	const UE::Geometry::FDynamicMesh3* UBreachSourceComponent::ResolveCutterMesh(
	const UE::Geometry::FAxisAlignedBox3d& TargetWorldBounds,
	const FVector& HitPosition, const FVector& HitDirection,
	UE::Geometry::FTransformSRT3d& OutTransform)
{
	using namespace UE::Geometry;

	const FVector3d Direction = HitDirection.GetSafeNormal();
	if (Direction.IsNearlyZero() || TargetWorldBounds.IsEmpty())
	{
		return nullptr;
	}
	if (CachedCutterMesh.TriangleCount() == 0 && !CacheCutterMesh())
	{
		return nullptr;
	}

	// Direction의 길이가 1이므로 내적은 피격점 기준 관통 축의 거리다.
	// 박스의 8개 꼭짓점 중 최소/최대 거리가 대상 전체를 포함하는 구간을 만든다.
	double MinProjection = TNumericLimits<double>::Max();
	double MaxProjection = TNumericLimits<double>::Lowest();
	for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
	{
		const double Projection = (TargetWorldBounds.GetCorner(CornerIndex) - FVector3d(HitPosition)).Dot(Direction);
		MinProjection = FMath::Min(MinProjection, Projection);
		MaxProjection = FMath::Max(MaxProjection, Projection);
	}
	// 커터 뚜껑이 벽 표면과 겹치지 않도록 양 끝을 최소 1cm씩 밖으로 연장한다.
	const double Padding = FMath::Max(1.0, (MaxProjection - MinProjection) * 0.01);
	const double Length = MaxProjection - MinProjection + 2.0 * Padding;
	// 피격점을 지나는 축 위에 커터 중심을 놓는다. 피격점 자체가 중심은 아니다.
	const FVector3d Center = FVector3d(HitPosition) + Direction * ((MinProjection + MaxProjection) * 0.5);
	// Scale→Rotation→Translation 순서다. 로컬 Z만 늘린 후 관통 방향으로 돌리므로
	// X/Y 단면 크기는 벽의 Scale과 관계없이 유지된다.
	OutTransform = FTransformSRT3d(FQuaterniond(FVector3d::UnitZ(), Direction), Center, FVector3d(1.0, 1.0, Length));

	return &CachedCutterMesh;
}

```

![[Pasted image 20260927214811.png]]![[Pasted image 20260927214817.png]]