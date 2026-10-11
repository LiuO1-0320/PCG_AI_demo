#include "RoguelitePrototype.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogRoguelitePrototype);

class FRoguelitePrototypeModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        UE_LOG(LogRoguelitePrototype, Display, TEXT("ROGUELITE_MODULE_LOADED"));
    }
};

IMPLEMENT_MODULE(FRoguelitePrototypeModule, RoguelitePrototype)
