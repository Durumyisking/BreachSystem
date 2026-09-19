// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter.h"

DECLARE_LOG_CATEGORY_EXTERN(LogBS, All, All);

namespace BSLog
{
	inline int32 NextIndex()
	{
		static FThreadSafeCounter Counter;
		return Counter.Increment();
	}

	inline FString BuildPrefix(const TCHAR* InCallInfo)
	{
		const FDateTime Now = FDateTime::Now();
		const FString TimeText = FString::Printf(
			TEXT("%04d.%02d.%02d-%02d.%02d.%02d.%03d"),
			Now.GetYear(),
			Now.GetMonth(),
			Now.GetDay(),
			Now.GetHour(),
			Now.GetMinute(),
			Now.GetSecond(),
			Now.GetMillisecond());

		return FString::Printf(TEXT("[#%d][%s][%s]"), NextIndex(), *TimeText, InCallInfo);
	}
}

#define LOG_CALLINFO ANSI_TO_TCHAR(__FUNCTION__)
#define BSLOG(LogCat, Verbosity, Format, ...) UE_LOG(LogCat, Verbosity, TEXT("%s %s"), *BSLog::BuildPrefix(LOG_CALLINFO), *FString::Printf(Format, ##__VA_ARGS__))
#define BS_DISPLAYLOG(Key, Duration, Color, Format, ...) GEngine->AddOnScreenDebugMessage(Key, Duration, Color, *FString::Printf(Format, ##__VA_ARGS__));

#define BS_SLOG(Verbosity, Format, ...) BSLOG(LogBS, Verbosity, Format, ##__VA_ARGS__)
#define SLOG(Format, ...) BSLOG(LogBS, Log, Format, ##__VA_ARGS__)
#define SWARN(Format, ...) BSLOG(LogBS, Warning, Format, ##__VA_ARGS__)
#define SERROR(Format, ...) BSLOG(LogBS, Error, Format, ##__VA_ARGS__)
