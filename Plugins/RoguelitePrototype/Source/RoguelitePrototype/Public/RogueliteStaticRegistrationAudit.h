#pragma once
#include "CoreMinimal.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"

/** Native evidence only; never expands component/probe arrays in Details. */
struct FRogueliteStaticRegistrationAudit
{
    FString Before,FirstRegistered;
    int32 RegisteredBefore=0,RegisteredAfter=0;
    static FString Signature(const AActor* Actor,int32& Registered)
    {
        TInlineComponentArray<UStaticMeshComponent*> Components;Actor->GetComponents(Components);
        TArray<FString> Rows;Registered=0;
        for(const auto* C:Components)if(C->CreationMethod==EComponentCreationMethod::Native)
        {Registered+=C->IsRegistered();Rows.Add(C->GetName()+C->GetRelativeTransform().ToString()+GetPathNameSafe(C->GetStaticMesh())+FString::FromInt(uint8(C->GetCollisionEnabled())));}
        Rows.Sort();return Actor->GetActorTransform().ToString()+FString::Join(Rows,TEXT("|"));
    }
    void Prepared(const AActor* Actor){if(Before.IsEmpty())Before=Signature(Actor,RegisteredBefore);}
    void Registered(const AActor* Actor){if(FirstRegistered.IsEmpty())FirstRegistered=Signature(Actor,RegisteredAfter);}
    bool Passed(const AActor* Actor) const
    {int32 Count=0;return !Before.IsEmpty()&&RegisteredBefore==0&&RegisteredAfter>0&&Before==FirstRegistered&&FirstRegistered==Signature(Actor,Count);}
    FString Describe(const AActor* Actor) const
    {return FString::Printf(TEXT("Actor=%s PreRegistered=%d FirstRegistered=%d PreparedBeforeRegistration=%d GeometryUnchangedAfterConstruction=%d"),*Actor->GetPathName(),RegisteredBefore,RegisteredAfter,RegisteredBefore==0&&Before==FirstRegistered,Passed(Actor));}
};
