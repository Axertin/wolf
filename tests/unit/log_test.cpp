#include <doctest/doctest.h>

#include <log/log.hpp>
#include <mods/mod.hpp>
#include <string>

#include "support.hpp"

TEST_SUITE( "log" )
{
    TEST_CASE( "writeLog formats the line and passes the mod and level to the sink" )
    {
        logs::start();
        WolfMod mod{};
        wolf::writeLog( &mod, WOLF_LOG_WARN, "%s table passed %u entries", "posts", 1024U );
        const auto lines = logs::lines();
        REQUIRE( lines.size() == 1 );
        CHECK( lines[ 0 ].mod == &mod );
        CHECK( lines[ 0 ].level == WOLF_LOG_WARN );
        CHECK( lines[ 0 ].text == "posts table passed 1024 entries" );
    }

    TEST_CASE( "logOnce is true once per mod and bit" )
    {
        WolfMod a{};
        WolfMod b{};
        CHECK( wolf::logOnce( &a, wolf::kOnceOutOfMemory ) );
        CHECK_FALSE( wolf::logOnce( &a, wolf::kOnceOutOfMemory ) );
        CHECK( wolf::logOnce( &a, wolf::kOncePendingFull ) );
        CHECK( wolf::logOnce( &b, wolf::kOnceOutOfMemory ) );
    }
}
