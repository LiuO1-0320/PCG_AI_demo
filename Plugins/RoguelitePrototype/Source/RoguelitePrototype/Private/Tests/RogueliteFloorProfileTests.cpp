#include "RogueliteFloorProfile.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFloorProfiles,"Roguelite.FloorProfile.SharedResolver",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFloorProfiles::RunTest(const FString&)
{
    using F=FRogueliteFloorProfileResolver;
    for(int32 Floor=1;Floor<=3;++Floor)for(int32 Seed=0;Seed<100;++Seed)
    {
        auto D=F::Defaults(Floor);const auto A=F::Resolve(D,Seed),B=F::Resolve(D,Seed);
        TestTrue(TEXT("Profile budgets legal: ")+A.Message,A.bValid);if(!A.bValid)continue;
        TestEqual(TEXT("Deterministic config"),A.ConfigurationDigest,B.ConfigurationDigest);TestEqual(TEXT("Input seed retained"),A.UsedSeed,Seed);
        auto G=FRogueliteRoomGraphGenerator::Generate(A.UsedSeed,A.Source.GraphSettings());
        TestTrue(TEXT("Tree count and hard room budget"),G.bSucceeded&&G.Graph.Nodes.Num()>=D.MinRoomCount&&G.Graph.Nodes.Num()<=D.MaxRoomCount&&G.Graph.Edges.Num()==G.Graph.Nodes.Num()-1);
        D.EncounterDifficultyProfileId=TEXT("FutureOnly");auto Reserved=F::Resolve(D,Seed);
        TestNotEqual(TEXT("Metadata revisions invalidate profile"),A.ProfileDigest,Reserved.ProfileDigest);
        TestEqual(TEXT("Reserved gameplay does not change consumed generator settings"),F::SettingsDigest(A.Source,A.Planning),F::SettingsDigest(Reserved.Source,Reserved.Planning));
        TestFalse(TEXT("Boss/safe house gameplay not claimed"),Reserved.bGameplayPoliciesImplemented);
        const auto Run=F::Resolve(D,123,true);TestEqual(TEXT("Shared floor seed derivation"),Run.UsedSeed,F::DeriveFloorSeed(123,Floor,D.ProfileVersion));
    }
    AddInfo(TEXT("E2_PROFILE_CONFIGS 300Seeds checked; no World argument or spawn path."));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFloorInvalid,"Roguelite.FloorProfile.HardConstraints",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFloorInvalid::RunTest(const FString&)
{
    using F=FRogueliteFloorProfileResolver;auto D=F::Defaults(1);
    D.MaxRoomCount=5;TestFalse(TEXT("Intro never silently changes count"),F::Resolve(D,0).bValid);
    D=F::Defaults(3);D.MaxLoops=2;TestFalse(TEXT("No false 2-loop support"),F::Resolve(D,0).bValid);
    D=F::Defaults(1);D.AllowedTemplates.Reset();TestFalse(TEXT("Empty template catalog rejected"),F::Resolve(D,0).bValid);
    D=F::Defaults(1);D.CorridorWidth=100;TestFalse(TEXT("Door width mismatch rejected"),F::Resolve(D,0).bValid);
    D=F::Defaults(1);D.MinBranches=10;TestFalse(TEXT("Unsatisfiable branch quota rejected"),F::Resolve(D,0).bValid);
    D=F::Defaults(1);D.bLastFloor=true;TestFalse(TEXT("Exit/Boss policy contradiction rejected"),F::Resolve(D,0).bValid);
    FRogueliteSeedBatch B;FRogueliteCommittedLoopBatch L;FRogueliteRuntimeLoopResult Out;FRoguelitePreparedTemplateCatalog Catalog;
    TestFalse(TEXT("Production quick-ready unsupported explicitly"),F::GenerateRuntime(nullptr,F::Resolve(F::Defaults(1),0),ERogueliteFloorValidationPurpose::RuntimeGenerationValidation,Catalog,B,L,Out));
    TestTrue(TEXT("Unsupported mode creates no authority or actors"),B.OwnedActors.IsEmpty()&&L.Edges.IsEmpty()&&Out.Stage==TEXT("RuntimeGenerationValidationUnavailable"));return true;
}
#endif
