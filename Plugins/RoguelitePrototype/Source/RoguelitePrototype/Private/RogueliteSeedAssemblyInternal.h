#pragma once
#include "RogueliteSeedAssembly.h"

// Shared B2/B3A catalog, ordered candidate rules and signatures; never a reservation authority.
struct FRoguelitePreparedTemplateCatalog;
struct FRogueliteRoomTypeAssignmentResult;
namespace RogueliteSeedAssemblyInternal
{
    struct CatalogEntry
    {
        FRogueliteSeedTemplateOption Option;
        UClass* Class=nullptr;
        int32 Capacity=0;
        FString Signature, MemberSignature;
        FRogueliteTemplateDescriptor Descriptor;
        TArray<FRogueliteDoorSocket> Sockets;
    };
    struct Inputs
    {
        FRogueliteRoomGraph Graph;
        TArray<CatalogEntry> Catalog;
        TArray<FRogueliteSeedCorridorProfile> Profiles;
        TArray<ERogueliteSeedRoomType> Types;
        TArray<FName> Encounters;
        TArray<FName> EventDefinitions;
    };
    FName BatchTag(const FGuid& Id);
    FString MembersText(ARogueliteRoomTemplate* Room);
    bool Rigid(const FTransform& Transform);
    bool BuildInputs(AActor* Owner,const FRogueliteSeedLayoutSettings& Settings,FRogueliteSeedBatch& Batch,Inputs& Out,FString& Reason,const FRoguelitePreparedTemplateCatalog* Prepared=nullptr);
    bool ValidateRoomRoleRules(const FRogueliteSeedLayoutSettings& Settings,FString& Reason);
    bool IsTemplateRoleAllowed(FName Id,const FRogueliteRoomGraph& Graph,int32 Node,const FRogueliteSeedLayoutSettings& Settings);
    FRogueliteRoomTypeAssignmentResult AssignTypesForLayout(const FRogueliteRoomGraph& Graph,const FRogueliteSeedLayoutSettings& Settings,
        const TArray<TArray<ERogueliteSeedRoomType>>& Compatible={});
    TArray<int32> TemplateOrder(const TArray<CatalogEntry>& Catalog,ERogueliteSeedRoomType Type,int32 Degree,int32 Node,const FRogueliteSeedLayoutSettings& Settings,const FRogueliteRoomGraph& Graph);
    TArray<FRogueliteSeedCandidateRecord> Candidates(const Inputs& Input,const FRogueliteSeedLayoutSettings& Settings,
        int32 ChildNode,int32 EdgeId,TArray<FRogueliteDoorSocket> FreeParentPorts,ERogueliteRoomSide Preferred);
}
