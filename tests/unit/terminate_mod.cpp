// wolf-terminate-mod.dll: a C++ mod whose init lets an exception escape a noexcept function, so its own C runtime calls terminate. crash_child.cpp loads it
// in the terminate-mod mode.
#include <wolf/wolf.hpp>

#include <stdexcept>

#ifdef _MSC_VER
#define TEST_NOINLINE __declspec( noinline )
#else
#define TEST_NOINLINE __attribute__( ( noinline ) )
#endif

namespace {

[[noreturn]] TEST_NOINLINE void raiseError()
{
    throw std::runtime_error( "escapes" );
}

// The throw sits behind a call, so the compiler cannot tell at compile time that this noexcept function throws.
// NOLINTNEXTLINE(bugprone-exception-escape)
[[noreturn]] void escape() noexcept
{
    raiseError();
}

bool init()
{
    escape();
}

} // namespace

WOLF_MOD_ENTRY( init )
