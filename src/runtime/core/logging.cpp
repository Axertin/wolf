#include "logging.h"

#include "../utilities/logger.h"
#include "mod_lifecycle.h" // For findMod function

// C API implementations
extern "C"
{
    void wolfRuntimeLog(WolfModId mod_id, WolfLogLevel level, const char *message)
    {
        if (!message)
            return;

        std::string logMessage;
        ModInfo *mod = findMod(mod_id);

        if (mod)
        {
            logMessage = mod->logPrefix + " " + message;
        }
        else
        {
            logMessage = "[Unknown] ";
            logMessage += message;
        }

        switch (level)
        {
        case WOLF_LOG_INFO:
            ::logInfo(logMessage);
            break;
        case WOLF_LOG_WARNING:
            ::logWarning(logMessage);
            break;
        case WOLF_LOG_ERROR:
            ::logError(logMessage);
            break;
        case WOLF_LOG_DEBUG:
            ::logDebug(logMessage);
            break;
        default:
            ::logInfo(logMessage);
            break;
        }
    }

    void wolfRuntimeSetLogPrefix(WolfModId mod_id, const char *prefix)
    {
        if (!prefix)
            return;

        ModInfo *mod = findMod(mod_id);
        if (mod)
        {
            mod->logPrefix = prefix;
        }
    }

} // extern "C"