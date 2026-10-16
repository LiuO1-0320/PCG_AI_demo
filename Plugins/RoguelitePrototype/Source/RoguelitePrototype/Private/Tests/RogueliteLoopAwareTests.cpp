#include "RogueliteLoopAwarePlanning.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRogueliteLoopAwareSettingsTest,"Roguelite.LoopAware.SettingsAndVersionIsolation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRogueliteLoopAwareSettingsTest::RunTest(const FString&)
{
    FRogueliteLoopAwareSettings S;FString Why;TestFalse(TEXT("Explicit opt-in required"),FRogueliteLoopAwarePlanner::ValidateSettings(S,Why));S.bEnableLoopAwarePlanning=true;TestTrue(TEXT("Bounded single-loop settings legal"),FRogueliteLoopAwarePlanner::ValidateSettings(S,Why));
    S.MaxLoops=2;TestFalse(TEXT("No false multi-loop claim"),FRogueliteLoopAwarePlanner::ValidateSettings(S,Why));S.MaxLoops=1;S.MinLoops=2;TestFalse(TEXT("Impossible min retained/rejected"),FRogueliteLoopAwarePlanner::ValidateSettings(S,Why));S.MinLoops=0;S.MaxLoops=0;TestTrue(TEXT("Explicit zero loops supported"),FRogueliteLoopAwarePlanner::ValidateSettings(S,Why));
    S.MaxBacktrackStates=0;TestFalse(TEXT("Finite positive search budget required"),FRogueliteLoopAwarePlanner::ValidateSettings(S,Why));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRogueliteLoopAwareSeedTest,"Roguelite.LoopAware.SeedDomainIsolation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRogueliteLoopAwareSeedTest::RunTest(const FString&)
{
    using G=FRogueliteRoomGraphGenerator;FRogueliteGraphGenerationSettings S;S.AlgorithmVersion=1;const auto Before=G::Generate(123,S);
    TSet<int32> Seeds;for(int32 I=0;I<100;++I){const auto A=FRogueliteLoopAwarePlanner::SpatialSeed(I);TestEqual(TEXT("Stable independent spatial seed"),A,FRogueliteLoopAwarePlanner::SpatialSeed(I));Seeds.Add(A);}
    TestEqual(TEXT("100 distinct derived samples"),Seeds.Num(),100);const auto After=G::Generate(123,S);TestTrue(TEXT("V1 unaffected"),Before.bSucceeded&&After.bSucceeded&&Before.Graph.StableDigest==After.Graph.StableDigest);
    TestNotEqual(TEXT("Attempt domains separated"),FRogueliteLoopAwarePlanner::SpatialSeed(1,1,0),FRogueliteLoopAwarePlanner::SpatialSeed(1,1,1));return true;
}
#endif
