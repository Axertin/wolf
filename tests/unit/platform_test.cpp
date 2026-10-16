#include <windows.h>

#include <doctest/doctest.h>

TEST_CASE("the unit tests run as 64-bit Windows code")
{
    CHECK(sizeof(void *) == 8);
    CHECK(GetModuleHandleW(L"kernel32.dll") != nullptr);
}
