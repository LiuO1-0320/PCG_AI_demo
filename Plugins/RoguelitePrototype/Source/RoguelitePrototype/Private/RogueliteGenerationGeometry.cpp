#include "RogueliteGenerationGeometry.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"
#include "UObject/UnrealType.h"
#include "UObject/StructOnScope.h"

namespace
{
// Match the original reflected ExportText representation while excluding only HF03-C
// extension fields. This retains legacy sealed-catalog/settings identities.
FString LegacyText(const UScriptStruct* Type, const void* Value, const void* Defaults=nullptr)
{
    FString Text=TEXT("("); bool First=true;
    for (TFieldIterator<FProperty> It(Type);It;++It)
    {
        const FProperty* P=*It; const FName N=P->GetFName();
        const FString T=Type->GetName();
        if ((T==TEXT("RogueliteSeedLayoutSettings")&&N==TEXT("GenerationGeometry"))
            ||(T==TEXT("RogueliteSeedCorridorProfile")&&N==TEXT("bGenerateCeiling"))
            ||(T==TEXT("RoguelitePreparedTemplateRow")&&(N==TEXT("SourceDescriptor")||N==TEXT("EffectiveDoors")))
            ||(T==TEXT("RoguelitePreparedTemplateCatalog")&&N==TEXT("GeometrySettingsDigest"))) continue;
        if(!P->ShouldPort(PPF_None))continue;
        FString Part; const void* Data=P->ContainerPtrToValuePtr<void>(Value);
        const void* Delta=Defaults?P->ContainerPtrToValuePtr<void>(Defaults):nullptr;
        if(!P->ExportText_Direct(Part,Data,Delta,nullptr,PPF_Delimited))continue;
        if(const auto* Array=CastField<FArrayProperty>(P))
        {
            const auto* Inner=CastField<FStructProperty>(Array->Inner);
            if(Inner&&(Inner->Struct==FRogueliteSeedCorridorProfile::StaticStruct()
                ||Inner->Struct->GetName()==TEXT("RoguelitePreparedTemplateRow")))
            {
                FScriptArrayHelper Values(Array,Data);FStructOnScope InnerDefaults(Inner->Struct);Part=TEXT("(");
                for(int32 I=0;I<Values.Num();++I){if(I)Part+=TEXT(",");Part+=LegacyText(Inner->Struct,Values.GetRawPtr(I),InnerDefaults.GetStructMemory());}
                Part+=TEXT(")");
            }
        }
        if(!First)Text+=TEXT(",");First=false;Text+=P->GetName()+TEXT("=")+Part;
    }
    return Text+TEXT(")");
}
}
namespace RogueliteGenerationGeometry
{
bool IsLegacyDefaults(const FRogueliteSeedLayoutSettings& S)
{
    if(S.GenerationGeometry.OpeningMode!=ERogueliteOpeningMode::Legacy)return false;
    for(const auto& C:S.Corridors)if(!C.bGenerateCeiling)return false;
    return true;
}
FString SettingsText(const FRogueliteSeedLayoutSettings& S)
{
    if(IsLegacyDefaults(S))return LegacyText(FRogueliteSeedLayoutSettings::StaticStruct(),&S);
    FString Text;FRogueliteSeedLayoutSettings::StaticStruct()->ExportText(Text,&S,nullptr,nullptr,PPF_None,nullptr);return Text;
}
FString SettingsDigest(const FRogueliteSeedLayoutSettings& S)
{return FRogueliteRoomGraphGenerator::StableTextDigest(SettingsText(S));}
FString GeometryDigest(const FRogueliteSeedLayoutSettings& S)
{
    FString Text=FString::Printf(TEXT("HF03CGeometryV1|mode=%d|height=%.6f"),uint8(S.GenerationGeometry.OpeningMode),S.GenerationGeometry.OpenPassageRoomHeight);
    auto Profiles=S.Corridors;Profiles.Sort([](const auto& A,const auto& B){return A.Id.ToString()<B.Id.ToString();});
    for(const auto& P:Profiles)Text+=FString::Printf(TEXT("|%s,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d"),*P.Id.ToString(),P.bEnabled,P.Width,P.Height,P.WallThickness,P.FloorThickness,P.DoorWidth,P.DoorHeight,P.bGenerateCeiling);
    return FRogueliteRoomGraphGenerator::StableTextDigest(Text);
}
FString LegacyCatalogText(const UScriptStruct* Type,const void* Value){return LegacyText(Type,Value);}
TArray<FRogueliteDoorConfig> Doors(const ARogueliteParametricRoom* R)
{return {R->NorthDoor,R->SouthDoor,R->EastDoor,R->WestDoor};}
TArray<FRogueliteDoorConfig> EffectiveDoors(const ARogueliteParametricRoom* R,const FRogueliteSeedLayoutSettings& S)
{
    auto Result=Doors(R);
    if(S.GenerationGeometry.OpeningMode==ERogueliteOpeningMode::OpenPassage)
        if(const auto* P=S.Corridors.FindByPredicate([](const auto& C){return C.bEnabled;}))
            for(auto& D:Result)if(D.bEnabled){D.OpeningMode=ERogueliteOpeningMode::OpenPassage;D.DoorWidth=P->Width;D.DoorHeight=S.GenerationGeometry.OpenPassageRoomHeight;}
    return Result;
}
bool Validate(const FRogueliteSeedLayoutSettings& S,FString& Reason)
{
    const auto& G=S.GenerationGeometry;
    if(G.OpeningMode!=ERogueliteOpeningMode::Legacy&&G.OpeningMode!=ERogueliteOpeningMode::OpenPassage)
    {Reason=TEXT("GenerationOpeningModeInvalid: no automatic Legacy fallback.");return false;}
    if(G.OpeningMode==ERogueliteOpeningMode::OpenPassage)
    {
        if(!FMath::IsFinite(G.OpenPassageRoomHeight)||G.OpenPassageRoomHeight!=500)
        {Reason=TEXT("OpenPassageInstanceHeightInvalid: this authorized adapter requires 500 cm.");return false;}
        for(const auto& P:S.Corridors)if(P.bEnabled&&(P.DoorWidth!=P.Width||P.DoorHeight!=G.OpenPassageRoomHeight||P.Height!=G.OpenPassageRoomHeight))
        {Reason=TEXT("OpenPassageProfileMismatch: full-width opening and uniform 500 cm instance/corridor height required.");return false;}
    }
    return true;
}
bool ConfigureTemplate(ARogueliteRoomTemplate* R,const FRogueliteSeedLayoutSettings& S,FString& Reason)
{
    if(!IsValid(R)||!R->GetWorld()||!Validate(S,Reason))return false;
    if(R->GetWorld()->IsGameWorld()&&R->GetRootComponent()&&R->GetRootComponent()->IsRegistered())
    {Reason=TEXT("StaticGeometryAlreadyRegistered: instance adaptation refused; no values changed.");return false;}
    if(S.GenerationGeometry.OpeningMode==ERogueliteOpeningMode::Legacy)return true;
    const FRogueliteSeedCorridorProfile* P=S.Corridors.FindByPredicate([](const auto& C){return C.bEnabled;});
    if(!P){Reason=TEXT("OpenPassageCorridorProfileMissing");return false;}
    for(const auto& C:S.Corridors)if(C.bEnabled&&C.Width!=P->Width)
    {Reason=TEXT("OpenPassageProfileWidthsDiffer: one room cannot receive conflicting opening widths.");return false;}
    auto Configs=Doors(R);
    // Validate authored enabled ports before adapting; do not repair invalid template data.
    for(int32 I=0;I<Configs.Num();++I)if(Configs[I].bEnabled)
    {FRogueliteDoorSocket D;if(!R->TryGetDoorSocket(static_cast<ERogueliteRoomSide>(I),D))
        {Reason=TEXT("AuthoredSocketInvalidBeforeAdaptation: ")+R->GetPathName();return false;}}
    Configs=EffectiveDoors(R,S);
    R->RoomHeight=S.GenerationGeometry.OpenPassageRoomHeight;
    R->NorthDoor=Configs[0];R->SouthDoor=Configs[1];R->EastDoor=Configs[2];R->WestDoor=Configs[3];
    if(!R->GetWorld()->IsGameWorld())R->RebuildRoom();
    Reason=TEXT("Explicit instance-only 500 cm Open Passage adaptation; authored assets/POI transforms unchanged.");return true;
}
void ApplyPlannedDoors(ARogueliteRoomTemplate* R,const FRogueliteSeedRoomRecord& Record)
{
    if(Record.EffectiveDoors.Num()==4)
    {R->NorthDoor=Record.EffectiveDoors[0];R->SouthDoor=Record.EffectiveDoors[1];R->EastDoor=Record.EffectiveDoors[2];R->WestDoor=Record.EffectiveDoors[3];}
}
}
