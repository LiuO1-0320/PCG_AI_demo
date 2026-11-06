#include "Tests/RogueliteProductionCandidateRoom.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
ARogueliteProductionCandidateRoom::ARogueliteProductionCandidateRoom()
{
    M1BaselinePOI=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("M1_BaselinePOI"));
    M1BaselinePOI->SetupAttachment(TemplateContentRoot);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    M1BaselinePOI->SetStaticMesh(Cube.Object);M1BaselinePOI->SetRelativeLocation(FVector(-300,-300,50));
    M1BaselinePOI->SetRelativeScale3D(FVector(.5,.5,1));M1BaselinePOI->SetCollisionProfileName(TEXT("BlockAll"));M1BaselinePOI->SetMobility(EComponentMobility::Static);
}
