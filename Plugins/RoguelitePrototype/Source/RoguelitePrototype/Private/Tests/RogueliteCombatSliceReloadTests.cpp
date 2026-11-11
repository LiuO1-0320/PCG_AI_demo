#include "Misc/AutomationTest.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
namespace CombatSliceEncounterVerification { bool Queue(FAutomationTestBase*,const FString&); }
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FG1BReloadRecovery,"ProjectVerification.G1B.FIX02",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
void FG1BReloadRecovery::GetTests(TArray<FString>& Names,TArray<FString>& Commands)const
{
    Names={TEXT("ReloadEmptyEntry"),TEXT("ReloadCombatEmpty"),TEXT("ReloadPartialFullSeed1"),TEXT("ReloadLifeDeath"),TEXT("ReloadLifeClear"),TEXT("ReloadLifeBatchStale"),TEXT("ReloadLifeStopPIE")};Commands=Names;
}
bool FG1BReloadRecovery::RunTest(const FString& Scenario){return CombatSliceEncounterVerification::Queue(this,Scenario);}
#endif
