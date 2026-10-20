#include "Common.h"

#include <shlobj.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwctype>

namespace LipSync
{
Config g_config;
std::mutex g_configLock;

std::string GetInputDeviceSetting()
{
    std::lock_guard guard(g_configLock);
    return g_config.Device;
}

std::string GetOutputDeviceSetting()
{
    std::lock_guard guard(g_configLock);
    return g_config.OutputDevice;
}

namespace
{
SRWLOCK s_logLock = SRWLOCK_INIT;

// PKEY_Device_FriendlyName.
const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
} // namespace

std::wstring DeviceName(IMMDevice* apDevice)
{
    std::wstring name;
    ComPtr<IPropertyStore> store;
    if (SUCCEEDED(apDevice->OpenPropertyStore(STGM_READ, &store)))
    {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(store->GetValue(kFriendlyName, &value)) && value.vt == VT_LPWSTR && value.pwszVal)
            name = value.pwszVal;
        PropVariantClear(&value);
    }
    return name;
}

void Log(const char* acpFormat, ...)
{
    static std::wstring s_path;

    char line[1024];
    va_list args;
    va_start(args, acpFormat);
    vsnprintf(line, sizeof(line), acpFormat, args);
    va_end(args);

    AcquireSRWLockExclusive(&s_logLock);
    if (s_path.empty())
    {
        PWSTR pDocuments = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &pDocuments)))
        {
            std::wstring base = std::wstring(pDocuments) + L"\\My Games\\";
            CoTaskMemFree(pDocuments);

            // GOG installs keep their own folder.
            std::wstring folder = base + L"Skyrim Special Edition GOG\\SKSE";
            if (GetFileAttributesW(folder.c_str()) == INVALID_FILE_ATTRIBUTES)
                folder = base + L"Skyrim Special Edition\\SKSE";
            SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
            s_path = folder + L"\\LipSyncTogether.log";
            DeleteFileW(s_path.c_str());
        }
    }

    FILE* pFile = nullptr;
    if (!s_path.empty() && _wfopen_s(&pFile, s_path.c_str(), L"a") == 0 && pFile)
    {
        SYSTEMTIME time;
        GetLocalTime(&time);
        fprintf(pFile, "[%02d:%02d:%02d] %s\n", time.wHour, time.wMinute, time.wSecond, line);
        fclose(pFile);
    }
    ReleaseSRWLockExclusive(&s_logLock);
}

std::string ToUtf8(const wchar_t* acpText)
{
    if (!acpText || !*acpText)
        return {};
    const int cSize = WideCharToMultiByte(CP_UTF8, 0, acpText, -1, nullptr, 0, nullptr, nullptr);
    std::string out(cSize > 0 ? cSize - 1 : 0, '\0');
    if (cSize > 1)
        WideCharToMultiByte(CP_UTF8, 0, acpText, -1, out.data(), cSize, nullptr, nullptr);
    return out;
}

std::wstring ToWide(const std::string& acText)
{
    if (acText.empty())
        return {};
    const int cSize = MultiByteToWideChar(CP_UTF8, 0, acText.c_str(), -1, nullptr, 0);
    std::wstring out(cSize > 0 ? cSize - 1 : 0, L'\0');
    if (cSize > 1)
        MultiByteToWideChar(CP_UTF8, 0, acText.c_str(), -1, out.data(), cSize);
    return out;
}

std::wstring Lower(std::wstring aText)
{
    for (auto& c : aText)
        c = static_cast<wchar_t>(std::towlower(c));
    return aText;
}

bool GameHasFocus() noexcept
{
    DWORD processId = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &processId);
    return processId == GetCurrentProcessId();
}

bool KeyDown(uint32_t aVirtualKey) noexcept
{
    return aVirtualKey && GameHasFocus() && (GetAsyncKeyState(static_cast<int>(aVirtualKey)) & 0x8000) != 0;
}

bool PickDevice(IMMDeviceEnumerator* apEnumerator, EDataFlow aFlow, ERole aRole, const std::string& acWanted, const char* acpKind, bool& aListed,
                IMMDevice** appDevice, std::wstring& aName)
{
    ComPtr<IMMDeviceCollection> devices;
    UINT count = 0;
    if (SUCCEEDED(apEnumerator->EnumAudioEndpoints(aFlow, DEVICE_STATE_ACTIVE, &devices)))
        devices->GetCount(&count);

    const std::wstring cWanted = Lower(ToWide(acWanted));
    IMMDevice* pMatch = nullptr;
    for (UINT i = 0; i < count; ++i)
    {
        IMMDevice* pDevice = nullptr;
        if (FAILED(devices->Item(i, &pDevice)))
            continue;
        const std::wstring cName = DeviceName(pDevice);
        if (!aListed)
            Log("  %s %u: %s", acpKind, i + 1, ToUtf8(cName.c_str()).c_str());
        if (!pMatch && !cWanted.empty() && Lower(cName).find(cWanted) != std::wstring::npos)
        {
            pMatch = pDevice;
            aName = cName;
            continue;
        }
        pDevice->Release();
    }
    if (!aListed)
    {
        aListed = true;
        if (count == 0)
            Log("  no %s connected", acpKind);
    }

    if (pMatch)
    {
        *appDevice = pMatch;
        return true;
    }

    if (!cWanted.empty())
        Log("No %s matches '%s'; using the Windows default", acpKind, acWanted.c_str());

    if (FAILED(apEnumerator->GetDefaultAudioEndpoint(aFlow, aRole, appDevice)) && FAILED(apEnumerator->GetDefaultAudioEndpoint(aFlow, eConsole, appDevice)))
        return false;
    aName = DeviceName(*appDevice);
    return true;
}

SampleFormat DescribeFormat(const WAVEFORMATEX* apFormat) noexcept
{
    SampleFormat format;
    bool isPcm = apFormat->wFormatTag == WAVE_FORMAT_PCM;
    format.Float = apFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (apFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE && apFormat->cbSize >= 22)
    {
        const auto* pExtensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(apFormat);
        format.Float = pExtensible->SubFormat.Data1 == WAVE_FORMAT_IEEE_FLOAT;
        isPcm = pExtensible->SubFormat.Data1 == WAVE_FORMAT_PCM;
    }
    format.Channels = std::max<uint32_t>(1, apFormat->nChannels);
    format.SampleBytes = apFormat->nBlockAlign / format.Channels;
    format.Rate = apFormat->nSamplesPerSec;
    // The shared-mode format is nearly always 32-bit float; take integer PCM too.
    format.Usable = (format.Float && format.SampleBytes == 4) || (isPcm && (format.SampleBytes == 2 || format.SampleBytes == 3 || format.SampleBytes == 4));
    return format;
}

float ReadSample(const BYTE* apSample, const SampleFormat& acFormat) noexcept
{
    if (acFormat.Float)
    {
        float v;
        std::memcpy(&v, apSample, 4);
        return v;
    }
    if (acFormat.SampleBytes == 2)
    {
        int16_t v;
        std::memcpy(&v, apSample, 2);
        return v / 32768.f;
    }
    if (acFormat.SampleBytes == 3)
    {
        const int32_t v = static_cast<int32_t>((static_cast<uint32_t>(apSample[0]) << 8) | (static_cast<uint32_t>(apSample[1]) << 16) |
                                               (static_cast<uint32_t>(apSample[2]) << 24));
        return v / 2147483648.f;
    }
    int32_t v;
    std::memcpy(&v, apSample, 4);
    return v / 2147483648.f;
}
} // namespace LipSync
