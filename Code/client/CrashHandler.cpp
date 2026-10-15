#include <BranchInfo.h>
#include <TiltedCore/Filesystem.hpp>
#include "CrashHandler.h"
#include <DbgHelp.h>
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <strsafe.h>

using time_point = std::chrono::system_clock::time_point;

std::string SerializeTimePoint(const time_point& time, const std::string& format)
{
    std::time_t tt = std::chrono::system_clock::to_time_t(time);
    std::tm tm = *std::gmtime(&tt); // GMT (UTC)
    // std::tm tm = *std::localtime(&tt); //Locale time-zone, usually UTC by default.
    std::stringstream ss;
    ss << std::put_time(&tm, format.c_str());
    return ss.str();
}

// "<module>+0x<offset>" for an address that lies inside a loaded module,
// "unmapped" otherwise; lets every logged address be tied back to an image.
void FormatModuleOffset(uintptr_t apAddress, char (&aBuf)[MAX_PATH + 48])
{
    HMODULE hMod = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(apAddress), &hMod) &&
        hMod)
    {
        char modName[MAX_PATH] = {};
        if (GetModuleFileNameA(hMod, modName, MAX_PATH))
        {
            const char* base = strrchr(modName, '\\');
            sprintf_s(aBuf, sizeof(aBuf), "%s+0x%llx", base ? base + 1 : modName,
                      apAddress - reinterpret_cast<uintptr_t>(hMod));
            return;
        }
    }
    sprintf_s(aBuf, sizeof(aBuf), "unmapped 0x%llx", apAddress);
}

// SEH guard kept in its own function: __try cannot live in a function that
// needs C++ object unwinding.
static bool TryReadStackSlot(const uintptr_t* apSlot, uintptr_t& aValue)
{
    __try
    {
        aValue = *apSlot;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

LONG WINAPI VectoredExceptionHandler(PEXCEPTION_POINTERS pExceptionInfo)
{
    static int alreadyCrashed = 0;
    auto retval = EXCEPTION_CONTINUE_SEARCH;

    // Serialize 
    static std::mutex singleThreaded;
    const std::lock_guard lock{singleThreaded};

    // Check for severe, not continuable and not software-originated exception
    if (pExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        alreadyCrashed++ == 0)
    {
        spdlog::critical (__FUNCTION__ ": crash occurred!"); 
        
        spdlog::error(__FUNCTION__ ": exception code is {:x}, at address {}, flags {:x} ",
                      pExceptionInfo->ExceptionRecord->ExceptionCode,
                      pExceptionInfo->ExceptionRecord->ExceptionAddress,
                      pExceptionInfo->ExceptionRecord->ExceptionFlags);

        {
            // Name the module the fault happened in and the address it tried to touch,
            // so a crash can be attributed without the (large) minidump.
            char where[MAX_PATH + 48];
            FormatModuleOffset(reinterpret_cast<uintptr_t>(pExceptionInfo->ExceptionRecord->ExceptionAddress), where);
            const auto* rec = pExceptionInfo->ExceptionRecord;
            const char* access = rec->NumberParameters >= 2 ? (rec->ExceptionInformation[0] == 1 ? "write" : rec->ExceptionInformation[0] == 8 ? "execute" : "read") : "?";
            spdlog::error(__FUNCTION__ ": faulting module {}, {} of 0x{:x}", where, access,
                          rec->NumberParameters >= 2 ? rec->ExceptionInformation[1] : 0);

            // Heuristic stack scan: values on the stack that point into a loaded
            // module are mostly return addresses. Good enough to see who called in.
            const auto* pStack = reinterpret_cast<const uintptr_t*>(pExceptionInfo->ContextRecord->Rsp);
            int logged = 0;
            for (int i = 0; i < 256 && logged < 24; ++i)
            {
                uintptr_t value = 0;
                if (!TryReadStackSlot(pStack + i, value))
                    break;

                HMODULE hMod = nullptr;
                if (value < 0x10000 || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                                           reinterpret_cast<LPCWSTR>(value), &hMod))
                    continue;

                char frame[MAX_PATH + 48];
                FormatModuleOffset(value, frame);
                spdlog::error(__FUNCTION__ ":   stack[rsp+0x{:x}] {}", i * 8, frame);
                ++logged;
            }
        }

#if (IS_MASTER)
        volatile static bool bMiniDump = false;
#else
        volatile static bool bMiniDump = true;
#endif
        if (bMiniDump)
        {
            HANDLE hDumpFile = NULL;
            try
            {
                MINIDUMP_EXCEPTION_INFORMATION M;
                char dumpPath[MAX_PATH];

                M.ThreadId = GetCurrentThreadId();
                M.ExceptionPointers = pExceptionInfo;
                M.ClientPointers = 0;

                std::ostringstream oss;
                oss << "crash_" << SerializeTimePoint(std::chrono::system_clock::now(), "UTC_%Y-%m-%d_%H-%M-%S")
                    << ".dmp";

                // Next to the logs (the launcher's folder), never in the game's
                // install folder, and only the latest one is kept.
                (void)dumpPath;
                auto subPath = TiltedPhoques::GetPath() / "logs";
                std::error_code ec;
                std::filesystem::create_directories(subPath, ec);

                CrashHandler::RemovePreviousDump(subPath);

                subPath /= oss.str();

                hDumpFile = CreateFileA(subPath.string().c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, NULL);

                // baseline settings from https://stackoverflow.com/a/63123214/5273909
                auto dumpSettings = MiniDumpWithDataSegs | MiniDumpWithProcessThreadData | MiniDumpWithHandleData |
                                    MiniDumpWithThreadInfo |
                                    /*
                                    //MiniDumpWithPrivateReadWriteMemory | // this one gens bad dump
                                    MiniDumpWithUnloadedModules |
                                    MiniDumpWithFullMemoryInfo |
                                    MiniDumpWithTokenInformation |
                                    MiniDumpWithPrivateWriteCopyMemory |
                                    */
                                    0;

                MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDumpFile, (MINIDUMP_TYPE)dumpSettings,
                                  (pExceptionInfo) ? &M : NULL, NULL, NULL);
            }
            catch (...) // Mini-dump is best effort only.
            {
            }

            if (!hDumpFile)
                spdlog::critical(__FUNCTION__ ": coredump may have failed.");
            else
            {
                CloseHandle(hDumpFile);
                spdlog::critical(__FUNCTION__ ": coredump created -> flush logs.");
            }
        }

        // Something in STR breaks top-level unhandled exception filters.
        // The Win API for them is pretty clunky (non-atomic, not chainable), 
        // but they can do some important things. If someone actually set one
        // they probably meant it; make sure it actually runs.
        // This will make more CrashLogger mods work with STR.

        // Get the current unhandled exception filter. If it has changed
        // from when STR started up, invoke it here.
        LPTOP_LEVEL_EXCEPTION_FILTER pCurrentUnhandledExceptionFilter = SetUnhandledExceptionFilter(CrashHandler::GetOriginalUnhandledExceptionFilter());
        SetUnhandledExceptionFilter(pCurrentUnhandledExceptionFilter);
        if (pCurrentUnhandledExceptionFilter != CrashHandler::GetOriginalUnhandledExceptionFilter())
        {
            spdlog::critical(__FUNCTION__ ": UnhandledExceptionFilter() workaround triggered.");

            singleThreaded.unlock();        // Might reenter, but is safe at this point.
            if ((*pCurrentUnhandledExceptionFilter)(pExceptionInfo) == EXCEPTION_CONTINUE_EXECUTION)
                retval = EXCEPTION_CONTINUE_EXECUTION;
            singleThreaded.lock();
        }

        spdlog::shutdown();
    }
    return retval;
}

LPTOP_LEVEL_EXCEPTION_FILTER CrashHandler::m_pUnhandled;
CrashHandler::CrashHandler()
{
    // Record the original (or as close as we can get) top-level unhandled exception handler.
    // We grab this so we can see if it is changed, presumably by a mod or even graphics drivers.
    // Something in STR breaks unhandled exception handling, so we'll fake it if necessary.
    // This is the only way to get the current setting, but the race is small.
    m_pUnhandled = SetUnhandledExceptionFilter(NULL);
    SetUnhandledExceptionFilter(m_pUnhandled);

    m_handler = AddVectoredExceptionHandler(1, &VectoredExceptionHandler);
}

CrashHandler::~CrashHandler()
{
}

void CrashHandler::RemovePreviousDump(std::filesystem::path path)
{
    for (auto& entry : std::filesystem::directory_iterator(path))
    {
        const auto name = entry.path().filename().string();
        if (name.rfind("crash_", 0) == 0 && entry.path().extension() == ".dmp")
        {
            DeleteFileA(entry.path().string().c_str());
        }
    }
}
