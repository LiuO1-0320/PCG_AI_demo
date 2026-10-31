// Included only by the authorized planner translation unit: tests call the actual
// production ordering helper, not a second implementation of weighted selection.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FF1WeightedDistribution,"Roguelite.FloorTemplateWeights.WeightedDistribution",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FF1WeightedDistribution::RunTest(const FString&)
{
    using namespace RogueliteSeedAssemblyInternal;
    auto* Class=LoadClass<ARogueliteRoomTemplate>(nullptr,TEXT("/Game/Roguelite/RoomTemplates/BP_Room_Junction3.BP_Room_Junction3_C"));
    if(!TestNotNull(TEXT("All three statistical candidates share the actual same template class"),Class))return false;
    auto* CDO=Class->GetDefaultObject<ARogueliteRoomTemplate>();TArray<CatalogEntry> Catalog;
    for(int32 I=0;I<3;++I){CatalogEntry E;E.Option.Id=FName(*FString::Printf(TEXT("Statistics%d"),I));E.Option.TemplateClass=Class;E.Option.AllowedTypes={ERogueliteSeedRoomType::Combat};E.Class=Class;E.Descriptor=CDO->GetTemplateDescriptor();E.Sockets=CDO->GetDoorSockets();E.Capacity=E.Sockets.Num();Catalog.Add(E);}
    FString Rows=TEXT("Fixture\tWeightA\tWeightB\tWeightC\tSeeds\tFirstA\tFirstB\tFirstC\n");bool Good=true;
    for(int32 Fixture=0;Fixture<2;++Fixture)
    {
        const int32 Weights[2][3]={{10,5,1},{1,5,10}};FRogueliteSeedLayoutSettings S;S.AlgorithmVersion=2;S.ConfigVersion=2;
        for(int32 I=0;I<3;++I){Catalog[I].Option.Weight=Weights[Fixture][I];S.Templates.Add(Catalog[I].Option);}
        int32 Counts[3]={0,0,0};constexpr int32 Seeds=16000;
        for(int32 Seed=0;Seed<Seeds;++Seed)
        {
            S.Seed=Seed;const auto Order=F1WeightedOrder(Catalog,{0,1,2},7,S);const auto Repeat=F1WeightedOrder(Catalog,{2,0,1,1},7,S);
            if(Order.Num()!=3||Order!=Repeat){AddError(TEXT("Weighted order must be stable, unique, complete and independent of eligible input order"));return false;}++Counts[Order[0]];
        }
        const int32 High=Fixture?2:0,Low=Fixture?0:2;
        Good&=TestTrue(TEXT("Weighted first choices detect a uniform-selection regression"),Counts[High]>Seeds*.55&&Counts[High]<Seeds*.70&&Counts[Low]<Seeds*.11&&Counts[High]>Counts[Low]*5&&Counts[1]>Seeds*.25&&Counts[1]<Seeds*.38);
        Rows+=FString::Printf(TEXT("%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n"),Fixture,Weights[Fixture][0],Weights[Fixture][1],Weights[Fixture][2],Seeds,Counts[0],Counts[1],Counts[2]);
        // A forbidden entry cannot reappear when the current floor has only a subset.
        S.Templates.RemoveAll([](const auto& T){return T.Id==TEXT("Statistics1");});
        const auto Subset=F1WeightedOrder(Catalog,{0,1,2},7,S);Good&=TestTrue(TEXT("Current-floor hard whitelist cannot be bypassed"),Subset.Num()==2&&!Subset.Contains(1));
        S.Templates.Reset();Good&=TestTrue(TEXT("Empty current-floor candidates are not repopulated"),F1WeightedOrder(Catalog,{0,1,2},7,S).IsEmpty());
    }
    const FString Path=FPaths::ProjectPluginsDir()/TEXT("RoguelitePrototype/Development/P03F1/Run-20261031-130717/pure-weight-statistics.tsv");
    Good&=FFileHelper::SaveStringToFile(Rows,*Path,FFileHelper::EEncodingOptions::ForceUTF8);AddInfo(Rows);return Good;
}
#endif
