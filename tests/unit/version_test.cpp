#include <wolf_version.h>

#include <doctest/doctest.h>

#include <string_view>

TEST_CASE( "the version is version.txt, with a git describe suffix unless built at the release tag" )
{
    const std::string_view version = WOLF_VERSION_STRING;
    const std::string_view file = WOLF_VERSION;
    REQUIRE( !file.empty() );
    REQUIRE( version.starts_with( file ) );

    const std::string_view suffix = version.substr( file.size() );
    CHECK( ( suffix.empty() || ( suffix.size() > 1 && suffix.front() == '+' ) ) );
}
