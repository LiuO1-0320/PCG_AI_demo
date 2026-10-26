#include "RogueliteOpenPassagePrototypeRig.h"
#include "RogueliteDevGameMode.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "FileHelpers.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
const TCHAR* Map=TEXT("/Game/Roguelite/Maps/L_Roguelite_CeilingToggle_Dev");
FString Dir(){static FString P=FPaths::ProjectPluginsDir()/TEXT("RoguelitePrototype/Development/P03E2HF03B/Geometry-")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));IFileManager::Get().MakeDirectory(*P,true);return P;}
void Save(const FString& N,const FString& S){FFileHelper::SaveStringToFile(S,*(Dir()/N),FFileHelper::EEncodingOptions::ForceUTF8);}
bool OwnWorld(){return GEditor&&!GEditor->PlayWorld&&GEditor->GetEditorWorldContext().World()->GetOutermost()->GetName()==Map;}
ARogueliteOpenPassagePrototypeRig* Main(){if(!OwnWorld())return nullptr;for(TActorIterator<ARogueliteOpenPassagePrototypeRig> I(GEditor->GetEditorWorldContext().World());I;++I)if(!I->IsActorBeingDestroyed()&&I->GetActorLabel()==TEXT("HF03B_Ceiling_Controls"))return *I;return nullptr;}
UStaticMeshComponent* Mesh(AActor* A,FName Name){TArray<UStaticMeshComponent*> All;A->GetComponents(All);for(auto* M:All)if(M->GetFName()==Name)return M;return nullptr;}
FString RoomShapes(AActor* A){FString S;TArray<UStaticMeshComponent*> All;A->GetComponents(All);for(auto* M:All)S+=M->GetName()+M->GetRelativeTransform().ToString()+FString::Printf(TEXT("|%d|%d\n"),M->IsVisible(),int32(M->GetCollisionEnabled()));return S;}
bool TopChecks(FAutomationTestBase* Test,ARogueliteOpenPassagePrototypeRig* R,FString& Text)
{
    auto* C=R->Connector.Get();auto* W=C->GetWorld();const FTransform T=C->GetActorTransform();bool Good=true;
    TArray<UStaticMeshComponent*> All;C->GetComponents(All);Good&=All.Num()==10;
    auto* Roof=Mesh(C,TEXT("Ceiling"));Good&=Roof&&Roof->IsVisible()==C->bGenerateCeiling&&Roof->GetCollisionEnabled()==(C->bGenerateCeiling?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
    for(FName Name:{FName(TEXT("Floor")),FName(TEXT("LeftWall")),FName(TEXT("RightWall"))}){auto* M=Mesh(C,Name);Good&=M&&M->IsVisible()&&M->GetCollisionEnabled()==ECollisionEnabled::QueryAndPhysics;}
    FCollisionQueryParams Params(SCENE_QUERY_STAT(HF03BTop),false);Params.AddIgnoredActor(R->Assembly);
    const FCollisionShape Shape=FCollisionShape::MakeCapsule(34,88);
    for(float X:{-300.f,0.f,300.f})for(float Y:{-150.f,0.f,150.f})
    {
        FHitResult Ray,Up;const FVector Start=T.TransformPosition(FVector(X,Y,150)),End=T.TransformPosition(FVector(X,Y,600));
        const bool RayHit=W->LineTraceSingleByChannel(Ray,Start,End,ECC_Visibility,Params);
        const bool CapsuleHit=W->SweepSingleByChannel(Up,Start,End,FQuat::Identity,ECC_Pawn,Shape,Params);
        const bool Fits=!W->OverlapBlockingTestByChannel(Start,FQuat::Identity,ECC_Pawn,Shape,Params);
        const bool RayGood=C->bGenerateCeiling?RayHit&&Ray.GetComponent()==Roof&&!Ray.bStartPenetrating:!RayHit;
        const bool UpGood=C->bGenerateCeiling?CapsuleHit&&Up.GetComponent()==Roof&&!Up.bStartPenetrating&&Up.ImpactNormal.Z<-.99f:!CapsuleHit&&!W->OverlapBlockingTestByChannel(End,FQuat::Identity,ECC_Pawn,Shape,Params);
        Good&=RayGood&&UpGood&&Fits;
        Text+=FString::Printf(TEXT("x%.0f y%.0f Ceiling=%d Ray=%d Capsule=%d Fits=%d RayHit=%s CapsuleHit=%s Passed=%d\n"),X,Y,C->bGenerateCeiling,RayHit,CapsuleHit,Fits,*GetPathNameSafe(Ray.GetComponent()),*GetPathNameSafe(Up.GetComponent()),RayGood&&UpGood&&Fits);
    }
    for(float Sign:{-1.f,1.f}){FHitResult H;Good&=W->LineTraceSingleByChannel(H,T.TransformPosition(FVector(0,0,100)),T.TransformPosition(FVector(0,Sign*400,100)),ECC_Pawn,Params)&&H.GetActor()==C&&FMath::Abs(H.ImpactNormal.Z)<.01f;}
    FHitResult F;Good&=W->LineTraceSingleByChannel(F,T.TransformPosition(FVector(0,0,100)),T.TransformPosition(FVector(0,0,-100)),ECC_Pawn,Params)&&F.GetComponent()==Mesh(C,TEXT("Floor"))&&F.ImpactNormal.Z>.99f;
    Test->TestTrue(TEXT("Independent upward rays/capsules, floor/side collision and ten fixed components"),Good);return Good;
}
bool Configure(FAutomationTestBase* Test,ARogueliteOpenPassagePrototypeRig* R,bool Open,bool Ceiling)
{
    const FTransform A=R->RoomA->GetActorTransform(),B=R->RoomB->GetActorTransform(),C=R->Connector->GetActorTransform();const FGuid Id=ARogueliteRoomAssembly::GetCorridorConnectionId(R->Connector);
    Test->TestTrue(TEXT("Original HF03 mode interface"),R->Configure(Open?ERogueliteOpeningMode::OpenPassage:ERogueliteOpeningMode::Legacy));
    const FString RoomA=RoomShapes(R->RoomA),RoomB=RoomShapes(R->RoomB);const auto Before=R->Connector->GetCorridorSockets();
    auto* Property=FindFProperty<FProperty>(ARogueliteParametricCorridor::StaticClass(),TEXT("bGenerateCeiling"));Test->TestNotNull(TEXT("Reflected ceiling checkbox"),Property);if(!Property)return false;
    Test->TestTrue(TEXT("Checkbox editable outside PIE"),R->Connector->CanEditChange(Property));
    R->Connector->bGenerateCeiling=Ceiling;FPropertyChangedEvent E(Property);R->Connector->PostEditChangeProperty(E);R->ValidatePrototype();
    const auto Result=R->LastValidation;Test->TestTrue(TEXT("C2 original authority validates selected roof state: ")+Result.Message,Result.bSucceeded&&Result.bGeometryPassed&&Result.bCapsulePassed);
    const int32 Count=(Open?75:59)+(Ceiling?0:9);Test->TestEqual(TEXT("ON original count; OFF truthfully adds nine upward capsules"),Result.Probes.Num(),Count);
    const auto After=R->Connector->GetCorridorSockets();bool Sockets=Before.Num()==After.Num();for(int32 I=0;I<Before.Num()&&I<After.Num();++I)Sockets&=Before[I].DoorSocket.WorldPosition.Equals(After[I].DoorSocket.WorldPosition)&&Before[I].DoorSocket.WorldOutwardDirection.Equals(After[I].DoorSocket.WorldOutwardDirection)&&Before[I].DoorSocket.DoorWidth==After[I].DoorSocket.DoorWidth&&Before[I].DoorSocket.DoorHeight==After[I].DoorSocket.DoorHeight&&Before[I].ConnectionId==After[I].ConnectionId;
    Test->TestTrue(TEXT("Ceiling alone changes no Room/POI shapes, poses, dimensions, sockets or reservation"),RoomA==RoomShapes(R->RoomA)&&RoomB==RoomShapes(R->RoomB)&&A.Equals(R->RoomA->GetActorTransform())&&B.Equals(R->RoomB->GetActorTransform())&&C.Equals(R->Connector->GetActorTransform())&&Sockets&&Id==Result.ConnectionId&&R->Assembly->Connections.Num()==1);
    auto* Header=Mesh(R->Connector,TEXT("StartFrameLintel"));Test->TestTrue(TEXT("Legacy end header independent from corridor ceiling"),Header&&Header->IsVisible()!=Open&&Header->GetCollisionEnabled()==(Open?ECollisionEnabled::NoCollision:ECollisionEnabled::QueryAndPhysics));
    static int32 Serial=0;const FString Prefix=FString::Printf(TEXT("%04d-%s-%s"),++Serial,Open?TEXT("open"):TEXT("legacy"),Ceiling?TEXT("on"):TEXT("off"));
    FString Evidence=TEXT("Corridor pose: ")+C.ToString()+TEXT("\n");const bool Top=TopChecks(Test,R,Evidence);Save(Prefix+TEXT("-top.txt"),Evidence);
    FString Export;FRogueliteAssemblyResult::StaticStruct()->ExportText(Export,&Result,nullptr,nullptr,PPF_None,nullptr);Save(Prefix+TEXT("-c2.txt"),Export);
    return Result.bSucceeded&&Top;
}
bool View(FAutomationTestBase* Test,bool Open,bool Ceiling)
{
    auto* R=Main();if(!R){Test->AddError(TEXT("Owned HF03-B map and fixture required"));return false;}if(!Configure(Test,R,Open,Ceiling))return false;
    for(auto* V:GEditor->GetLevelViewportClients())if(V&&V->Viewport){V->SetGameView(true);V->SetViewLocation(FVector(1770,-1000,950));V->SetViewRotation(FRotator(-38,90,0));V->Invalidate();}GEditor->SelectNone(false,true,false);return true;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF03BDefault,"Roguelite.CorridorCeiling.DefaultAndReflection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF03BDefault::RunTest(const FString&){auto* P=FindFProperty<FProperty>(ARogueliteParametricCorridor::StaticClass(),TEXT("bGenerateCeiling"));TestTrue(TEXT("Old assets retain roof by default"),GetDefault<ARogueliteParametricCorridor>()->bGenerateCeiling);return TestTrue(TEXT("Real editable/read-only-to-Blueprint reflected checkbox"),P&&P->HasAnyPropertyFlags(CPF_Edit)&&P->HasAnyPropertyFlags(CPF_BlueprintReadOnly)&&P->GetMetaData(TEXT("DisplayName"))==TEXT("Generate Corridor Ceiling"));}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF03BCreate,"ProjectAuthoring.HF03B.CreateMap",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF03BCreate::RunTest(const FString&)
{
    if(!GEditor||GEditor->PlayWorld)return false;if(FPaths::FileExists(FPackageName::LongPackageNameToFilename(Map,FPackageName::GetMapPackageExtension()))){AddError(TEXT("Existing map protected; load and inspect rather than overwrite"));return false;}
    auto* W=UEditorLoadingAndSavingUtils::NewBlankMap(false);if(!W)return false;W->GetWorldSettings()->DefaultGameMode=ARogueliteDevGameMode::StaticClass();
    auto* R=ARogueliteOpenPassagePrototypeRig::CreateEditorPrototype(W,FTransform::Identity);if(!R)return false;R->SetActorLabel(TEXT("HF03B_Ceiling_Controls"));R->Connector->SetActorLabel(TEXT("HF03B_Corridor"));
    W->SpawnActor<APlayerStart>(FVector(0,0,150),FRotator::ZeroRotator);W->SpawnActor<ADirectionalLight>(FVector(0,0,1000),FRotator(-45,-30,0));W->SpawnActor<ASkyLight>();
    return TestTrue(TEXT("Only new HF03-B map saved"),UEditorLoadingAndSavingUtils::SaveMap(W,Map));
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHF03BMatrix,"ProjectVerification.HF03B.GeometryMatrix",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHF03BMatrix::RunTest(const FString&)
{
    if(!OwnWorld())return false;auto* W=GEditor->GetEditorWorldContext().World();int32 Before=0;for(TActorIterator<AActor> I(W);I;++I)if(!I->IsActorBeingDestroyed())++Before;
    for(int32 Shift=0;Shift<2;++Shift)
    {
        auto* R=ARogueliteOpenPassagePrototypeRig::CreateEditorPrototype(W,FTransform(FRotator(0,Shift*90,0),FVector(30000,30000,0)),ERogueliteRoomSide::North,Shift?150:0,Shift?-175:0,FVector(3000,2500,400));TestNotNull(TEXT("Owned matrix rig"),R);if(!R)return false;
        for(bool Open:{false,true})for(bool Roof:{true,false})Configure(this,R,Open,Roof);
        for(int32 I=0;I<10;++I)Configure(this,R,true,I%2==0);
        auto* Obstacle=W->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Obstacle);Obstacle->SetRootComponent(Box);Box->SetBoxExtent(FVector(100,100,10));Box->SetCollisionProfileName(TEXT("BlockAll"));Box->RegisterComponent();Obstacle->SetActorLocation(R->Connector->GetActorLocation()+FVector(0,0,410));
        auto Bad=R->Assembly->RevalidateConnection(R->LastValidation.ConnectionId);bool UpFailed=false;for(const auto& P:Bad.Probes)UpFailed|=P.Name.Contains(TEXT("open top PawnCapsule"))&&!P.bPassed;TestTrue(TEXT("Invisible injected roof collision is rejected"),!Bad.bSucceeded&&UpFailed);FString BadText;FRogueliteAssemblyResult::StaticStruct()->ExportText(BadText,&Bad,nullptr,nullptr,PPF_None,nullptr);Save(FString::Printf(TEXT("negative-invisible-roof-%d.txt"),Shift),BadText);Obstacle->Destroy();R->ValidatePrototype();TestTrue(TEXT("Removing only own obstacle restores connection"),R->LastValidation.bSucceeded);
        R->DestroyOwnTestFixture();
    }
    int32 After=0;for(TActorIterator<AActor> I(W);I;++I)if(!I->IsActorBeingDestroyed())++After;TestEqual(TEXT("Only owned ephemeral fixtures cleaned"),After,Before);return true;
}
#define HF03B_VIEW(Class,Name,Open,Roof) IMPLEMENT_SIMPLE_AUTOMATION_TEST(Class,Name,EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter) bool Class::RunTest(const FString&){return View(this,Open,Roof);}
HF03B_VIEW(FHF03BLegacyOn,"ProjectAuthoring.HF03B.LegacyOn",false,true)
HF03B_VIEW(FHF03BLegacyOff,"ProjectAuthoring.HF03B.LegacyOff",false,false)
HF03B_VIEW(FHF03BOpenOn,"ProjectAuthoring.HF03B.OpenOn",true,true)
HF03B_VIEW(FHF03BOpenOff,"ProjectAuthoring.HF03B.OpenOff",true,false)
#undef HF03B_VIEW
#endif
