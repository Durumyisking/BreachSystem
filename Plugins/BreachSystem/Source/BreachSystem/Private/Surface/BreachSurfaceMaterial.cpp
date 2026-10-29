#include "Surface/BreachSurfaceMaterial.h"

#if WITH_EDITOR
void UBreachSurfaceMaterial::PostLoad()
{
	Super::PostLoad();
	AccumulationShape.Contour.CacheJitterInterval();
}

void UBreachSurfaceMaterial::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	AccumulationShape.Contour.CacheJitterInterval();
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
