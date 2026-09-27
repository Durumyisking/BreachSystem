#include "BreachableDynamicMeshComponent.h"
#include "BreachableMeshComponent.h"

void UBreachableDynamicMeshComponent::ReceiveComponentDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	Super::ReceiveComponentDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (UBreachableMeshComponent* Mesh = BreachableMesh.Get())
	{
		Mesh->HandleBreachDamage(this, DamageEvent, DamageCauser);
	}
}
