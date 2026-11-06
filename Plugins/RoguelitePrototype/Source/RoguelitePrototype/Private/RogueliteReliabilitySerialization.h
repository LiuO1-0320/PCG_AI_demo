#pragma once
#include "UObject/UnrealType.h"

namespace RogueliteReliabilitySerialization
{
// Strip only complete top-level fields. Nested arrays, structs, quoted commas
// and escaped quotes retain the exact native ExportText representation.
inline FString WithoutInactivePolicy(const FString& In)
{
    TArray<FString> Keep;int32 Depth=0,Start=1;bool Quoted=false;
    const auto Add=[&](int32 End){const FString Part=In.Mid(Start,End-Start);if(!Part.StartsWith(TEXT("ReliabilityPolicyVersion="))&&!Part.StartsWith(TEXT("OptionalTreeReservePercent=")))Keep.Add(Part);};
    for(int32 I=0;I<In.Len();++I)
    {
        const TCHAR C=In[I];
        if(Quoted&&C==TEXT('\\')){++I;continue;}
        if(C==TEXT('"')){Quoted=!Quoted;continue;}
        if(Quoted)continue;
        if(C==TEXT('('))++Depth;
        else if(C==TEXT(')')){if(Depth==1)Add(I);--Depth;}
        else if(C==TEXT(',')&&Depth==1){Add(I);Start=I+1;}
    }
    return TEXT("(")+FString::Join(Keep,TEXT(","))+TEXT(")");
}
inline FString Text(UScriptStruct* Type,const void* Value,bool LegacyPolicy,bool SkipRoles=false)
{
    if(!SkipRoles)
    {
        FString Native;Type->ExportText(Native,Value,nullptr,nullptr,PPF_None,nullptr);
        return LegacyPolicy?WithoutInactivePolicy(Native):Native;
    }
    // Pre-G1 profiles used this explicit all-property encoding, not native
    // default-value elision. Keep that separate legacy encoding unchanged.
    FString Out=TEXT("(");bool First=true;
    for(TFieldIterator<FProperty> It(Type);It;++It)
    {
        const FProperty* P=*It;const FName Name=P->GetFName();
        if(!P->ShouldPort(PPF_None)||Name==TEXT("RoomTemplateRoles")
            ||(LegacyPolicy&&(Name==TEXT("ReliabilityPolicyVersion")||Name==TEXT("OptionalTreeReservePercent"))))continue;
        FString Part;P->ExportText_Direct(Part,P->ContainerPtrToValuePtr<void>(Value),nullptr,nullptr,PPF_Delimited);
        if(!First)Out+=TEXT(",");First=false;Out+=P->GetName()+TEXT("=")+Part;
    }
    return Out+TEXT(")");
}
}
