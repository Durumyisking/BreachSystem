#include "Data/BreachContourOptions.h"

#if WITH_EDITOR
void FBreachContourOptions::CacheJitterInterval()
{
	if (!FMath::IsFinite(VertexPositionJitter))
	{
		JitterInterval = 1;
		return;
	}
	// Saturate before converting very large, but finite, authored ratios to an integer.
	JitterInterval = VertexPositionJitter >= float(MAX_int32)
		? MAX_int32 : FMath::Max(1, FMath::CeilToInt(VertexPositionJitter));
}
#endif
