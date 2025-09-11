#define NOIMGUI
#include <wolf_framework.hpp>

class HelloWorldMod
{
  public:
    static void earlyGameInit()
    {
        wolf::setLogPrefix("[HelloWorld]");
        wolf::logInfo("Hello World mod: Early game initialization");

        wolf::addCommand(
            "hello",
            [](const std::vector<std::string> &args)
            {
                if (args.size() > 1)
                {
                    wolf::consolePrintf("Hello, %s!", args[1].c_str());
                }
                else
                {
                    wolf::consolePrint("Hello, World!");
                }
            },
            "Say hello to someone or the world");
    }

    static void lateGameInit()
    {
        wolf::logInfo("Hello World mod: Late game initialization");

        wolf::onGameStart([]() { wolf::logInfo("Game started!"); });

        wolf::onItemPickup([](int itemId, int count) { wolf::logInfo("Item picked up: ID=%d, Count=%d", itemId, count); });
    }

    static void shutdown()
    {
        wolf::logInfo("Hello World mod: Shutting down");
    }

    static const char *getName()
    {
        return "Hello World Example";
    }

    static const char *getVersion()
    {
        return "1.0.0";
    }
};

WOLF_MOD_ENTRY_CLASS_NO_IMGUI(HelloWorldMod)
