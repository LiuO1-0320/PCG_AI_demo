#include "RogueliteFloorProfile.h"
#include "RogueliteCompactCorridorContract.h"
#include "RogueliteRoomTypeAssignment.h"
#include "RogueliteGenerationGeometry.h"
#include "Engine/World.h"

FRogueliteFloorDefinition FRogueliteFloorProfileResolver::Defaults(int32 Floor)
{
    FRogueliteFloorDefinition D;D.FloorIndex=Floor;D.ProfileVersion=3;
    D.OpeningMode=ERogueliteOpeningMode::OpenPassage;D.bGenerateCorridorCeiling=true;
    D.ProfileId=Floor==1?TEXT("Floor1.Intro"):Floor==2?TEXT("Floor2.Expansion"):TEXT("Floor3.Final");
    D.DisplayName=FText::FromString(Floor==1?TEXT("Floor 1 — Intro / 入门"):Floor==2?TEXT("Floor 2 — Expansion / 扩编"):TEXT("Floor 3 — Final / 最终层"));
    if(Floor>1){D.MinRoomCount=Floor==2?6:8;D.MaxRoomCount=Floor==2?8:10;D.MinBranches=Floor==2?2:3;D.MaxBranches=Floor==2?3:4;D.MaxBranchDepth=2;D.MinDecisionNodes=Floor==2?2:3;D.MaxDecisionNodes=Floor==2?3:4;D.MinNestedDecisionNodes=1;D.MaxBranchGenerationDepth=3;D.MaxLoops=1;}
    D.bLastFloor=Floor==3;D.FinalRolePolicy=D.bLastFloor?ERogueliteFloorFinalPolicy::BossFinal:ERogueliteFloorFinalPolicy::FloorExit;
    for(auto& Rule:D.RoomTypes.Rules)
    {
        Rule.MinCount=0;Rule.MaxCount=0;Rule.Weight=0;
        if(Rule.Type==ERogueliteSeedRoomType::Combat){Rule.MinCount=2;Rule.MaxCount=32;Rule.Weight=4;}
        else if(Rule.Type==ERogueliteSeedRoomType::Final){Rule.MinCount=Rule.MaxCount=Rule.Weight=1;}
        else if(Floor>1){Rule.MinCount=1;Rule.MaxCount=2;Rule.Weight=1;}
    }
    const TArray<ERogueliteSeedRoomType> All={ERogueliteSeedRoomType::Combat,ERogueliteSeedRoomType::Challenge,ERogueliteSeedRoomType::SupplyShop,ERogueliteSeedRoomType::Final,ERogueliteSeedRoomType::Event};
    FRogueliteSeedTemplateOption J;J.Id=TEXT("Junction3");J.TemplateClass=FSoftObjectPath(TEXT("/Game/Roguelite/RoomTemplates/BP_Room_Junction3.BP_Room_Junction3_C"));J.AllowedTypes=All;
    FRogueliteSeedTemplateOption W;W.Id=TEXT("Warehouse");W.TemplateClass=FSoftObjectPath(TEXT("/Game/Roguelite/RoomTemplates/BP_Room_Warehouse.BP_Room_Warehouse_C"));W.AllowedTypes=All;D.AllowedTemplates={J,W};return D;
}
FString FRogueliteFloorProfileResolver::Digest(const FRogueliteFloorDefinition& D)
{FString Text;FRogueliteFloorDefinition::StaticStruct()->ExportText(Text,&D,nullptr,nullptr,PPF_None,nullptr);return FRogueliteRoomGraphGenerator::StableTextDigest(TEXT("FloorProfile1|")+Text);}
FString FRogueliteFloorProfileResolver::SettingsDigest(const FRogueliteSeedLayoutSettings& S,const FRogueliteLoopAwareSettings& P)
{const FString A=RogueliteGenerationGeometry::SettingsText(S);FString B;FRogueliteLoopAwareSettings::StaticStruct()->ExportText(B,&P,nullptr,nullptr,PPF_None,nullptr);return FRogueliteRoomGraphGenerator::StableTextDigest(A+TEXT("|")+B);}
int32 FRogueliteFloorProfileResolver::DeriveFloorSeed(int32 Seed,int32 Floor,int32 Version)
{return FRogueliteRoomGraphGenerator::DeriveSeed(Seed,static_cast<ERogueliteGraphSeedPurpose>(0x6e2f0121u),1,Version,Floor,0);}
FString FRogueliteFloorProfileResolver::FriendlyFailure(const FString& Raw)
{
    if(Raw.Contains(TEXT("Compact"))||Raw.Contains(TEXT("Closure"))||Raw.Contains(TEXT("TreeBranchSpatial"))||Raw.Contains(TEXT("SpatialCheckBudget")))return TEXT("当前房间排列无法在12–15米走廊内完成合法连接，请重新生成或调整房间模板；未延长走廊或降低回环要求。");
    if(Raw.Contains(TEXT("Template"))||Raw.Contains(TEXT("Capacity"))||Raw.Contains(TEXT("Catalog")))return TEXT("当前模板不存在、门洞或房型支持不足，请检查可用模板和连接预算。");
    if(Raw.Contains(TEXT("Loop"))||Raw.Contains(TEXT("Closure")))return TEXT("无法在允许空间内形成要求的回环，请调整模板、走廊范围或回环预算。");
    if(Raw.Contains(TEXT("Budget"))||Raw.Contains(TEXT("Exhausted")))return TEXT("当前空间或搜索预算不足，未生成合法布局；请调整规则或检查场景障碍。");
    if(Raw.Contains(TEXT("Stale"))||Raw.Contains(TEXT("ProfileChanged")))return TEXT("配置、场景或批次已经改变，请清理当前自有批次后重新生成。");
    if(Raw.Contains(TEXT("Navigation")))return TEXT("几何与导航是独立状态；当前导航未完成或未测试，不能作为地牢就绪。");
    if(Raw.Contains(TEXT("ExistingBatch")))return TEXT("已有地牢批次，请先清理；不会叠加生成或改动其他批次。");
    return TEXT("楼层配置或真实验证未通过，请查看原始原因；没有降低硬性约束。");
}
FRogueliteResolvedFloor FRogueliteFloorProfileResolver::Resolve(const FRogueliteFloorDefinition& D,int32 Seed,bool FromRun)
{
    FRogueliteResolvedFloor R;R.ProfileId=D.ProfileId;R.FloorIndex=D.FloorIndex;R.bDerivedFromRunSeed=FromRun;R.UsedSeed=FromRun?DeriveFloorSeed(Seed,D.FloorIndex,D.ProfileVersion):Seed;R.ProfileDigest=Digest(D);
    const auto Fail=[&](const FString& Code,const FString& Message){R.ErrorCode=Code;R.Message=Message;return R;};
    if(D.ProfileId.IsNone()||D.FloorIndex<1||D.ProfileVersion<1||D.SpatialPlanningVersion!=2)return Fail(TEXT("InvalidProfileIdentity"),TEXT("楼层标识、版本或空间规划版本无效。"));
    if(D.MinRoomCount<2||D.MaxRoomCount<D.MinRoomCount||D.MaxRoomCount>32)return Fail(TEXT("RoomBudgetInvalid"),TEXT("房间范围无效；当前共享规划器最多支持32间房。"));
    if(D.FloorIndex==1&&(D.MinRoomCount!=4||D.MaxRoomCount!=4||D.MinLoops!=0||D.MaxLoops!=0))return Fail(TEXT("IntroBudgetViolation"),TEXT("第一层当前合同为4房3廊、零回环；不能静默改变该预设。"));
    if(D.MinLoops<0||D.MaxLoops<D.MinLoops||D.MaxLoops>1)return Fail(TEXT("LoopBudgetUnsupported"),TEXT("当前只能生成0或1个真实回环，不支持2环。"));
    if(D.bLastFloor!=(D.FinalRolePolicy==ERogueliteFloorFinalPolicy::BossFinal))return Fail(TEXT("FinalPolicyMismatch"),TEXT("仅最后一层使用BossFinal；普通楼层终点是FloorExit。两者Gameplay均未实现。"));
    if(D.AllowedTemplates.IsEmpty()||D.AllowedTemplates.Num()>16)return Fail(TEXT("TemplateCatalogInvalid"),TEXT("请选择1至16个现有房间模板。"));
    TSet<FName> Names;for(const auto& T:D.AllowedTemplates){if(T.Id.IsNone()||T.TemplateClass.IsNull()||T.Weight<1||T.AllowedTypes.IsEmpty()||Names.Contains(T.Id))return Fail(TEXT("TemplateOptionInvalid"),TEXT("模板标识、资源、权重或允许房型无效/重复。"));Names.Add(T.Id);}
    if(D.MinCorridorLength!=1200||D.MaxCorridorLength!=1500)return Fail(TEXT("CompactProfileRangeRequired"),TEXT("本轮所有楼层走廊固定12–15米；请更新Profile，不能使用旧长走廊范围。"));
    for(float V:{D.MinCorridorLength,D.MaxCorridorLength,D.CorridorWidth,D.CorridorHeight,D.WallThickness,D.FloorThickness,D.DoorWidth,D.DoorHeight})if(!FMath::IsFinite(V)||V<=0)return Fail(TEXT("CorridorDimensionInvalid"),TEXT("走廊尺寸必须是有限正数，单位厘米。"));
    if(D.MinCorridorLength<100||D.MaxCorridorLength<D.MinCorridorLength||D.MaxCorridorLength>100000||D.MinCorridorLength<=2*D.WallThickness||D.CorridorWidth<D.DoorWidth||D.CorridorHeight<D.DoorHeight)return Fail(TEXT("CorridorRangeInvalid"),TEXT("走廊长度范围或门洞净宽高不兼容。"));
    auto& S=R.Source;S.Seed=R.UsedSeed;S.AlgorithmVersion=2;S.ConfigVersion=2;S.TemplateCatalogVersion=2;
    S.MinNodes=D.MinRoomCount;S.MaxNodes=D.MaxRoomCount;S.MinMainNodes=D.MinReferencePathRooms;S.MaxMainNodes=D.MaxReferencePathRooms;
    S.MinBranches=D.MinBranches;S.MaxBranches=D.MaxBranches;S.MinBranchDepth=D.MinBranchDepth;S.MaxBranchDepth=D.MaxBranchDepth;
    S.MinDecisionNodes=D.MinDecisionNodes;S.MaxDecisionNodes=D.MaxDecisionNodes;S.MinNestedDecisionNodes=D.MinNestedDecisionNodes;S.MaxBranchGenerationDepth=D.MaxBranchGenerationDepth;S.MaxDegree=D.MaxDegree;
    S.GraphAttempts=D.SearchBudget.GraphAttempts;S.MaxCandidatesPerEdge=D.SearchBudget.MaxCandidatesPerEdge;S.TypeAssignment=D.RoomTypes;S.Templates=D.AllowedTemplates;S.bCheckNavigation=false;
    S.GenerationGeometry.OpeningMode=D.OpeningMode;S.GenerationGeometry.OpenPassageRoomHeight=D.OpenPassageRoomHeight;
    FRogueliteSeedEncounterOption E;E.ProfileId=TEXT("NoEncounter_Reserved");E.AllowedTypes={ERogueliteSeedRoomType::Combat,ERogueliteSeedRoomType::Challenge,ERogueliteSeedRoomType::SupplyShop,ERogueliteSeedRoomType::Final,ERogueliteSeedRoomType::Event};S.Encounters={E};
    FRogueliteSeedCorridorProfile C;C.LengthCandidates.Reset();C.Width=D.CorridorWidth;C.Height=D.CorridorHeight;C.WallThickness=D.WallThickness;C.FloorThickness=D.FloorThickness;C.DoorWidth=D.DoorWidth;C.DoorHeight=D.DoorHeight;
    C.bGenerateCeiling=D.bGenerateCorridorCeiling;
    if(D.OpeningMode==ERogueliteOpeningMode::OpenPassage){C.Height=D.OpenPassageRoomHeight;C.DoorWidth=C.Width;C.DoorHeight=C.Height;}
    for(float L:{D.MinCorridorLength,1400.f,1450.f,1475.f,D.MaxCorridorLength})if(L>=D.MinCorridorLength&&L<=D.MaxCorridorLength)C.LengthCandidates.AddUnique(L);C.LengthCandidates.Sort();S.Corridors={C};
    FString GeometryReason;if(!RogueliteGenerationGeometry::Validate(S,GeometryReason))return Fail(TEXT("GenerationGeometryInvalid"),GeometryReason);
    auto& P=R.Planning;P.bEnableLoopAwarePlanning=true;P.SpatialPlanningVersion=D.SpatialPlanningVersion;P.MinLoops=D.MinLoops;P.MaxLoops=D.MaxLoops;
    P.MinTreeCorridorLength=P.MinLoopCorridorLength=D.MinCorridorLength;P.MaxTreeCorridorLength=P.MaxLoopCorridorLength=D.MaxCorridorLength;
    P.MaxLayoutAttempts=D.SearchBudget.MaxLayoutAttempts;P.MaxBacktrackStates=D.SearchBudget.MaxBacktrackStates;P.MaxSpatialChecks=D.SearchBudget.MaxSpatialChecks;
    FString Why;if(S.MaxCandidatesPerEdge<1||S.MaxCandidatesPerEdge>256||!FRogueliteLoopAwarePlanner::ValidateSettings(P,Why))return Fail(TEXT("PlanningBudgetInvalid"),TEXT("空间预算无效：")+Why);
    const auto G=FRogueliteRoomGraphGenerator::Generate(S.Seed,S.GraphSettings());if(!G.bSucceeded)return Fail(TEXT("GraphBudgetRejected"),TEXT("房间、参考路径及分岔预算无法同时满足：")+G.Reason);
    const auto Types=FRogueliteRoomTypeAssigner::Assign(G.Graph,S.TypeAssignment);if(!Types.bSucceeded)return Fail(TEXT("RoomTypeQuotaRejected"),TEXT("当前房型数量/权重无法满足：")+Types.Reason);
    R.ConfigurationDigest=FRogueliteRoomGraphGenerator::StableTextDigest(R.ProfileDigest+SettingsDigest(S,P));R.bValid=true;R.Message=TEXT("规则解析通过；真实模板、空间与C2仍须实际生成验证。安全屋/Boss/楼层切换规则仅为预留合同。");return R;
}
bool FRogueliteFloorProfileResolver::GenerateRuntime(AActor* Owner,const FRogueliteResolvedFloor& C,ERogueliteFloorValidationPurpose Purpose,const FRoguelitePreparedTemplateCatalog& Catalog,FRogueliteSeedBatch& B,FRogueliteCommittedLoopBatch& L,FRogueliteRuntimeLoopResult& Out)
{
    if(!C.bValid||Purpose!=ERogueliteFloorValidationPurpose::DeveloperValidation){Out={};Out.Stage=TEXT("RuntimeGenerationValidationUnavailable");Out.Reason=TEXT("正式开局轻量Ready尚未实现；不能跳过或替换B4-B3严格导航。已有批次不变。");return false;}
    FString Why;if(!FRogueliteCompactCorridorContract::Configuration(C.Source,C.Planning,Why)){Out={};Out.Stage=TEXT("CompactConfigurationRejected");Out.Reason=Why;return false;}
    if(!FRogueliteRuntimeLoopAssembler::Generate(Owner,C.Source,C.Planning,Catalog,B,L,Out))return false;
    TArray<FRogueliteCompactCorridorMeasurement> Lengths;
    if(FRogueliteCompactCorridorContract::Actual(B,L,Lengths,Why))return true;
    FString Cleanup;Out.bCleanupComplete=FRogueliteRuntimeLoopAssembler::Clear(Owner,B,L,Cleanup);Out.bSucceeded=false;Out.Traversal={};Out.Stage=TEXT("CompactActualLengthRejected");Out.Reason=Why+TEXT(" | ")+Cleanup;return false;
}
FRogueliteResolvedFloor URogueliteFloorProfile::ResolvePreview(int32 Seed) const{return FRogueliteFloorProfileResolver::Resolve(Rules,Seed);}
FRogueliteResolvedFloor URogueliteFloorProfile::ResolveRun(int32 Seed) const{return FRogueliteFloorProfileResolver::Resolve(Rules,Seed,true);}
