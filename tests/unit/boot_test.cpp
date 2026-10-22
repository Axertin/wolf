#include <windows.h>

#include <doctest/doctest.h>

#include <boot/boot.hpp>
#include <cstdint>
#include <cstring>

TEST_SUITE( "boot" )
{
    TEST_CASE( "-MODDED counts only as a whole token outside quotes, in any case" )
    {
        CHECK( wolf::hasModdedFlag( L"okami.exe -MODDED" ) );
        CHECK( wolf::hasModdedFlag( L"\"C:\\Games\\Okami\\okami.exe\" -modded -other" ) );
        CHECK( wolf::hasModdedFlag( L"okami.exe\t-MODDED" ) );
        CHECK( wolf::hasModdedFlag( L"-MODDED" ) );
        CHECK_FALSE( wolf::hasModdedFlag( L"okami.exe" ) );
        CHECK_FALSE( wolf::hasModdedFlag( L"okami.exe -MODDEDX" ) );
        CHECK_FALSE( wolf::hasModdedFlag( L"okami.exe x-MODDED" ) );
        CHECK_FALSE( wolf::hasModdedFlag( L"\"C:\\Games\\-MODDED\\okami.exe\" -MODDEDX" ) );
        CHECK_FALSE( wolf::hasModdedFlag( L"\"C:\\a b\\okami.exe -MODDED\"" ) );
        CHECK_FALSE( wolf::hasModdedFlag( L"\"-MODDED\"" ) );
        CHECK_FALSE( wolf::hasModdedFlag( L"" ) );
    }

    TEST_CASE( "the fingerprint is read from a mapped image's headers" )
    {
        const auto *base = reinterpret_cast<const uint8_t *>( GetModuleHandleW( nullptr ) );
        const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>( base );
        const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>( base + dos->e_lfanew );
        wolf::Fingerprint read{};
        REQUIRE( wolf::readFingerprint( base, &read ) );
        CHECK( read.timeDateStamp == nt->FileHeader.TimeDateStamp );
        CHECK( read.sizeOfImage == nt->OptionalHeader.SizeOfImage );
    }

    TEST_CASE( "memory that is not a PE32+ image has no fingerprint" )
    {
        alignas( 16 ) static uint8_t page[ 4096 ];
        wolf::Fingerprint read{};
        std::memset( page, 0, sizeof( page ) );
        CHECK_FALSE( wolf::readFingerprint( page, &read ) );

        page[ 0 ] = 'M';
        page[ 1 ] = 'Z';
        CHECK_FALSE( wolf::readFingerprint( page, &read ) );

        constexpr int32_t kLfanew = 0x80;
        std::memcpy( page + 0x3C, &kLfanew, sizeof( kLfanew ) );
        constexpr uint32_t kSignature = IMAGE_NT_SIGNATURE;
        std::memcpy( page + kLfanew, &kSignature, sizeof( kSignature ) );
        constexpr uint16_t kPe32 = 0x10B;
        std::memcpy( page + kLfanew + 4 + sizeof( IMAGE_FILE_HEADER ), &kPe32, sizeof( kPe32 ) );
        CHECK_FALSE( wolf::readFingerprint( page, &read ) );

        constexpr int32_t kFarLfanew = 0x7FFFFFF0;
        std::memcpy( page + 0x3C, &kFarLfanew, sizeof( kFarLfanew ) );
        CHECK_FALSE( wolf::readFingerprint( page, &read ) );
    }
}
