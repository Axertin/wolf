#include "harness.hpp"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include <doctest/doctest.h>

#include <atomic>
#include <thread>
#include <vector>

namespace {

using CreateFn = HRESULT(WINAPI *)(HINSTANCE, DWORD, REFIID, void **, IUnknown *);
using ClassObjectFn = HRESULT(WINAPI *)(REFCLSID, REFIID, void **);
using CanUnloadFn = HRESULT(WINAPI *)();
using DataFormatFn = const DIDATAFORMAT *(WINAPI *)();
using FakeMainCreateFn = CreateFn (*)();

HMODULE proxy()
{
    return GetModuleHandleW((harness::binDir() + L"dinput8.dll").c_str());
}

template <typename Fn>
Fn proxyExport(const char *name)
{
    return reinterpret_cast<Fn>(GetProcAddress(proxy(), name));
}

template <typename Fn>
Fn systemExport(const char *name)
{
    HMODULE system = LoadLibraryW(harness::systemDinput8Path().c_str());
    return reinterpret_cast<Fn>(GetProcAddress(system, name));
}

} // namespace

TEST_CASE("loading main.dll maps the proxy from the game directory and binds its import to it")
{
    const harness::FakeMain &fake = harness::fakeMain();
    REQUIRE(fake.main != nullptr);
    CHECK_FALSE(fake.proxyLoadedBefore);
    REQUIRE(proxy() != nullptr);

    const auto boundCreate = reinterpret_cast<FakeMainCreateFn>(GetProcAddress(fake.main, "FakeMain_DirectInput8Create"));
    REQUIRE(boundCreate != nullptr);
    CHECK(reinterpret_cast<void *>(boundCreate()) == reinterpret_cast<void *>(GetProcAddress(proxy(), "DirectInput8Create")));
}

TEST_CASE("main.dll carries the real main.dll's fingerprint")
{
    const harness::FakeMain &fake = harness::fakeMain();
    REQUIRE(fake.main != nullptr);
    const auto *base = reinterpret_cast<const unsigned char *>(fake.main);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
    CHECK(nt->FileHeader.TimeDateStamp == harness::kMainTimeDateStamp);
    CHECK(nt->OptionalHeader.SizeOfImage == harness::kMainSizeOfImage);
}

TEST_CASE("the proxy loads the system dinput8.dll only on the first call")
{
    const harness::FakeMain &fake = harness::fakeMain();
    REQUIRE(fake.main != nullptr);
    CHECK_FALSE(fake.systemLoadedOnLoad);
}

TEST_CASE("DirectInput8Create called from several threads at once forwards every call")
{
    REQUIRE(harness::fakeMain().main != nullptr);
    const auto create = proxyExport<CreateFn>("DirectInput8Create");
    REQUIRE(create != nullptr);

    constexpr int kThreads = 8;
    std::atomic<int> ready{0};
    std::atomic<int> created{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i)
    {
        threads.emplace_back([&] {
            ready.fetch_add(1);
            while (ready.load() < kThreads)
            {
            }
            IDirectInput8W *input = nullptr;
            if (create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, reinterpret_cast<void **>(&input), nullptr) == DI_OK &&
                input != nullptr)
            {
                input->Release();
                created.fetch_add(1);
            }
        });
    }
    for (std::thread &thread : threads)
    {
        thread.join();
    }
    CHECK(created.load() == kThreads);
    CHECK(GetModuleHandleW(harness::systemDinput8Path().c_str()) != nullptr);
}

TEST_CASE("DirectInput8Create forwards the wide and the ANSI interface")
{
    REQUIRE(harness::fakeMain().main != nullptr);
    const auto create = proxyExport<CreateFn>("DirectInput8Create");
    REQUIRE(create != nullptr);

    IDirectInput8W *wide = nullptr;
    REQUIRE(create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, reinterpret_cast<void **>(&wide), nullptr) == DI_OK);
    REQUIRE(wide != nullptr);
    wide->Release();

    IDirectInput8A *ansi = nullptr;
    REQUIRE(create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8A, reinterpret_cast<void **>(&ansi), nullptr) == DI_OK);
    REQUIRE(ansi != nullptr);
    ansi->Release();
}

TEST_CASE("DirectInput8Create passes the system's error results through unchanged")
{
    REQUIRE(harness::fakeMain().main != nullptr);
    const auto create = proxyExport<CreateFn>("DirectInput8Create");
    const auto systemCreate = systemExport<CreateFn>("DirectInput8Create");
    REQUIRE(create != nullptr);
    REQUIRE(systemCreate != nullptr);
    const HINSTANCE instance = GetModuleHandleW(nullptr);

    SUBCASE("null out pointer")
    {
        const HRESULT hr = create(instance, DIRECTINPUT_VERSION, IID_IDirectInput8W, nullptr, nullptr);
        CHECK(FAILED(hr));
        CHECK(hr == systemCreate(instance, DIRECTINPUT_VERSION, IID_IDirectInput8W, nullptr, nullptr));
    }
    SUBCASE("version older than DirectInput 8")
    {
        IDirectInput8W *input = nullptr;
        IDirectInput8W *systemInput = nullptr;
        const HRESULT hr = create(instance, 0x0700, IID_IDirectInput8W, reinterpret_cast<void **>(&input), nullptr);
        CHECK(FAILED(hr));
        CHECK(input == nullptr);
        CHECK(hr == systemCreate(instance, 0x0700, IID_IDirectInput8W, reinterpret_cast<void **>(&systemInput), nullptr));
        // Release anything a wrong forward handed out, so a failure here does
        // not leak into the next test case.
        for (IDirectInput8W *created : {input, systemInput})
        {
            if (created != nullptr)
            {
                created->Release();
            }
        }
    }
}

TEST_CASE("the COM exports forward to the system dinput8.dll")
{
    REQUIRE(harness::fakeMain().main != nullptr);

    const auto getClassObject = proxyExport<ClassObjectFn>("DllGetClassObject");
    REQUIRE(getClassObject != nullptr);
    IClassFactory *factory = nullptr;
    REQUIRE(getClassObject(CLSID_DirectInput8, IID_IClassFactory, reinterpret_cast<void **>(&factory)) == S_OK);
    REQUIRE(factory != nullptr);
    IDirectInput8W *input = nullptr;
    REQUIRE(factory->CreateInstance(nullptr, IID_IDirectInput8W, reinterpret_cast<void **>(&input)) == S_OK);
    REQUIRE(input != nullptr);
    CHECK(input->Initialize(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION) == DI_OK);
    input->Release();
    factory->Release();

    const auto canUnload = proxyExport<CanUnloadFn>("DllCanUnloadNow");
    REQUIRE(canUnload != nullptr);
    const HRESULT hr = canUnload();
    CHECK((hr == S_OK || hr == S_FALSE));

    // Registering would write to the registry of the machine running the
    // tests, so these two are only resolved, not called.
    CHECK(GetProcAddress(proxy(), "DllRegisterServer") != nullptr);
    CHECK(GetProcAddress(proxy(), "DllUnregisterServer") != nullptr);
}

TEST_CASE("GetdfDIJoystick returns what the system dinput8.dll returns")
{
    REQUIRE(harness::fakeMain().main != nullptr);
    const auto dataFormat = proxyExport<DataFormatFn>("GetdfDIJoystick");
    REQUIRE(dataFormat != nullptr);

    // Windows exports GetdfDIJoystick; Wine's builtin dinput8.dll does not,
    // and then the proxy returns null.
    const auto systemDataFormat = systemExport<DataFormatFn>("GetdfDIJoystick");
    if (systemDataFormat == nullptr)
    {
        CHECK(dataFormat() == nullptr);
        return;
    }
    const DIDATAFORMAT *format = dataFormat();
    REQUIRE(format != nullptr);
    CHECK(format == systemDataFormat());
    CHECK(format->dwSize == sizeof(DIDATAFORMAT));
}
