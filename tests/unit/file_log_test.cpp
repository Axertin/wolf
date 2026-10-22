#include <windows.h>

#include <doctest/doctest.h>

#include <log/file_log.hpp>
#include <log/log.hpp>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "support.hpp"

namespace {

std::vector<std::string> fileLines( const std::wstring &dir )
{
    const std::string text = readFile( dir + L"logs\\wolf.log" );
    std::vector<std::string> lines;
    size_t start = 0;
    for ( size_t end = text.find( "\r\n" ); end != std::string::npos; end = text.find( "\r\n", start ) )
    {
        lines.push_back( text.substr( start, end - start ) );
        start = end + 2;
    }
    CHECK( start == text.size() ); // every line ends in \r\n
    return lines;
}

} // namespace

TEST_SUITE( "file_log" )
{
    TEST_CASE( "lines written from several threads at once each land whole in logs/wolf.log" )
    {
        const std::wstring dir = freshTempDir();
        REQUIRE( wolf::startFileLog( dir.c_str() ) );
        constexpr int kThreads = 8;
        constexpr int kLines = 4000;
        raceThreads( kThreads, []( int t ) {
            for ( int n = 0; n < kLines; ++n )
            {
                wolf::writeLog( nullptr, WOLF_LOG_INFO, "line t=%d n=%d", t, n );
            }
        } );
        logs::start();

        const std::vector<std::string> lines = fileLines( dir );
        CHECK( lines.size() == size_t{ kThreads } * kLines );
        const std::regex shape( R"(\d\d:\d\d:\d\d\.\d{3} \d+ \[wolf\] INFO line t=(\d+) n=(\d+))" );
        std::set<std::pair<int, int>> seen;
        for ( const std::string &line : lines )
        {
            std::smatch match;
            REQUIRE( std::regex_match( line, match, shape ) );
            CHECK( seen.insert( { std::stoi( match[ 1 ] ), std::stoi( match[ 2 ] ) } ).second );
        }
        CHECK( seen.size() == size_t{ kThreads } * kLines );
    }

    TEST_CASE( "each level is named in the prefix" )
    {
        const std::wstring dir = freshTempDir();
        REQUIRE( wolf::startFileLog( dir.c_str() ) );
        wolf::writeLog( nullptr, WOLF_LOG_DEBUG, "msg" );
        wolf::writeLog( nullptr, WOLF_LOG_INFO, "msg" );
        wolf::writeLog( nullptr, WOLF_LOG_WARN, "msg" );
        wolf::writeLog( nullptr, WOLF_LOG_ERROR, "msg" );
        logs::start();

        const std::vector<std::string> lines = fileLines( dir );
        REQUIRE( lines.size() == 4 );
        CHECK( lines[ 0 ].ends_with( " [wolf] DEBUG msg" ) );
        CHECK( lines[ 1 ].ends_with( " [wolf] INFO msg" ) );
        CHECK( lines[ 2 ].ends_with( " [wolf] WARN msg" ) );
        CHECK( lines[ 3 ].ends_with( " [wolf] ERROR msg" ) );
    }

    TEST_CASE( "starting the file log again truncates the file" )
    {
        const std::wstring dir = freshTempDir();
        REQUIRE( wolf::startFileLog( dir.c_str() ) );
        wolf::writeLog( nullptr, WOLF_LOG_INFO, "first" );
        REQUIRE( wolf::startFileLog( dir.c_str() ) );
        wolf::writeLog( nullptr, WOLF_LOG_INFO, "second" );
        logs::start();

        const std::vector<std::string> lines = fileLines( dir );
        REQUIRE( lines.size() == 1 );
        CHECK( lines[ 0 ].ends_with( " second" ) );
    }

    TEST_CASE( "a log file that cannot be opened leaves the sink as it was" )
    {
        // A file named logs stands where the directory would go.
        const std::wstring dir = freshTempDir();
        const HANDLE blocker = CreateFileW( ( dir + L"logs" ).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr );
        REQUIRE( blocker != INVALID_HANDLE_VALUE );
        CloseHandle( blocker );
        logs::start();
        CHECK_FALSE( wolf::startFileLog( dir.c_str() ) );
        wolf::writeLog( nullptr, WOLF_LOG_INFO, "still recorded" );
        CHECK( logs::count( "still recorded" ) == 1 );
    }
}
