
```cpp
const FVector HitWorldPosition = Hit.ImpactPoint;
const FVector HitWorldNormal = Hit.ImpactNormal;

// 좌표변환
const FVector HitLocalPosition = BreachableMeshComponent->GetComponentTransform().InverseTransformPosition(Hit.ImpactPoint);
const FVector HitLocalNormal = BreachableMeshComponent->GetComponentTransform().InverseTransformVectorNoScale(HitWorldNormal);
```
