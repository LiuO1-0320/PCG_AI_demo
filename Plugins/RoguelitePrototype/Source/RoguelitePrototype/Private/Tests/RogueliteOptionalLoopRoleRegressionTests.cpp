#include "RogueliteSeedAssembly.h"
#include "../RogueliteSeedAssemblyInternal.h"
#include "RogueliteRoomTypeAssignment.h"
#include "RogueliteFloorProfile.h"
#include "RogueliteGenerationGeometry.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace R1G1Pure
{
using namespace RogueliteSeedAssemblyInternal;
constexpr auto TestFlags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
FRogueliteSeedLayoutSettings Settings(int32 Seed=0)
{
    FRogueliteSeedLayoutSettings R;R.Seed=Seed;R.AlgorithmVersion=R.ConfigVersion=2;
    R.MinNodes=R.MaxNodes=4;R.MinMainNodes=R.MaxMainNodes=3;R.MinBranches=R.MaxBranches=1;
    R.MinBranchDepth=R.MaxBranchDepth=1;R.MinDecisionNodes=R.MaxDecisionNodes=1;
    R.MaxDegree=3;R.GraphAttempts=32;
    const TCHAR* Names[]={TEXT("Safehouse"),TEXT("Normal10"),TEXT("Normal5"),TEXT("Normal1"),TEXT("Exit"),TEXT("Boss")};
    const int32 Weights[]={10000,10,5,1,10000,10000};
    for(int32 I=0;I<6;++I)
    {
        FRogueliteSeedTemplateOption T;T.Id=Names[I];T.Weight=Weights[I];
        T.TemplateClass=FSoftObjectPath(FString::Printf(TEXT("/Game/Roguelite/Tests/P03G1/BP_%s.BP_%s_C"),Names[I],Names[I]));
        T.AllowedTypes=I>=4?TArray<ERogueliteSeedRoomType>{ERogueliteSeedRoomType::Final}:
            TArray<ERogueliteSeedRoomType>{ERogueliteSeedRoomType::Combat,ERogueliteSeedRoomType::Challenge,ERogueliteSeedRoomType::SupplyShop,ERogueliteSeedRoomType::Event};
        R.Templates.Add(T);
    }
    R.RoomTemplateRoles.bEnabled=true;R.RoomTemplateRoles.StartTemplateIds={TEXT("Safehouse")};
    R.RoomTemplateRoles.NormalTemplateIds={TEXT("Normal10"),TEXT("Normal5"),TEXT("Normal1")};R.RoomTemplateRoles.FinalTemplateIds={TEXT("Exit")};
    for(auto& Q:R.TypeAssignment.Rules){Q.MinCount=Q.MaxCount=Q.Weight=0;if(Q.Type==ERogueliteSeedRoomType::Final)Q.MinCount=Q.MaxCount=Q.Weight=1;}
    auto& C=R.TypeAssignment.Rules[0];C.MinCount=2;C.MaxCount=2;C.Weight=4;
    return R;
}
TArray<CatalogEntry> Catalog(const FRogueliteSeedLayoutSettings& S)
{
    TArray<CatalogEntry> C;for(const auto& T:S.Templates){CatalogEntry E;E.Option=T;E.Capacity=T.Id==TEXT("Exit")||T.Id==TEXT("Boss")?1:3;C.Add(E);}return C;
}
void Save(const TCHAR* Name,const FString& Text)
{
    const FString Dir=FPaths::ProjectPluginsDir()/TEXT("RoguelitePrototype/Development/P03R1/Run-20261102T044951-375b82/");
    FString P=Dir+Name;if(IFileManager::Get().FileExists(*P))P+=TEXT(".")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d%H%M%S"));FFileHelper::SaveStringToFile(Text,*P,FFileHelper::EEncodingOptions::ForceUTF8);
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FR1G1RoleConfig,"Roguelite.OptionalLoopBudget.RoleRegression.ConfigurationAndFloors",R1G1Pure::TestFlags)
bool FR1G1RoleConfig::RunTest(const FString&)
{
    using namespace R1G1Pure;auto S=Settings();FString Why;bool Good=TestTrue(TEXT("Explicit pools are legal"),ValidateRoomRoleRules(S,Why));
    auto D=FRogueliteFloorProfileResolver::Defaults(1);D.AllowedTemplates=S.Templates;D.RoomTemplateRoles=S.RoomTemplateRoles;D.SpatialPlanningVersion=3;
    for(int32 Floor=1;Floor<=3;++Floor)
    {
        auto P=FRogueliteFloorProfileResolver::Defaults(Floor);P.AllowedTemplates=S.Templates;P.RoomTemplateRoles=S.RoomTemplateRoles;P.SpatialPlanningVersion=3;
        if(Floor==2)P.MinRoomCount=7; // N=6 cannot fit 2 ordinary Combat + Start + three mandatory types + Final.
        P.RoomTemplateRoles.FinalTemplateIds={Floor==3?FName(TEXT("Boss")):FName(TEXT("Exit"))};
        const auto R=FRogueliteFloorProfileResolver::Resolve(P,0);
        Good&=TestTrue(TEXT("Three-floor role configuration resolves"),R.bValid);
        if(R.bValid)Good&=TestEqual(TEXT("Resolved Final pool uses current floor"),R.Source.RoomTemplateRoles.FinalTemplateIds[0],Floor==3?FName(TEXT("Boss")):FName(TEXT("Exit")));
        P.bLastFloor=Floor!=3;P.FinalRolePolicy=P.bLastFloor?ERogueliteFloorFinalPolicy::BossFinal:ERogueliteFloorFinalPolicy::FloorExit;
        Good&=TestFalse(TEXT("A self-consistent but wrong-floor policy cannot pass"),FRogueliteFloorProfileResolver::Resolve(P,0).bValid);
    }
    auto Bad=S;Bad.RoomTemplateRoles.FinalTemplateIds.Reset();Good&=TestFalse(TEXT("No missing Final-role fallback"),ValidateRoomRoleRules(Bad,Why));
    Bad=S;Bad.RoomTemplateRoles.StartTemplateIds={TEXT("ForbiddenOtherFloor")};Good&=TestFalse(TEXT("No out-of-floor Id"),ValidateRoomRoleRules(Bad,Why));
    Bad=S;Bad.RoomTemplateRoles.NormalTemplateIds.Add(TEXT("Safehouse"));Good&=TestFalse(TEXT("Cross-role Id rejected"),ValidateRoomRoleRules(Bad,Why));
    Bad=S;Bad.Templates[1].TemplateClass=Bad.Templates[0].TemplateClass;Good&=TestFalse(TEXT("Class aliases cannot bypass role isolation"),ValidateRoomRoleRules(Bad,Why));
    Bad=S;Bad.Templates[0].Weight=0;Good&=TestFalse(TEXT("Weight zero is not disable"),ValidateRoomRoleRules(Bad,Why));
    D.SpatialPlanningVersion=2;Good&=TestFalse(TEXT("New mode requires explicit F1 selection"),FRogueliteFloorProfileResolver::Resolve(D,0).bValid);
    return Good;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FR1G1RolePool,"Roguelite.OptionalLoopBudget.RoleRegression.IsolationCapacityAndWeights",R1G1Pure::TestFlags)
bool FR1G1RolePool::RunTest(const FString&)
{
    using namespace R1G1Pure;auto S=Settings();const auto Generated=FRogueliteRoomGraphGenerator::Generate(0,S.GraphSettings());
    if(!TestTrue(TEXT("Synthetic descriptor graph generated"),Generated.bSucceeded))return false;
    const auto& G=Generated.Graph;auto C=Catalog(S);bool Good=true;int32 Counts[3]={0,0,0};FString Rows=TEXT("Seed\tFirstNormal\n");
    for(int32 Seed=0;Seed<2000;++Seed)
    {
        S.Seed=Seed;
        const auto Root=TemplateOrder(C,ERogueliteSeedRoomType::Combat,G.Nodes[G.StartNodeId].Degree,G.StartNodeId,S,G);
        Good&=Root.Num()==1&&C[Root[0]].Option.Id==TEXT("Safehouse");
        const auto Order=TemplateOrder(C,ERogueliteSeedRoomType::Combat,G.Nodes[1].Degree,1,S,G);
        const auto Repeat=TemplateOrder(C,ERogueliteSeedRoomType::Combat,G.Nodes[1].Degree,1,S,G);
        if(Order.Num()!=3||Order!=Repeat){AddError(TEXT("Normal candidates must be stable and role-filtered"));return false;}
        const FName First=C[Order[0]].Option.Id;const int32 I=First==TEXT("Normal10")?0:First==TEXT("Normal5")?1:2;++Counts[I];
        Rows+=FString::Printf(TEXT("%d\t%s\n"),Seed,*First.ToString());
        for(int32 Index:Order)Good&=S.RoomTemplateRoles.NormalTemplateIds.Contains(C[Index].Option.Id);
        const auto Final=TemplateOrder(C,ERogueliteSeedRoomType::Final,1,G.FinalNodeId,S,G);
        Good&=Final.Num()==1&&C[Final[0]].Option.Id==TEXT("Exit");
    }
    Good&=TestTrue(TEXT("10:5:1 preference remains, detects uniform regression"),Counts[0]>1100&&Counts[0]<1400&&Counts[1]>500&&Counts[1]<760&&Counts[2]<220&&Counts[0]>Counts[2]*5);
    C[0].Capacity=0;Good&=TestTrue(TEXT("No normal template fallback for insufficient Start sockets"),TemplateOrder(C,ERogueliteSeedRoomType::Combat,1,G.StartNodeId,S,G).IsEmpty());
    Good&=TestTrue(TEXT("Final identity is not the last appended branch"),G.FinalNodeId!=G.Nodes.Num()-1);
    Good&=TestTrue(TEXT("Both Start and ordinary Combat stay isolated"),Good);
    Save(TEXT("pure-weight-choices.tsv"),Rows);AddInfo(FString::Printf(TEXT("2000 Seeds: Normal10=%d Normal5=%d Normal1=%d"),Counts[0],Counts[1],Counts[2]));return Good;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FR1G1Quota,"Roguelite.OptionalLoopBudget.RoleRegression.OrdinaryCombatQuotas",R1G1Pure::TestFlags)
bool FR1G1Quota::RunTest(const FString&)
{
    using namespace R1G1Pure;bool Good=true;int32 Cases=0,Success=0,ExpectedRejected=0;
    for(int32 Seed=0;Seed<40;++Seed)
    {
        auto S=Settings(Seed);const auto GG=FRogueliteRoomGraphGenerator::Generate(Seed,S.GraphSettings());
        if(!GG.bSucceeded){AddError(GG.Reason);return false;}const auto& G=GG.Graph;
        for(int32 Min=0;Min<=3;++Min)for(int32 Max=Min;Max<=4;++Max)for(int32 Weight:{0,4})for(int32 Flags=0;Flags<4;++Flags)
        {
            if(Min>0&&Weight==0)continue;
            auto& Combat=S.TypeAssignment.Rules[0];Combat.MinCount=Min;Combat.MaxCount=Max;Combat.Weight=Weight;
            Combat.bAllowMainPath=bool(Flags&1);Combat.bAllowBranch=bool(Flags&2);
            auto& Challenge=S.TypeAssignment.Rules[1];Challenge.Weight=1;Challenge.MaxCount=4;
            int32 AllowedCombatNodes=0;for(const auto& Node:G.Nodes)if(Node.NodeId!=G.StartNodeId&&Node.NodeId!=G.FinalNodeId)
                AllowedCombatNodes+=Node.Role==ERogueliteGraphNodeRole::Branch?Combat.bAllowBranch:Combat.bAllowMainPath;
            const bool Feasible=Min<=FMath::Min(Max,Weight?AllowedCombatNodes:0);
            const FString Before=FRogueliteRoomTypeAssigner::DescribeSettings(S.TypeAssignment);
            const auto A=AssignTypesForLayout(G,S),Repeat=AssignTypesForLayout(G,S);++Cases;
            if(A.bSucceeded!=Feasible||A.bSucceeded!=Repeat.bSucceeded||A.StableDigest!=Repeat.StableDigest)
            {AddError(FString::Printf(TEXT("Seed%d quota[%d,%d] Weight%d Flags%d expected%d actual%d: %s"),Seed,Min,Max,Weight,Flags,Feasible,A.bSucceeded,*A.Reason));return false;}
            Good&=Before==FRogueliteRoomTypeAssigner::DescribeSettings(S.TypeAssignment);
            if(A.bSucceeded)
            {
                ++Success;int32 Ordinary=0;for(int32 Node=0;Node<G.Nodes.Num();++Node)Ordinary+=Node!=G.StartNodeId&&A.Types[Node]==ERogueliteSeedRoomType::Combat;
                Good&=A.Types[G.StartNodeId]==ERogueliteSeedRoomType::Combat&&Ordinary>=Min&&Ordinary<=Max&&A.Counts[0]==Ordinary;
            }else{++ExpectedRejected;Good&=!A.Reason.IsEmpty();}
            S.RoomTemplateRoles.bEnabled=false;
            const auto Old=FRogueliteRoomTypeAssigner::Assign(G,S.TypeAssignment),Compat=AssignTypesForLayout(G,S);
            Good&=Old.bSucceeded==Compat.bSucceeded&&Old.Types==Compat.Types&&Old.Counts==Compat.Counts&&Old.StableDigest==Compat.StableDigest&&Old.Reason==Compat.Reason;
            S.RoomTemplateRoles.bEnabled=true;
        }
    }
    AddInfo(FString::Printf(TEXT("Native quota cases=%d succeeded=%d expectedRejected=%d"),Cases,Success,ExpectedRejected));
    Save(TEXT("pure-quota-summary.txt"),FString::Printf(TEXT("Cases=%d Success=%d ExpectedRejected=%d AllConditions=%d"),Cases,Success,ExpectedRejected,Good));
    return TestTrue(TEXT("New ordinary quota semantics, zero/max/position boundaries, original rules immutability and exact legacy delegation"),Good);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FR1G1Identity,"Roguelite.OptionalLoopBudget.RoleRegression.DisabledCompatibilityAndDeterminism",R1G1Pure::TestFlags)
bool FR1G1Identity::RunTest(const FString&)
{
    using namespace R1G1Pure;auto S=Settings();S.RoomTemplateRoles.bEnabled=false;
    auto D=FRogueliteFloorProfileResolver::Defaults(1);const FString ProfileBefore=FRogueliteFloorProfileResolver::Digest(D);
    D.RoomTemplateRoles.StartTemplateIds={TEXT("UnusedPrivate")};const bool ProfileSame=ProfileBefore==FRogueliteFloorProfileResolver::Digest(D);
    bool Good=TestTrue(TEXT("Inactive roles do not alter existing Profile digest"),ProfileSame);
    for(auto Mode:{ERogueliteOpeningMode::Legacy,ERogueliteOpeningMode::OpenPassage})for(bool Ceiling:{false,true})
    {
        S.GenerationGeometry.OpeningMode=Mode;S.Corridors.SetNum(1);S.Corridors[0].bGenerateCeiling=Ceiling;
        S.RoomTemplateRoles.bEnabled=false;S.RoomTemplateRoles.StartTemplateIds.Reset();
        const FString Text=RogueliteGenerationGeometry::SettingsText(S);S.RoomTemplateRoles.StartTemplateIds={TEXT("Ignored")};
        Good&=TestEqual(TEXT("Inactive role payload omitted for every historical geometry combination"),RogueliteGenerationGeometry::SettingsText(S),Text);
        S.RoomTemplateRoles.StartTemplateIds={TEXT("Safehouse")};S.RoomTemplateRoles.bEnabled=true;
        Good&=TestNotEqual(TEXT("Active role policy enters settings identity"),RogueliteGenerationGeometry::SettingsText(S),Text);
    }
    for(int32 Seed=0;Seed<100;++Seed)
    {
        S=Settings(Seed);auto A=FRogueliteRoomGraphGenerator::Generate(Seed,S.GraphSettings());const auto T=AssignTypesForLayout(A.Graph,S);
        S.RoomTemplateRoles.bEnabled=false;auto B=FRogueliteRoomGraphGenerator::Generate(Seed,S.GraphSettings());
        Good&=A.bSucceeded&&B.bSucceeded&&T.bSucceeded&&A.Graph.StableDigest==B.Graph.StableDigest;
    }
    return TestTrue(TEXT("Role and ordinary quota feature never changes the topology stream"),Good);
}
#endif
