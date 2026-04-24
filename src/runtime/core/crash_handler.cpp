#include "crash_handler.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include <psapi.h>

#include "../utilities/logger.h"
#include "console_system.h"
#include "mod_lifecycle.h"

// DbgHelp types and function pointers (loaded dynamically)
using SymInitialize_t = BOOL(WINAPI *)(HANDLE, PCSTR, BOOL);
using SymFromAddr_t = BOOL(WINAPI *)(HANDLE, DWORD64, PDWORD64, void *);
using SymGetLineFromAddr64_t = BOOL(WINAPI *)(HANDLE, DWORD64, PDWORD, void *);
using SymCleanup_t = BOOL(WINAPI *)(HANDLE);
using MiniDumpWriteDump_t = BOOL(WINAPI *)(HANDLE, DWORD, HANDLE, DWORD, void *, void *, void *);

// NT unwind functions (loaded from kernel32/ntdll)
using RtlLookupFunctionEntry_t = void *(WINAPI *)(DWORD64, PDWORD64, void *);
using RtlVirtualUnwind_t = void *(WINAPI *)(DWORD, DWORD64, DWORD64, void *, PCONTEXT, PVOID *, PDWORD64, void *);

// SYMBOL_INFO structure for DbgHelp (avoid including dbghelp.h which may not be available)
#pragma pack(push, 8)
struct WOLF_SYMBOL_INFO
{
    ULONG SizeOfStruct;
    ULONG TypeIndex;
    ULONG64 Reserved[2];
    ULONG Index;
    ULONG Size;
    ULONG64 ModBase;
    ULONG Flags;
    ULONG64 Value;
    ULONG64 Address;
    ULONG Register;
    ULONG Scope;
    ULONG Tag;
    ULONG NameLen;
    ULONG MaxNameLen;
    CHAR Name[256];
};

struct WOLF_IMAGEHLP_LINE64
{
    DWORD SizeOfStruct;
    PVOID Key;
    DWORD LineNumber;
    PCHAR FileName;
    DWORD64 Address;
};

// MINIDUMP_EXCEPTION_INFORMATION
struct WOLF_MINIDUMP_EXCEPTION_INFORMATION
{
    DWORD ThreadId;
    PEXCEPTION_POINTERS ExceptionPointers;
    BOOL ClientPointers;
};
#pragma pack(pop)

// MiniDumpWithThreadInfo = 0x00001000
static constexpr DWORD WOLF_MiniDumpWithThreadInfo = 0x00001000;

// Static crash-safe resources (pre-allocated, no runtime allocation)
static char g_CrashBuffer[65536];
static wchar_t g_CrashDirPath[MAX_PATH];
static PVOID g_VehHandle = nullptr;
static std::atomic<LONG> g_CrashHandlerActive{0};

// DbgHelp function pointers (symbol resolution only)
static HMODULE g_DbgHelp = nullptr;
static SymInitialize_t g_SymInitialize = nullptr;
static SymFromAddr_t g_SymFromAddr = nullptr;
static SymGetLineFromAddr64_t g_SymGetLineFromAddr64 = nullptr;
static SymCleanup_t g_SymCleanup = nullptr;
static MiniDumpWriteDump_t g_MiniDumpWriteDump = nullptr;
static bool g_SymInitialized = false;

// NT unwind function pointers (stack walking)
static RtlLookupFunctionEntry_t g_RtlLookupFunctionEntry = nullptr;
static RtlVirtualUnwind_t g_RtlVirtualUnwind = nullptr;

static bool isFatalException(DWORD code)
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_STACK_OVERFLOW:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_GUARD_PAGE:
        return true;
    default:
        return false;
    }
}

static const char *exceptionCodeToString(DWORD code)
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:
        return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_STACK_OVERFLOW:
        return "EXCEPTION_STACK_OVERFLOW";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_PRIV_INSTRUCTION:
        return "EXCEPTION_PRIV_INSTRUCTION";
    case EXCEPTION_IN_PAGE_ERROR:
        return "EXCEPTION_IN_PAGE_ERROR";
    case EXCEPTION_GUARD_PAGE:
        return "EXCEPTION_GUARD_PAGE";
    default:
        return "UNKNOWN_EXCEPTION";
    }
}

// Crash-safe snprintf append helper (returns new offset)
static int crashAppend(char *buf, int bufSize, int offset, const char *fmt, ...)
{
    if (offset >= bufSize - 1)
        return offset;
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buf + offset, bufSize - offset, fmt, args);
    va_end(args);
    if (written > 0)
        offset += written;
    if (offset >= bufSize)
        offset = bufSize - 1;
    return offset;
}

static int resolveFrame(char *buf, int bufSize, int offset, HANDLE process, int frameIndex, DWORD64 frameAddr)
{
    HMODULE hModule = nullptr;
    char moduleName[256] = "???";
    DWORD64 moduleBase = 0;

    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(frameAddr),
                           &hModule))
    {
        char modulePathA[MAX_PATH];
        if (GetModuleFileNameA(hModule, modulePathA, MAX_PATH))
        {
            const char *lastSlash = modulePathA;
            for (const char *p = modulePathA; *p; p++)
            {
                if (*p == '\\' || *p == '/')
                    lastSlash = p + 1;
            }
            int j = 0;
            for (const char *p = lastSlash; *p && j < 255; p++, j++)
                moduleName[j] = *p;
            moduleName[j] = '\0';
        }
        moduleBase = reinterpret_cast<DWORD64>(hModule);
    }

    DWORD64 offsetFromBase = frameAddr - moduleBase;

    bool symbolResolved = false;
    if (g_SymFromAddr && g_SymInitialized)
    {
        WOLF_SYMBOL_INFO symInfo = {};
        symInfo.SizeOfStruct = sizeof(WOLF_SYMBOL_INFO) - sizeof(symInfo.Name) + 1;
        symInfo.MaxNameLen = 255;
        DWORD64 displacement = 0;

        if (g_SymFromAddr(process, frameAddr, &displacement, &symInfo))
        {
            if (g_SymGetLineFromAddr64)
            {
                WOLF_IMAGEHLP_LINE64 lineInfo = {};
                lineInfo.SizeOfStruct = sizeof(WOLF_IMAGEHLP_LINE64);
                DWORD lineDisp = 0;
                if (g_SymGetLineFromAddr64(process, frameAddr, &lineDisp, &lineInfo))
                {
                    offset = crashAppend(buf, bufSize, offset, "  #%-2d %s!%s+0x%llX (%s:%lu)\n", frameIndex, moduleName, symInfo.Name, displacement,
                                         lineInfo.FileName, lineInfo.LineNumber);
                    symbolResolved = true;
                }
            }

            if (!symbolResolved)
            {
                offset = crashAppend(buf, bufSize, offset, "  #%-2d %s!%s+0x%llX\n", frameIndex, moduleName, symInfo.Name, displacement);
                symbolResolved = true;
            }
        }
    }

    if (!symbolResolved)
    {
        offset = crashAppend(buf, bufSize, offset, "  #%-2d %s+0x%llX\n", frameIndex, moduleName, offsetFromBase);
    }

    return offset;
}

static int formatStackTrace(char *buf, int bufSize, int offset, CONTEXT *ctx)
{
    offset = crashAppend(buf, bufSize, offset, "\nStack trace:\n");

    HANDLE process = GetCurrentProcess();

    // Use RtlVirtualUnwind to walk the faulting thread's actual stack.
    // Unlike StackWalk64 (which depends on SymFunctionTableAccess64 from dbghelp),
    // RtlLookupFunctionEntry reads .pdata directly from loaded PE modules —
    // no PDB symbols needed.
    if (g_RtlLookupFunctionEntry && g_RtlVirtualUnwind)
    {
        CONTEXT ctxCopy = *ctx;

        for (int i = 0; i < 64; i++)
        {
            DWORD64 pc = ctxCopy.Rip;
            if (pc == 0)
                break;

            offset = resolveFrame(buf, bufSize, offset, process, i, pc);

            DWORD64 imageBase = 0;
            auto *funcEntry = g_RtlLookupFunctionEntry(pc, &imageBase, nullptr);

            if (funcEntry)
            {
                // Function has .pdata unwind info — use RtlVirtualUnwind
                PVOID handlerData = nullptr;
                DWORD64 establisherFrame = 0;
                g_RtlVirtualUnwind(0 /* UNW_FLAG_NHANDLER */, imageBase, pc, funcEntry, &ctxCopy, &handlerData, &establisherFrame, nullptr);
            }
            else
            {
                // Leaf function (no .pdata entry) — return address is at [RSP]
                if (ctxCopy.Rsp == 0)
                    break;
                ctxCopy.Rip = *reinterpret_cast<DWORD64 *>(ctxCopy.Rsp);
                ctxCopy.Rsp += 8;
            }
        }
    }
    else
    {
        // Fallback: CaptureStackBackTrace (captures handler stack, not fault stack)
        void *frames[64];
        USHORT frameCount = CaptureStackBackTrace(0, 64, frames, nullptr);

        for (USHORT i = 0; i < frameCount; i++)
        {
            offset = resolveFrame(buf, bufSize, offset, process, i, reinterpret_cast<DWORD64>(frames[i]));
        }
    }

    return offset;
}

static int formatRegisters(char *buf, int bufSize, int offset, CONTEXT *ctx)
{
    offset = crashAppend(buf, bufSize, offset, "\nRegisters:\n");

    offset = crashAppend(buf, bufSize, offset, "  RAX=%016llX  RBX=%016llX\n", ctx->Rax, ctx->Rbx);
    offset = crashAppend(buf, bufSize, offset, "  RCX=%016llX  RDX=%016llX\n", ctx->Rcx, ctx->Rdx);
    offset = crashAppend(buf, bufSize, offset, "  RSP=%016llX  RBP=%016llX\n", ctx->Rsp, ctx->Rbp);
    offset = crashAppend(buf, bufSize, offset, "  RSI=%016llX  RDI=%016llX\n", ctx->Rsi, ctx->Rdi);
    offset = crashAppend(buf, bufSize, offset, "  RIP=%016llX  RFLAGS=%08lX\n", ctx->Rip, ctx->EFlags);
    offset = crashAppend(buf, bufSize, offset, "  R8 =%016llX  R9 =%016llX\n", ctx->R8, ctx->R9);
    offset = crashAppend(buf, bufSize, offset, "  R10=%016llX  R11=%016llX\n", ctx->R10, ctx->R11);
    offset = crashAppend(buf, bufSize, offset, "  R12=%016llX  R13=%016llX\n", ctx->R12, ctx->R13);
    offset = crashAppend(buf, bufSize, offset, "  R14=%016llX  R15=%016llX\n", ctx->R14, ctx->R15);

    return offset;
}

static int formatModuleList(char *buf, int bufSize, int offset)
{
    offset = crashAppend(buf, bufSize, offset, "\nLoaded modules:\n");

    HANDLE process = GetCurrentProcess();
    HMODULE modules[256];
    DWORD cbNeeded = 0;

    if (EnumProcessModules(process, modules, sizeof(modules), &cbNeeded))
    {
        DWORD moduleCount = cbNeeded / sizeof(HMODULE);
        if (moduleCount > 256)
            moduleCount = 256;

        for (DWORD i = 0; i < moduleCount; i++)
        {
            char moduleName[MAX_PATH] = {};
            MODULEINFO modInfo = {};

            GetModuleFileNameA(modules[i], moduleName, MAX_PATH);
            GetModuleInformation(process, modules[i], &modInfo, sizeof(modInfo));

            // Extract just filename
            const char *name = moduleName;
            for (const char *p = moduleName; *p; p++)
            {
                if (*p == '\\' || *p == '/')
                    name = p + 1;
            }

            offset =
                crashAppend(buf, bufSize, offset, "  %-30s 0x%p  (%lu bytes)\n", name, modInfo.lpBaseOfDll, static_cast<unsigned long>(modInfo.SizeOfImage));
        }
    }

    return offset;
}

static void writeMinidump(EXCEPTION_POINTERS *exInfo, const SYSTEMTIME &st)
{
    // Check if minidumps are enabled
    char envBuf[16] = {};
    if (GetEnvironmentVariableA("WOLF_CRASH_MINIDUMP", envBuf, sizeof(envBuf)) == 0)
        return;
    if (envBuf[0] != '1')
        return;

    if (!g_MiniDumpWriteDump)
        return;

    // Build dump file path
    wchar_t dumpPath[MAX_PATH];
    _snwprintf(dumpPath, MAX_PATH, L"%s\\crash_%04d%02d%02d_%02d%02d%02d_%03d_%lu.dmp", g_CrashDirPath, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
               st.wSecond, st.wMilliseconds, GetCurrentProcessId());

    HANDLE hFile = CreateFileW(dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return;

    WOLF_MINIDUMP_EXCEPTION_INFORMATION mei = {};
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = exInfo;
    mei.ClientPointers = FALSE;

    g_MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, WOLF_MiniDumpWithThreadInfo, &mei, nullptr, nullptr);

    CloseHandle(hFile);
}

static void __cdecl crashTestCommand(int argc, const char **argv, void *userdata)
{
    (void)argc;
    (void)argv;
    (void)userdata;
    volatile int *p = nullptr;
    *p = 42;
}

static void __cdecl lastCrashCommand(int argc, const char **argv, void *userdata)
{
    (void)argc;
    (void)argv;
    (void)userdata;

    namespace fs = std::filesystem;
    const fs::path crashDir = "logs/crashes";

    if (!fs::exists(crashDir) || fs::is_empty(crashDir))
    {
        wolfRuntimeConsolePrint("No crash reports found.");
        return;
    }

    // Find the most recent crash log by filename (they contain timestamps, so lexicographic order works)
    fs::path newestFile;
    for (const auto &entry : fs::directory_iterator(crashDir))
    {
        if (entry.path().extension() == ".log" && entry.path() > newestFile)
            newestFile = entry.path();
    }

    if (newestFile.empty())
    {
        wolfRuntimeConsolePrint("No crash reports found.");
        return;
    }

    std::ifstream file(newestFile);
    if (!file)
    {
        wolfRuntimeConsolePrint("Failed to open crash report.");
        return;
    }

    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    wolfRuntimeConsolePrint(contents.c_str());
}

static LONG CALLBACK wolfCrashHandler(EXCEPTION_POINTERS *exInfo)
{
    if (!exInfo || !exInfo->ExceptionRecord)
        return EXCEPTION_CONTINUE_SEARCH;

    DWORD code = exInfo->ExceptionRecord->ExceptionCode;

    // Only handle fatal exceptions
    if (!isFatalException(code))
        return EXCEPTION_CONTINUE_SEARCH;

    // Re-entrancy guard
    LONG expected = 0;
    if (!g_CrashHandlerActive.compare_exchange_strong(expected, 1))
        return EXCEPTION_CONTINUE_SEARCH;

    // Get timestamp
    SYSTEMTIME st;
    GetLocalTime(&st);

    // Format crash report into static buffer
    int offset = 0;
    int bufSize = sizeof(g_CrashBuffer);

    offset = crashAppend(g_CrashBuffer, bufSize, offset, "=== WOLF CRASH REPORT ===\n");
    offset = crashAppend(g_CrashBuffer, bufSize, offset, "Timestamp: %04d-%02d-%02d %02d:%02d:%02d.%03d\n", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                         st.wSecond, st.wMilliseconds);
    offset = crashAppend(g_CrashBuffer, bufSize, offset, "Exception: %s (0x%08lX)\n", exceptionCodeToString(code), code);

    // Access violation details
    if (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR)
    {
        if (exInfo->ExceptionRecord->NumberParameters >= 2)
        {
            const char *accessType = "UNKNOWN";
            switch (exInfo->ExceptionRecord->ExceptionInformation[0])
            {
            case 0:
                accessType = "READ";
                break;
            case 1:
                accessType = "WRITE";
                break;
            case 8:
                accessType = "DEP";
                break;
            }
            offset = crashAppend(g_CrashBuffer, bufSize, offset, "  Access type: %s of address 0x%p\n", accessType,
                                 reinterpret_cast<void *>(exInfo->ExceptionRecord->ExceptionInformation[1]));
        }
    }

    // Fault address with module+offset
    {
        HMODULE hModule = nullptr;
        char faultModule[256] = "???";
        DWORD64 faultAddr = reinterpret_cast<DWORD64>(exInfo->ExceptionRecord->ExceptionAddress);

        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(exInfo->ExceptionRecord->ExceptionAddress), &hModule))
        {
            char modulePathA[MAX_PATH];
            if (GetModuleFileNameA(hModule, modulePathA, MAX_PATH))
            {
                const char *lastSlash = modulePathA;
                for (const char *p = modulePathA; *p; p++)
                {
                    if (*p == '\\' || *p == '/')
                        lastSlash = p + 1;
                }
                int j = 0;
                for (const char *p = lastSlash; *p && j < 255; p++, j++)
                    faultModule[j] = *p;
                faultModule[j] = '\0';
            }
            DWORD64 moduleBase = reinterpret_cast<DWORD64>(hModule);
            offset = crashAppend(g_CrashBuffer, bufSize, offset, "  Fault address: %s+0x%llX\n", faultModule, faultAddr - moduleBase);
        }
        else
        {
            offset = crashAppend(g_CrashBuffer, bufSize, offset, "  Fault address: 0x%p\n", exInfo->ExceptionRecord->ExceptionAddress);
        }
    }

    offset = crashAppend(g_CrashBuffer, bufSize, offset, "Thread: 0x%lX\n", GetCurrentThreadId());

    // Active mod context
    WolfModId activeModId = g_CurrentModId;
    if (activeModId != 0)
    {
        offset = crashAppend(g_CrashBuffer, bufSize, offset, "Active mod ID: %u\n", activeModId);
    }
    else
    {
        offset = crashAppend(g_CrashBuffer, bufSize, offset, "Active mod: (none)\n");
    }

    // Registers first (before RtlVirtualUnwind modifies the context copy)
    if (exInfo->ContextRecord)
    {
        offset = formatRegisters(g_CrashBuffer, bufSize, offset, exInfo->ContextRecord);
        offset = formatStackTrace(g_CrashBuffer, bufSize, offset, exInfo->ContextRecord);
    }

    // Module list
    offset = formatModuleList(g_CrashBuffer, bufSize, offset);

    offset = crashAppend(g_CrashBuffer, bufSize, offset, "=========================\n");

    // Write crash report to file using raw Win32 I/O
    {
        wchar_t filePath[MAX_PATH];
        _snwprintf(filePath, MAX_PATH, L"%s\\crash_%04d%02d%02d_%02d%02d%02d_%03d_%lu.log", g_CrashDirPath, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                   st.wSecond, st.wMilliseconds, GetCurrentProcessId());

        HANDLE hFile = CreateFileW(filePath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            DWORD bytesWritten = 0;
            WriteFile(hFile, g_CrashBuffer, static_cast<DWORD>(offset), &bytesWritten, nullptr);
            FlushFileBuffers(hFile);
            CloseHandle(hFile);
        }
    }

    // Write minidump if enabled
    writeMinidump(exInfo, st);

    // Let the OS handle termination
    return EXCEPTION_CONTINUE_SEARCH;
}

namespace wolf::runtime::internal
{

void installCrashHandler()
{
    // Guard against double-registration
    if (g_VehHandle)
        return;

    // Create crash directory and pre-compute path for crash-safe use later
    std::filesystem::create_directories("logs/crashes");
    GetCurrentDirectoryW(MAX_PATH, g_CrashDirPath);
    _snwprintf(g_CrashDirPath + wcslen(g_CrashDirPath), MAX_PATH - wcslen(g_CrashDirPath), L"\\logs\\crashes");

    // Reserve stack space for stack overflow handling
    ULONG stackGuarantee = 32768; // 32KB
    SetThreadStackGuarantee(&stackGuarantee);

    // Load NT unwind functions from kernel32 (always available on x64 Windows)
    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    if (hKernel32)
    {
        g_RtlLookupFunctionEntry = reinterpret_cast<RtlLookupFunctionEntry_t>(GetProcAddress(hKernel32, "RtlLookupFunctionEntry"));
        g_RtlVirtualUnwind = reinterpret_cast<RtlVirtualUnwind_t>(GetProcAddress(hKernel32, "RtlVirtualUnwind"));
    }

    // Load DbgHelp dynamically (for symbol resolution and minidumps only)
    g_DbgHelp = LoadLibraryA("dbghelp.dll");
    if (g_DbgHelp)
    {
        g_SymInitialize = reinterpret_cast<SymInitialize_t>(GetProcAddress(g_DbgHelp, "SymInitialize"));
        g_SymFromAddr = reinterpret_cast<SymFromAddr_t>(GetProcAddress(g_DbgHelp, "SymFromAddr"));
        g_SymGetLineFromAddr64 = reinterpret_cast<SymGetLineFromAddr64_t>(GetProcAddress(g_DbgHelp, "SymGetLineFromAddr64"));
        g_SymCleanup = reinterpret_cast<SymCleanup_t>(GetProcAddress(g_DbgHelp, "SymCleanup"));
        g_MiniDumpWriteDump = reinterpret_cast<MiniDumpWriteDump_t>(GetProcAddress(g_DbgHelp, "MiniDumpWriteDump"));

        // Initialize symbol engine
        if (g_SymInitialize)
        {
            g_SymInitialized = g_SymInitialize(GetCurrentProcess(), nullptr, TRUE);
        }
    }

    // Register vectored exception handler (first handler)
    g_VehHandle = AddVectoredExceptionHandler(1, wolfCrashHandler);

    // Register console commands
    wolfRuntimeAddCommand(0, "crashtest", crashTestCommand, nullptr, "Deliberately crash to test crash handler");
    wolfRuntimeAddCommand(0, "lastcrash", lastCrashCommand, nullptr, "Display the most recent crash report");

    logInfo("[WOLF] Crash handler installed");
}

void uninstallCrashHandler()
{
    if (g_VehHandle)
    {
        RemoveVectoredExceptionHandler(g_VehHandle);
        g_VehHandle = nullptr;
    }

    if (g_SymCleanup && g_SymInitialized)
    {
        g_SymCleanup(GetCurrentProcess());
        g_SymInitialized = false;
    }

    if (g_DbgHelp)
    {
        FreeLibrary(g_DbgHelp);
        g_DbgHelp = nullptr;
    }

    g_SymInitialize = nullptr;
    g_SymFromAddr = nullptr;
    g_SymGetLineFromAddr64 = nullptr;
    g_SymCleanup = nullptr;
    g_MiniDumpWriteDump = nullptr;
    g_RtlLookupFunctionEntry = nullptr;
    g_RtlVirtualUnwind = nullptr;

    g_CrashHandlerActive.store(0);
    g_CrashDirPath[0] = L'\0';

    logInfo("[WOLF] Crash handler uninstalled");
}

} // namespace wolf::runtime::internal
