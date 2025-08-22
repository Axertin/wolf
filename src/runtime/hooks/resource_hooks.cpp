#include "resource_hooks.h"

#include <string>

#include "../utilities/logger.h"
#include "../wolf_runtime_api.h"

#include <MinHook.h>

//==============================================================================
// RESOURCE LOADING & MANAGEMENT HOOKS
//==============================================================================

namespace wolf::runtime::hooks
{

// Hook function pointers
static const void *(__fastcall *oLoadRsc)(void *rscPackage, const char *type, uint32_t idx);
static void *(__fastcall *oLoadResourcePackageAsync)(void *filesystem, const char *filename, void **outputRscData, void *heap, int32_t, int32_t, int32_t,
                                                     int32_t);
static int64_t(__fastcall *oGXTextureManager_GetNumEntries)(void *textureManager, int32_t texGroup);
static void(__fastcall *oLoadCore20MSD)(void *msgStruct);

// Hook implementations
const void *__fastcall onLoadRsc(void *rscPackage, const char *type, uint32_t idx)
{
    // Check for resource interception through runtime API
    std::string resourcePath = std::string(type) + "/" + std::to_string(idx);
    const char *intercepted = wolf::runtime::internal::interceptResourceLoad(resourcePath.c_str());

    if (intercepted)
    {
        logDebug("[WOLF] Resource intercepted: %s -> %s", resourcePath.c_str(), intercepted);
        // TODO: Load intercepted resource
        // For now, fall back to original
    }

    return oLoadRsc(rscPackage, type, idx);
}

void *__fastcall onLoadResourcePackageAsync(void *filesystem, const char *filename, void **outputRscData, void *heap, int32_t a5, int32_t a6, int32_t a7,
                                            int32_t a8)
{
    if (filename)
    {
        // Check for resource interception through runtime API
        const char *intercepted = wolf::runtime::internal::interceptResourceLoad(filename);

        if (intercepted)
        {
            logDebug("[WOLF] Resource package intercepted: %s -> %s", filename, intercepted);
            filename = intercepted;
        }
    }

    return oLoadResourcePackageAsync(filesystem, filename, outputRscData, heap, a5, a6, a7, a8);
}

int64_t __fastcall onGXTextureManager_GetNumEntries(void *textureManager, int32_t texGroup)
{
    if (texGroup == 4)
    {
        // Increase texture limit for modded content
        return 300;
    }
    return oGXTextureManager_GetNumEntries(textureManager, texGroup);
}

void __fastcall onLoadCore20MSD(void *msgStruct)
{
    oLoadCore20MSD(msgStruct);

    // TODO: Allow mods to modify MSD data through runtime API
    // This would be where mods could add custom text strings
    logDebug("[WOLF] Core20 MSD loaded - ready for mod string additions");
}

bool setupResourceHooks(uintptr_t mainBase)
{
    logDebug("[WOLF] Setting up resource hooks...");

    // Resource loading hooks
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x1B1770), reinterpret_cast<LPVOID>(&onLoadRsc), reinterpret_cast<LPVOID *>(&oLoadRsc)) != MH_OK)
        return false;
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x1AFC90), reinterpret_cast<LPVOID>(&onLoadResourcePackageAsync),
                      reinterpret_cast<LPVOID *>(&oLoadResourcePackageAsync)) != MH_OK)
        return false;
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x1412B0), reinterpret_cast<LPVOID>(&onGXTextureManager_GetNumEntries),
                      reinterpret_cast<LPVOID *>(&oGXTextureManager_GetNumEntries)) != MH_OK)
        return false;
    if (MH_CreateHook(reinterpret_cast<void *>(mainBase + 0x1C9510), reinterpret_cast<LPVOID>(&onLoadCore20MSD), reinterpret_cast<LPVOID *>(&oLoadCore20MSD)) !=
        MH_OK)
        return false;

    return true;
}

} // namespace wolf::runtime::hooks
