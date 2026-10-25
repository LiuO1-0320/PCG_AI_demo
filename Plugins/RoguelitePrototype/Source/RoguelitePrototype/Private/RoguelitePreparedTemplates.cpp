#include "RoguelitePreparedTemplates.h"
#include "RogueliteSeedAssemblyInternal.h"
#include "Engine/World.h"

FString FRoguelitePreparedTemplateCatalog::OptionDigest(const FRogueliteSeedTemplateOption& Option)
{
    FString Text;FRogueliteSeedTemplateOption::StaticStruct()->ExportText(Text,&Option,nullptr,nullptr,PPF_None,nullptr);
    return FRogueliteRoomGraphGenerator::StableTextDigest(Text);
}
FString FRoguelitePreparedTemplateCatalog::ComputeDigest() const
{
    auto Value=*this;Value.Digest.Reset();FString Text;
    StaticStruct()->ExportText(Text,&Value,nullptr,nullptr,PPF_None,nullptr);
    return FRogueliteRoomGraphGenerator::StableTextDigest(Text);
}
bool FRoguelitePreparedTemplateCatalog::IsCurrent(const FRogueliteSeedLayoutSettings& Settings,FString& Reason) const
{
    if(Version!=1||TemplateCatalogVersion!=Settings.TemplateCatalogVersion||Rows.Num()!=Settings.Templates.Num()
        ||Rows.IsEmpty()||Rows.Num()>16||Digest.IsEmpty()||Digest!=ComputeDigest())
    {Reason=TEXT("Prepared template catalog absent/stale/invalid; prepare it in Editor before PIE. No GameWorld probe spawned.");return false;}
    for(const auto& Option:Settings.Templates)
    {
        const auto* Row=Rows.FindByPredicate([&](const auto& R){return R.Id==Option.Id;});
        if(!Row||Row->TemplateClass!=Option.TemplateClass||Row->OptionDigest!=OptionDigest(Option)
            ||Row->Sockets.IsEmpty()||Row->Sockets.Num()>4||Row->MemberSignature.IsEmpty())
        {Reason=TEXT("Prepared template option changed or has no verified authored ports/content: ")+Option.Id.ToString();return false;}
    }
    Reason=TEXT("Current sealed Editor-prepared template data; runtime instances must still match actual content.");return true;
}
bool FRoguelitePreparedTemplateCatalog::CaptureEditor(AActor* Owner,const FRogueliteSeedLayoutSettings& Settings,
    FRoguelitePreparedTemplateCatalog& Out,FString& Reason)
{
    if(!IsValid(Owner)||!Owner->GetWorld()||Owner->GetWorld()->WorldType!=EWorldType::Editor)
    {Reason=TEXT("Template capture is Editor-only. Runtime never creates metadata probe actors.");return false;}
    RogueliteSeedAssemblyInternal::Inputs Input;FRogueliteSeedBatch Metadata;Metadata.BatchId=FGuid::NewGuid();
    if(!RogueliteSeedAssemblyInternal::BuildInputs(Owner,Settings,Metadata,Input,Reason))return false;
    FRoguelitePreparedTemplateCatalog Result;Result.TemplateCatalogVersion=Settings.TemplateCatalogVersion;
    for(const auto& Entry:Input.Catalog)
    {
        auto& Row=Result.Rows.AddDefaulted_GetRef();Row.Id=Entry.Option.Id;Row.TemplateClass=Entry.Option.TemplateClass;
        Row.OptionDigest=OptionDigest(Entry.Option);Row.Descriptor=Entry.Descriptor;Row.Sockets=Entry.Sockets;
        Row.Signature=Entry.Signature;Row.MemberSignature=Entry.MemberSignature;
        // Strip meaningless probe-world coordinates; keep the original P02 local socket format.
        for(auto& Socket:Row.Sockets){Socket.WorldPosition=Socket.LocalPosition;Socket.WorldOutwardDirection=Socket.LocalOutwardDirection;}
        Row.Descriptor.DoorSockets=Row.Sockets;
    }
    Result.Digest=Result.ComputeDigest();if(!Result.IsCurrent(Settings,Reason))return false;
    Out=MoveTemp(Result);Reason=TEXT("Editor authored descriptors captured, temporary probes destroyed; catalog ready for probe-free Runtime planning.");return true;
}
