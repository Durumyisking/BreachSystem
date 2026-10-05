
```cpp
void UBreachableMeshComponent::ConvertStaticMeshToDynamicMesh(UStaticMesh* InSourceStaticMesh)
{
	if (!InSourceStaticMesh)
	{
		SERROR(TEXT("Invalid Static Mesh"));
		return;
	}

	// DynamicMesh로 변환하기 위한 기존 메시의 Desc를 가져온다.
	FMeshDescription* StaticMeshDescription = InSourceStaticMesh->GetMeshDescription(0);
	if (!StaticMeshDescription)
	{
		SERROR(TEXT("Invalid Static Mesh Description"));
		return;
	}

	UE::Geometry::FDynamicMesh3 DynamicMesh;

	// 이런 멋진 구조체가 존재한다.
	FMeshDescriptionToDynamicMesh Converter;
	Converter.Convert(StaticMeshDescription, DynamicMesh, true);

	// UE식 Rvalue Ref
	SetMesh(MoveTemp(DynamicMesh));
}

```

### 😡 UStaticMesh::GetMeshDescription(Var);

서버를 위해 모듈을 나누던도중..

``` cpp
#if WITH_EDITORONLY_DATA
	/*
	 * Return the MeshDescription associate to the LODIndex. The mesh description can be created on the fly if it was null
	 * and there is a FRawMesh data for this LODIndex.
	 */
	ENGINE_API FMeshDescription* GetMeshDescription(int32 LodIndex) const;

```

이 함수가 EditorOnly라는것을 알아버렸다.

이걸 쓰자
```cpp
/** Return a new StaticMeshDescription referencing the MeshDescription of the given LOD */
	UFUNCTION(BlueprintCallable, Category="StaticMesh")
	ENGINE_API UStaticMeshDescription* GetStaticMeshDescription(int32 LODIndex);

//////////////////////////////////////////////////////////////////////
const FMeshDescription& MeshDescription = StaticMeshDescription->GetMeshDescription();
FMeshDescriptionToDynamicMesh Converter;
Converter.Convert(&MeshDescription, OutMesh, true);
```