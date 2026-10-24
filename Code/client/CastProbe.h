#pragma once

// Temporary cast probe output. Written to its own file next to the client log with plain file APIs, so
// the lines survive whatever stops the host's client log after a second player joins, and mirrored into
// the client log as well.

#include <TiltedCore/Filesystem.hpp>

#include <Windows.h>
#include <mutex>
#include <string>

template <class... Args> void CastProbeLog(fmt::format_string<Args...> aFormat, Args&&... aArgs) noexcept
{
    static std::mutex s_lock;
    static HANDLE s_file = INVALID_HANDLE_VALUE;
    static bool s_opened = false;

    std::string line;
    try
    {
        line = fmt::format(aFormat, std::forward<Args>(aArgs)...);
    }
    catch (...)
    {
        return;
    }

    spdlog::info("[castprobe] {}", line);

    std::lock_guard guard(s_lock);
    if (!s_opened)
    {
        s_opened = true;
        std::error_code ec;
        const auto dir = TiltedPhoques::GetPath() / "logs";
        std::filesystem::create_directories(dir, ec);
        const auto path = dir / fmt::format("castprobe_{}.log", GetCurrentProcessId());
        s_file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (s_file == INVALID_HANDLE_VALUE)
        return;

    SYSTEMTIME now{};
    GetLocalTime(&now);
    const std::string out = fmt::format("{:02}:{:02}:{:02}.{:03} {}\r\n", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, line);
    DWORD written = 0;
    WriteFile(s_file, out.data(), static_cast<DWORD>(out.size()), &written, nullptr);
}
