// Fill out your copyright notice in the Description page of Project Settings.


#include "BreachableMeshActorBase.h"

#include "BreachableMeshComponent.h"
#include "BreachSystemLog.h"


// Sets default values
ABreachableMeshActorBase::ABreachableMeshActorBase()
{
	BreachableMeshComponent = CreateDefaultSubobject<UBreachableMeshComponent>(TEXT("BreachableMeshComponent"));
	RootComponent = BreachableMeshComponent;
	BreachableMeshComponent->SetCollisionEnabled(ECollisionEnabled::Type::QueryOnly);
	BreachableMeshComponent->SetCollisionObjectType(ECC_WorldStatic);
	BreachableMeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
}

void ABreachableMeshActorBase::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (!SourceStaticMesh)
	{
		BreachableMeshComponent->ConvertStaticMeshToDynamicMesh(SourceStaticMesh);
	}
	else
	{
		SWARN(TEXT("Invalid Static Mesh"));
	}
}


// Called every frame
void ABreachableMeshActorBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

