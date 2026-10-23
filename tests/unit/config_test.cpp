#include <doctest/doctest.h>

#include <config/config.hpp>
#include <mods/mod.hpp>
#include <string>
#include <string_view>

#include "support.hpp"

TEST_SUITE( "config" )
{
    TEST_CASE( "a byte order mark and CRLF line ends parse like plain LF" )
    {
        static WolfMod mod;
        wolf::configParse( &mod, "\xEF\xBB\xBFspeed = 3\r\nname=Ammy\r\n", "config.ini" );
        CHECK( wolf::configInt( &mod, "speed", 0 ) == 3 );
        CHECK( std::string_view( wolf::configStr( &mod, "name", "" ) ) == "Ammy" );
    }

    TEST_CASE( "sections prefix their keys" )
    {
        static WolfMod mod;
        wolf::configParse( &mod, "[audio]\nvolume = 0.5\n", "config.ini" );
        CHECK( wolf::configFloat( &mod, "audio.volume", 0.0 ) == 0.5 );
        CHECK( wolf::configValue( &mod, "volume" ) == nullptr );
    }

    TEST_CASE( "a sectioned key of any length is kept whole" )
    {
        static WolfMod mod;
        const std::string longKey( 400, 'k' );
        const std::string otherKey = longKey + "2";
        wolf::configParse( &mod, ( "[" + std::string( 300, 's' ) + "]\n" + longKey + " = 1\n" + otherKey + " = 2\n" ), "c.ini" );
        const std::string section( 300, 's' );
        CHECK( wolf::configInt( &mod, ( section + "." + longKey ).c_str(), 0 ) == 1 );
        CHECK( wolf::configInt( &mod, ( section + "." + otherKey ).c_str(), 0 ) == 2 );
    }

    TEST_CASE( "comments, blank lines and a later duplicate" )
    {
        static WolfMod mod;
        wolf::configParse( &mod, "; c\n# c\n\nk = 1\nk = 2\n", "config.ini" );
        CHECK( wolf::configInt( &mod, "k", 0 ) == 2 );
    }

    TEST_CASE( "a line with no = is skipped" )
    {
        static WolfMod mod;
        wolf::configParse( &mod, "oops\nk=1\n", "mods/x/config.ini" );
        CHECK( wolf::configInt( &mod, "k", 0 ) == 1 );
        CHECK( wolf::configValue( &mod, "oops" ) == nullptr );
    }

    TEST_CASE( "an empty key is malformed too" )
    {
        static WolfMod mod;
        wolf::configParse( &mod, "k=1\n = 2\n", "c.ini" );
        CHECK( wolf::configInt( &mod, "k", 0 ) == 1 );
        CHECK( wolf::configValue( &mod, "" ) == nullptr );
    }

    TEST_CASE( "configInt takes decimal, hex and a sign, and nothing else" )
    {
        static WolfMod mod;
        wolf::configParse( &mod, "a=-12\nb=0x1F\nc=+7\nd=12abc\ne=0x\nf=\ng=99999999999999999999\n", "c.ini" );
        CHECK( wolf::configInt( &mod, "a", 5 ) == -12 );
        CHECK( wolf::configInt( &mod, "b", 5 ) == 31 );
        CHECK( wolf::configInt( &mod, "c", 5 ) == 7 );
        CHECK( wolf::configInt( &mod, "d", 5 ) == 5 );
        CHECK( wolf::configInt( &mod, "e", 5 ) == 5 );
        CHECK( wolf::configInt( &mod, "f", 5 ) == 5 );
        CHECK( wolf::configInt( &mod, "g", 5 ) == 5 );
    }

    TEST_CASE( "configBool's words" )
    {
        static WolfMod mod;
        wolf::configParse( &mod, "a=On\nb=YES\nc=true\nd=1\ne=off\nf=No\ng=FALSE\nh=0\ni=maybe\n", "c.ini" );
        for ( const char *key : { "a", "b", "c", "d" } )
        {
            CHECK( wolf::configBool( &mod, key, 7 ) == 1 );
        }
        for ( const char *key : { "e", "f", "g", "h" } )
        {
            CHECK( wolf::configBool( &mod, key, 7 ) == 0 );
        }
        CHECK( wolf::configBool( &mod, "i", 7 ) == 7 );
    }

    TEST_CASE( "configFloat needs the whole value" )
    {
        static WolfMod mod;
        wolf::configParse( &mod, "a=1.5\nb=1.5x\n", "c.ini" );
        CHECK( wolf::configFloat( &mod, "a", 9.0 ) == 1.5 );
        CHECK( wolf::configFloat( &mod, "b", 9.0 ) == 9.0 );
    }

    TEST_CASE( "configFloat takes signed decimals and exponents, and refuses hex, inf, nan and overflow" )
    {
        static WolfMod mod;
        wolf::configParse( &mod, "a=+2.5\nb=-.5\nc=1e3\nd=0x1p3\ne=inf\nf=nan\ng=1e999\nh=+-1\ni=+\nj=1,5\n", "c.ini" );
        CHECK( wolf::configFloat( &mod, "a", 9.0 ) == 2.5 );
        CHECK( wolf::configFloat( &mod, "b", 9.0 ) == -0.5 );
        CHECK( wolf::configFloat( &mod, "c", 9.0 ) == 1000.0 );
        for ( const char *key : { "d", "e", "f", "g", "h", "i", "j" } )
        {
            CAPTURE( key );
            CHECK( wolf::configFloat( &mod, key, 9.0 ) == 9.0 );
        }
    }

    TEST_CASE( "a value keeps an = and a #" )
    {
        static WolfMod mod;
        wolf::configParse( &mod, "url = a=b#c\n", "c.ini" );
        CHECK( std::string_view( wolf::configStr( &mod, "url", "" ) ) == "a=b#c" );
    }

    TEST_CASE( "a mod reads only its own keys" )
    {
        static WolfMod a;
        static WolfMod b;
        wolf::configParse( &a, "k=1\n", "a.ini" );
        wolf::configParse( &b, "j=2\n", "b.ini" );
        CHECK( wolf::configValue( &a, "j" ) == nullptr );
        CHECK( wolf::configInt( &b, "j", 0 ) == 2 );
    }

    TEST_CASE( "a missing key returns the fallback, whatever its type" )
    {
        static WolfMod mod;
        CHECK( wolf::configValue( &mod, "x" ) == nullptr );
        CHECK( wolf::configInt( &mod, "x", 5 ) == 5 );
        CHECK( wolf::configFloat( &mod, "x", 2.5 ) == 2.5 );
        CHECK( wolf::configBool( &mod, "x", 1 ) == 1 );
        CHECK( std::string_view( wolf::configStr( &mod, "x", "fb" ) ) == "fb" );
    }
}
