#include "CastleAI.h"
#if WITH_EDITOR
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_SimpleParallel.h"
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode.h"
#include "BehaviorTreeGraphNode_Decorator.h"
#include "BehaviorTreeGraphNode_SimpleParallel.h"
#include "EdGraphSchema_BehaviorTree.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"

static bool SaveAIAsset(UObject* Asset)
{
    auto* Package = Asset->GetOutermost();
    Package->MarkPackageDirty();
    FSavePackageArgs Args; Args.TopLevelFlags = RF_Public|RF_Standalone;
    const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(),FPackageName::GetAssetPackageExtension());
    return UPackage::SavePackage(Package,Asset,*File,Args);
}

static UBTTask_CastleAction* MakeAction(UBehaviorTree* Tree, ECastleAIAction Action, const TCHAR* Label)
{
    auto* Task = NewObject<UBTTask_CastleAction>(Tree);
    Task->Action = Action; Task->NodeName = Label;
    return Task;
}

static void AddChild(UBTCompositeNode* Parent, UBTNode* Node)
{
    FBTCompositeChild Child;
    Child.ChildComposite = Cast<UBTCompositeNode>(Node);
    Child.ChildTask = Cast<UBTTaskNode>(Node);
    Parent->Children.Add(Child);
}

static UBehaviorTree* MakeCastleTree(UBlackboardData* BB, bool bBoss)
{
    const FString Name = bBoss ? TEXT("BT_CastleBoss") : TEXT("BT_CastleGuard");
    const FString Path = TEXT("/Game/Demo/AI/") + Name;
    auto* Package = CreatePackage(*Path); Package->FullyLoad();
    auto* Tree = NewObject<UBehaviorTree>(Package,FName(*Name),RF_Public|RF_Standalone);
    Tree->BlackboardAsset = BB;
    auto* Root = NewObject<UBTComposite_Selector>(Tree);
    Root->NodeName = TEXT("Combat or hold current state"); Tree->RootNode = Root;
    Root->Services.Add(NewObject<UBTService_CastlePerception>(Tree));
    auto* Combat = NewObject<UBTComposite_SimpleParallel>(Tree);
    Combat->NodeName = TEXT("Pursuit and combat simultaneously");
    Combat->FinishMode = EBTParallelMode::AbortBackground;
    AddChild(Root,Combat);
    for (FName Key : {FName(TEXT("IsOperational")),FName(TEXT("InDecisionRange"))})
    {
        auto* Gate = NewObject<UBTDecorator_CastleBool>(Tree);
        Gate->ConfigureKey(Key); static_cast<UBTNode*>(Gate)->InitializeFromAsset(*Tree);
        Root->Children[0].Decorators.Add(Gate);
    }
    AddChild(Combat,MakeAction(Tree,ECastleAIAction::Pursuit,TEXT("Face and chase / stop")));
    if (bBoss)
    {
        auto* Abilities = NewObject<UBTComposite_SimpleParallel>(Tree);
        Abilities->NodeName = TEXT("Attack and ground slam independently");
        Abilities->FinishMode = EBTParallelMode::AbortBackground;
        AddChild(Combat,Abilities);
        AddChild(Abilities,MakeAction(Tree,ECastleAIAction::Attack,TEXT("Melee / fan attack + enrage")));
        AddChild(Abilities,MakeAction(Tree,ECastleAIAction::GroundSlam,TEXT("Ground slam telegraph (no sight required)")));
    }
    else AddChild(Combat,MakeAction(Tree,ECastleAIAction::Attack,TEXT("Melee / single projectile")));
    AddChild(Root,MakeAction(Tree,ECastleAIAction::Hold,TEXT("Out of range: hold; inactive: stop")));

    // Persist editor graph nodes as well as runtime nodes, so opening the asset is reviewable.
    auto* Graph = NewObject<UBehaviorTreeGraph>(Tree,TEXT("BehaviorTreeGraph"),RF_Transactional);
    Tree->BTGraph = Graph;
    Graph->Schema = UEdGraphSchema_BehaviorTree::StaticClass();
    Graph->LockUpdates();
    Graph->GetSchema()->CreateDefaultNodesForGraph(*Graph);
    // UE's reconstruction helper adds decorators before assigning NodeInstance,
    // which reports null-node errors in an unattended commandlet. Attach them fully initialized.
    const auto Gates = Root->Children[0].Decorators;
    Root->Children[0].Decorators.Reset();
    Graph->SpawnMissingNodes();
    // Generic runtime-to-graph reconstruction connects every child to the first
    // output. Simple Parallel needs its second child on the background output.
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        auto* ParallelGraph = Cast<UBehaviorTreeGraphNode_SimpleParallel>(Node);
        if (!ParallelGraph) continue;
        auto* MainPin = ParallelGraph->GetOutputPin(0);
        auto* BackgroundPin = ParallelGraph->GetOutputPin(1);
        if (MainPin && BackgroundPin && MainPin->LinkedTo.Num() == 2)
        {
            auto* BackgroundChild = MainPin->LinkedTo[1];
            MainPin->BreakLinkTo(BackgroundChild);
            BackgroundPin->MakeLinkTo(BackgroundChild);
        }
    }
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        auto* CombatGraph = Cast<UBehaviorTreeGraphNode>(Node);
        if (!CombatGraph || CombatGraph->NodeInstance != Combat) continue;
        for (UBTDecorator* Gate : Gates)
        {
            auto* DecoratorGraph = NewObject<UBehaviorTreeGraphNode_Decorator>(Graph);
            DecoratorGraph->NodeInstance = Gate;
            CombatGraph->AddSubNode(DecoratorGraph,Graph);
        }
    }
    Graph->SpawnMissingNodesForParallel();
    // Leave generous spacing for services/decorators and readable editor screenshots.
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        auto* BTGraphNode=Cast<UBehaviorTreeGraphNode>(Node);
        if (!BTGraphNode) continue;
        BTGraphNode->UpdateNodeClassData();
        for (UBehaviorTreeGraphNode* SubNode : BTGraphNode->Decorators) if (SubNode) SubNode->UpdateNodeClassData();
        for (UBehaviorTreeGraphNode* SubNode : BTGraphNode->Services) if (SubNode) SubNode->UpdateNodeClassData();
        BTGraphNode->NodePosX=850; BTGraphNode->NodePosY=0;
        if (BTGraphNode->NodeInstance==Root) {BTGraphNode->NodePosX=850;BTGraphNode->NodePosY=250;}
        else if (BTGraphNode->NodeInstance==Combat) {BTGraphNode->NodePosX=450;BTGraphNode->NodePosY=600;}
        else if (auto* Task=Cast<UBTTask_CastleAction>(BTGraphNode->NodeInstance))
        {
            switch(Task->Action)
            {
            case ECastleAIAction::Pursuit: BTGraphNode->NodePosX=0;BTGraphNode->NodePosY=1100;break;
            case ECastleAIAction::Hold: BTGraphNode->NodePosX=1700;BTGraphNode->NodePosY=600;break;
            case ECastleAIAction::Attack: BTGraphNode->NodePosX=bBoss?700:900;BTGraphNode->NodePosY=bBoss?1500:1100;break;
            case ECastleAIAction::GroundSlam: BTGraphNode->NodePosX=1400;BTGraphNode->NodePosY=1500;break;
            }
        }
        else if (BTGraphNode->NodeInstance) {BTGraphNode->NodePosX=900;BTGraphNode->NodePosY=1100;}
    }
    Graph->UnlockUpdates();
    Graph->UpdateAsset(UBehaviorTreeGraph::ClearDebuggerFlags);
    FAssetRegistryModule::AssetCreated(Tree);
    return Tree;
}
#endif

UCastleAISetupCommandlet::UCastleAISetupCommandlet()
{
    IsClient=false; IsServer=false; IsEditor=true; LogToConsole=true;
}

int32 UCastleAISetupCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
    if (FParse::Param(*Params,TEXT("Verify")))
    {
        // Follow the editor's reopen path, then inspect the rebuilt runtime tree.
        for (const TCHAR* Name : {TEXT("BT_CastleGuard"),TEXT("BT_CastleBoss")})
        {
            auto* Tree=LoadObject<UBehaviorTree>(nullptr,*FString::Printf(TEXT("/Game/Demo/AI/%s.%s"),Name,Name));
            auto* Graph=Tree?Cast<UBehaviorTreeGraph>(Tree->BTGraph):nullptr;
            if (!Graph) return 3;
            Graph->OnLoaded();Graph->Initialize();Graph->UpdateAsset(UBehaviorTreeGraph::KeepRebuildCounter);
            if (!Tree->RootNode || Tree->RootNode->Children.Num()!=2 || Tree->RootNode->Services.Num()!=1 ||
                Tree->RootNode->Children[0].Decorators.Num()!=2 || !Tree->BlackboardAsset) return 4;
            for (UBTDecorator* Gate : Tree->RootNode->Children[0].Decorators)
                if (!Gate || Gate->GetOutermost()!=Tree->GetOutermost()) return 5;
            int32 Pursuit=0,Attack=0,Slam=0,Hold=0;
            TFunction<void(UBTCompositeNode*)> Inspect=[&](UBTCompositeNode* Composite)
            {
                for (const auto& Child : Composite->Children)
                {
                    if (auto* Task=Cast<UBTTask_CastleAction>(Child.ChildTask))
                    {
                        switch(Task->Action)
                        {
                        case ECastleAIAction::Pursuit: ++Pursuit;break;
                        case ECastleAIAction::Attack: ++Attack;break;
                        case ECastleAIAction::GroundSlam: ++Slam;break;
                        case ECastleAIAction::Hold: ++Hold;break;
                        }
                    }
                    if (Child.ChildComposite) Inspect(Child.ChildComposite);
                }
            };
            Inspect(Tree->RootNode);
            if (Pursuit!=1 || Attack!=1 || Hold!=1 || Slam!=(Tree->GetFName()==TEXT("BT_CastleBoss")?1:0)) return 6;
            UE_LOG(LogTemp,Display,TEXT("CASTLE_AI_EDITOR_VERIFY_OK: %s reopened; gates, service and action branches preserved."),Name);
        }
        return 0;
    }
    const FString Path = TEXT("/Game/Demo/AI/BB_CastleEnemy");
    auto* Package = CreatePackage(*Path); Package->FullyLoad();
    auto* BB = NewObject<UBlackboardData>(Package,TEXT("BB_CastleEnemy"),RF_Public|RF_Standalone);
    auto AddKey = [BB](const TCHAR* Name, UBlackboardKeyType* Type)
    {
        FBlackboardEntry Entry; Entry.EntryName = Name; Entry.KeyType = Type; BB->Keys.Add(Entry);
    };
    auto* Target = NewObject<UBlackboardKeyType_Object>(BB); Target->BaseClass = AActor::StaticClass();
    AddKey(TEXT("TargetActor"),Target);
    AddKey(TEXT("HomeLocation"),NewObject<UBlackboardKeyType_Vector>(BB));
    AddKey(TEXT("TargetDistance"),NewObject<UBlackboardKeyType_Float>(BB));
    for (const TCHAR* Name : {TEXT("IsOperational"),TEXT("InDecisionRange"),TEXT("CanSeeTarget"),TEXT("IsBoss"),TEXT("IsEnraged"),TEXT("IsChargingBlast")})
        AddKey(Name,NewObject<UBlackboardKeyType_Bool>(BB));
    FAssetRegistryModule::AssetCreated(BB);
    if (!SaveAIAsset(BB)) return 1;
    for (bool bBoss : {false,true}) if (!SaveAIAsset(MakeCastleTree(BB,bBoss))) return 2;
    UE_LOG(LogTemp,Display,TEXT("CASTLE_AI_ASSETS_OK: BB_CastleEnemy, BT_CastleGuard, BT_CastleBoss saved with editable graphs."));
    return 0;
#else
    return 1;
#endif
}
