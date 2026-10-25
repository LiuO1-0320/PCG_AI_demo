#pragma once
#include "CoreMinimal.h"
#include "RogueliteLoopAwarePlanning.h"
struct ROGUELITEPROTOTYPE_API FRogueliteCompactCorridorMeasurement
{
    int32 EdgeId=-1;bool bLoop=false,bPassed=false;float NativeLength=0;double SocketDistance=0,StartError=0,EndError=0;FString Reason;
};
/** FloorProfile-scoped policy. Legacy C1/C2/planner defaults remain unchanged. */
class ROGUELITEPROTOTYPE_API FRogueliteCompactCorridorContract
{
public:
    static constexpr float MinCm=500;
    static constexpr float MaxCm=1000;
    static bool Configuration(const FRogueliteSeedLayoutSettings& Source,const FRogueliteLoopAwareSettings& Planning,FString& Why);
    static bool Plan(const FRogueliteLoopAwareSpatialPlan& Plan,FString& Why);
    static bool Actual(const FRogueliteSeedBatch& Batch,const FRogueliteCommittedLoopBatch& Loops,TArray<FRogueliteCompactCorridorMeasurement>& Measurements,FString& Why);
};
