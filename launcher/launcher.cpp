#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincrypt.h>
#include <dwmapi.h>
#include <filesystem>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstdio>
#include "payload_manifest.h"
namespace fs = std::filesystem;
static HWND splash = nullptr;
static bool cancelled = false, dark = true;
static unsigned tick = 0;
static HFONT font = nullptr;
static void fail(const char *text)
{
    throw std::runtime_error(text);
}
static std::wstring env(const wchar_t *name)
{
    DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    std::wstring value(size, L'\0');
    if (size)
    {
        GetEnvironmentVariableW(name, value.data(), size);
        value.resize(size - 1);
    }
    return value;
}
static std::wstring quote(const std::wstring &value)
{
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : value)
    {
        if (ch == L'\\')
        {
            ++slashes;
            continue;
        }
        if (ch == L'\"')
            result.append(slashes * 2 + 1, L'\\');
        else
            result.append(slashes, L'\\');
        result += ch;
        slashes = 0;
    }
    result.append(slashes * 2, L'\\');
    return result + L"\"";
}
static void pump()
{
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_CLOSE)
    {
        cancelled = true;
        ShowWindow(window, SW_HIDE);
        return 0;
    }
    if (message == WM_TIMER)
    {
        ++tick;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        RECT area;
        GetClientRect(window, &area);
        HBRUSH background = CreateSolidBrush(dark ? RGB(25, 26, 31) : RGB(244, 243, 248));
        FillRect(dc, &area, background);
        DeleteObject(background);
        auto previous = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, dark ? RGB(241, 239, 248) : RGB(41, 35, 51));
        RECT text{24, 20, area.right - 24, 60};
        DrawTextW(dc, L"Starting Athanor…", -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        HBRUSH track = CreateSolidBrush(dark ? RGB(48, 51, 63) : RGB(226, 221, 236));
        HPEN pen = CreatePen(PS_NULL, 0, 0);
        auto oldPen = SelectObject(dc, pen);
        auto oldBrush = SelectObject(dc, track);
        RoundRect(dc, 24, 73, area.right - 24, 79, 6, 6);
        HBRUSH accent = CreateSolidBrush(dark ? RGB(178, 154, 255) : RGB(112, 69, 194));
        SelectObject(dc, accent);
        int width = area.right - 48, length = width / 3, position = int(tick % 70) * (width - length) / 69;
        RoundRect(dc, 24 + position, 73, 24 + position + length, 79, 6, 6);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        SelectObject(dc, previous);
        DeleteObject(track);
        DeleteObject(accent);
        DeleteObject(pen);
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
static void showSplash(HINSTANCE instance)
{
    DWORD light = 0, size = sizeof(light);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size) == ERROR_SUCCESS)
        dark = light == 0;
    WNDCLASSW cls{};
    cls.lpfnWndProc = windowProc;
    cls.hInstance = instance;
    cls.lpszClassName = L"AthanorPortableStart";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    RegisterClassW(&cls);
    font = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                       L"Segoe UI");
    splash = CreateWindowExW(0, cls.lpszClassName, L"Athanor", WS_CAPTION | WS_SYSMENU,
                             (GetSystemMetrics(SM_CXSCREEN) - 400) / 2, (GetSystemMetrics(SM_CYSCREEN) - 150) / 2, 400,
                             150, nullptr, nullptr, instance, nullptr);
    BOOL useDark = dark;
    DwmSetWindowAttribute(splash, 20, &useDark, sizeof(useDark));
    DWORD round = 2;
    DwmSetWindowAttribute(splash, 33, &round, sizeof(round));
    SetTimer(splash, 1, 40, nullptr);
    ShowWindow(splash, SW_SHOW);
    UpdateWindow(splash);
}
static HANDLE start(const fs::path &executable, const std::vector<std::wstring> &args, bool hidden, bool inherit)
{
    std::wstring command = quote(executable.wstring());
    for (const auto &arg : args)
        command += L" " + quote(arg);
    STARTUPINFOW info{};
    info.cb = sizeof(info);
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE), error = GetStdHandle(STD_ERROR_HANDLE);
    if (inherit && output && output != INVALID_HANDLE_VALUE && error && error != INVALID_HANDLE_VALUE)
    {
        info.dwFlags = STARTF_USESTDHANDLES;
        info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        info.hStdOutput = output;
        info.hStdError = error;
    }
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, inherit, hidden ? CREATE_NO_WINDOW : 0,
                        nullptr, nullptr, &info, &process))
        fail("Cannot start the bundled runtime.");
    CloseHandle(process.hThread);
    return process.hProcess;
}
static DWORD wait(HANDLE process, bool allowCancel)
{
    while (WaitForSingleObject(process, 40) == WAIT_TIMEOUT)
    {
        pump();
        if (allowCancel && cancelled)
        {
            TerminateProcess(process, 130);
            WaitForSingleObject(process, 10000);
            break;
        }
    }
    DWORD code = 1;
    GetExitCodeProcess(process, &code);
    CloseHandle(process);
    return code;
}
static void rejectLinks(const fs::path &path)
{
    for (fs::path current = path; !current.empty(); current = current.parent_path())
    {
        DWORD attributes = GetFileAttributesW(current.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            fail("The runtime location contains a redirected folder.");
        if (current == current.parent_path())
            break;
    }
}
static void removeOwned(const fs::path &path, const fs::path &root)
{
    if (path.parent_path() != root)
        fail("Invalid runtime cleanup location.");
    rejectLinks(path);
    if (!fs::exists(path))
        return;
    for (auto &entry : fs::recursive_directory_iterator(path))
        if (GetFileAttributesW(entry.path().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)
            fail("Cannot clean a redirected runtime entry.");
    fs::remove_all(path);
}
static void writeResource(HINSTANCE instance, int id, const fs::path &path)
{
    HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(id), RT_RCDATA);
    DWORD size = resource ? SizeofResource(instance, resource) : 0;
    const auto *data =
        resource ? static_cast<const unsigned char *>(LockResource(LoadResource(instance, resource))) : nullptr;
    if (!data || !size)
        fail("The download is incomplete. Download Athanor again.");
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        fail("Cannot prepare Athanor's runtime files.");
    DWORD written = 0;
    bool ok = WriteFile(file, data, size, &written, nullptr) && written == size;
    CloseHandle(file);
    if (!ok)
        fail("Not enough space to prepare Athanor.");
}
static bool valid(const fs::path &cache)
{
    if (!fs::exists(cache / L".complete"))
        return false;
    rejectLinks(cache);
    for (const auto &file : bundleFiles)
    {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        auto path = cache / file.name;
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) ||
            data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
            return false;
        ULARGE_INTEGER bytes{};
        bytes.HighPart = data.nFileSizeHigh;
        bytes.LowPart = data.nFileSizeLow;
        if (bytes.QuadPart != file.bytes)
            return false;
    }
    return true;
}
static std::wstring fileDigest(const fs::path &path)
{
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        fail("Cannot read the downloaded update.");
    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) ||
        !CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash))
    {
        CloseHandle(file);
        if (provider)
            CryptReleaseContext(provider, 0);
        fail("Cannot verify the downloaded update.");
    }
    BYTE buffer[65536], digest[32];
    DWORD read = 0, size = sizeof(digest);
    bool ok = true;
    while (ok)
    {
        if (!ReadFile(file, buffer, sizeof(buffer), &read, nullptr))
        {
            ok = false;
            break;
        }
        if (!read)
            break;
        ok = CryptHashData(hash, buffer, read, 0);
    }
    ok = ok && CryptGetHashParam(hash, HP_HASHVAL, digest, &size, 0);
    CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    CloseHandle(file);
    if (!ok)
        fail("Cannot verify the downloaded update.");
    std::wstring text;
    for (auto value : digest)
    {
        text += L"0123456789abcdef"[value >> 4];
        text += L"0123456789abcdef"[value & 15];
    }
    return text;
}
static void waitForExit(const std::wstring &value)
{
    size_t end = 0;
    auto pid = std::stoul(value, &end);
    if (end != value.size() || !pid || pid > MAXDWORD || pid == GetCurrentProcessId())
        fail("Invalid update process.");
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, DWORD(pid));
    if (!process)
    {
        if (GetLastError() == ERROR_INVALID_PARAMETER)
            return;
        fail("Cannot wait for Athanor to close.");
    }
    DWORD result = WaitForSingleObject(process, 120000);
    CloseHandle(process);
    if (result != WAIT_OBJECT_0)
        fail("Close all Athanor windows and try the update again.");
}
static int applyUpdate(const fs::path &self, const std::vector<std::wstring> &args)
{
    if (args.size() != 5 || args[4].size() != 64 || fileDigest(self) != args[4])
        fail("The downloaded update could not be verified.");
    fs::path target = fs::absolute(args[1]).lexically_normal();
    rejectLinks(target);
    if (target.extension() != L".exe" || !fs::is_regular_file(target) || fs::equivalent(target, self))
        fail("Cannot locate the portable app to update.");
    if (wait(start(self, {L"--cli"}, true, false), false))
        fail("The downloaded app could not start. Your portable app is unchanged.");
    waitForExit(args[2]);
    waitForExit(args[3]);
    GUID id{};
    CoCreateGuid(&id);
    wchar_t guid[40]{};
    StringFromGUID2(id, guid, 40);
    fs::path replacement = target.parent_path() / (L".athanor-update-" + std::wstring(guid) + L".exe");
    fs::path backup = replacement;
    backup += L".bak";
    if (!CopyFileW(self.c_str(), replacement.c_str(), TRUE))
        fail("The portable app folder is not writable.");
    if (!CopyFileW(target.c_str(), backup.c_str(), TRUE))
    {
        fs::remove(replacement);
        fail("Cannot save a backup of the portable app. Your app is unchanged.");
    }
    bool replaced = false;
    for (int attempt = 0; attempt < 50; ++attempt)
    {
        if (MoveFileExW(replacement.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            replaced = true;
            break;
        }
        Sleep(100);
    }
    if (!replaced)
    {
        fs::remove(replacement);
        fs::remove(backup);
        fail("Windows is still using the portable app. Close its other windows and try again.");
    }
    try
    {
        HANDLE app =
            start(target,
                  env(L"ATHANOR_TEST").empty() ? std::vector<std::wstring>{} : std::vector<std::wstring>{L"--cli"},
                  false, false);
        CloseHandle(app);
        std::error_code cleanupError;
        fs::remove(backup, cleanupError);
    }
    catch (...)
    {
        MoveFileExW(backup.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        throw;
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, wchar_t *, int)
{
    fs::path staging, runtimeRoot;
    HANDLE mutex = nullptr, usage = INVALID_HANDLE_VALUE;
    try
    {
        int count = 0;
        wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &count);
        std::vector<std::wstring> args;
        bool quiet = false;
        for (int i = 1; i < count; i++)
        {
            args.emplace_back(argv[i]);
            if (args.back() == L"--version" || args.back() == L"--help" || args.back() == L"--cli" ||
                args.back() == L"--worker" || args.back() == L"--self-test")
                quiet = true;
        }
        LocalFree(argv);
        std::wstring self(32768, L'\0');
        DWORD length = GetModuleFileNameW(nullptr, self.data(), DWORD(self.size()));
        if (!length || length == self.size())
            fail("Cannot locate the downloaded executable.");
        self.resize(length);
        if (!args.empty() && args[0] == L"--apply-update")
            return applyUpdate(self, args);
        fs::path root = env(L"ATHANOR_PORTABLE_CACHE");
        if (root.empty())
        {
            PWSTR local = nullptr;
            if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)))
                fail("Cannot locate your application data folder.");
            root = fs::path(local) / L"Athanor";
            CoTaskMemFree(local);
        }
        root = fs::absolute(root).lexically_normal();
        rejectLinks(root);
        fs::create_directories(root);
        runtimeRoot = root / L"runtime";
        fs::create_directories(runtimeRoot);
        rejectLinks(runtimeRoot);
        fs::path cache = runtimeRoot / payloadHash;
        std::wstring mutexName = L"Local\\AthanorPrepare-" + std::wstring(payloadHash);
        mutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
        if (!mutex)
            fail("Cannot prepare the runtime lock.");
        bool locked = false;
        for (int i = 0; i < 3000; i++)
        {
            DWORD state = WaitForSingleObject(mutex, 40);
            pump();
            if (state == WAIT_OBJECT_0 || state == WAIT_ABANDONED)
            {
                locked = true;
                break;
            }
            if (state == WAIT_FAILED)
                fail("Cannot access the runtime lock.");
        }
        if (!locked)
            fail("Another Athanor launch is still preparing its runtime. Try again shortly.");
        if (!valid(cache))
        {
            if (!quiet)
                showSplash(instance);
            if (fs::exists(cache))
            {
                HANDLE repair = CreateFileW((cache / L".in-use").c_str(), GENERIC_READ, 0, nullptr, OPEN_ALWAYS,
                                            FILE_ATTRIBUTE_NORMAL, nullptr);
                if (repair == INVALID_HANDLE_VALUE)
                    fail("Close Athanor before repairing its cached runtime.");
                CloseHandle(repair);
                removeOwned(cache, runtimeRoot);
            }
            GUID id{};
            CoCreateGuid(&id);
            wchar_t guid[40]{};
            StringFromGUID2(id, guid, 40);
            staging = runtimeRoot / (L".prepare-" + std::wstring(guid));
            fs::create_directory(staging);
            writeResource(instance, 101, staging / L"payload.7z");
            writeResource(instance, 102, staging / L"7z.exe");
            writeResource(instance, 103, staging / L"7z.dll");
            DWORD code =
                wait(start(staging / L"7z.exe",
                           {L"x", L"-y", L"-bd", L"-o" + staging.wstring(), (staging / L"payload.7z").wstring()}, true,
                           false),
                     true);
            if (cancelled)
            {
                removeOwned(staging, runtimeRoot);
                staging.clear();
                ReleaseMutex(mutex);
                CloseHandle(mutex);
                return 130;
            }
            if (code)
                fail("Cannot unpack Athanor's runtime. Check free disk space or download the file again.");
            fs::remove(staging / L"payload.7z");
            fs::remove(staging / L"7z.exe");
            fs::remove(staging / L"7z.dll");
            HANDLE marker = CreateFileW((staging / L".complete").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
            if (marker == INVALID_HANDLE_VALUE)
                fail("Cannot finish preparing Athanor.");
            CloseHandle(marker);
            if (!valid(staging))
                fail("The bundled runtime is incomplete. Download Athanor again.");
            fs::rename(staging, cache);
            staging.clear();
        }
        usage = CreateFileW((cache / L".in-use").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (usage == INVALID_HANDLE_VALUE)
            fail("Cannot access the cached runtime.");
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        mutex = nullptr;
        fs::path settings = root / L"settings";
        fs::create_directories(settings);
        SetEnvironmentVariableW(L"ATHANOR_SETTINGS_DIR", settings.c_str());
        SetEnvironmentVariableW(L"ATHANOR_LAUNCHER_PATH", self.c_str());
        SetEnvironmentVariableW(L"ATHANOR_LAUNCHER_PID", std::to_wstring(GetCurrentProcessId()).c_str());
        HANDLE app = start(cache / L"Athanor" / L"Athanor.exe", args, false, true);
        if (splash)
        {
            DestroyWindow(splash);
            splash = nullptr;
        }
        DWORD code = wait(app, false);
        CloseHandle(usage);
        usage = INVALID_HANDLE_VALUE;
        if (font)
            DeleteObject(font);
        return int(code);
    }
    catch (const std::exception &error)
    {
        if (splash)
            DestroyWindow(splash);
        if (usage != INVALID_HANDLE_VALUE)
            CloseHandle(usage);
        if (!staging.empty())
        {
            try
            {
                removeOwned(staging, runtimeRoot);
            }
            catch (...)
            {
            }
        }
        if (mutex)
        {
            ReleaseMutex(mutex);
            CloseHandle(mutex);
        }
        std::string message = error.what();
        int n = MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, nullptr, 0);
        std::wstring text(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, text.data(), n);
        if (env(L"ATHANOR_TEST").empty())
            MessageBoxW(nullptr, text.c_str(), L"Athanor", MB_OK | MB_ICONERROR);
        else
        {
            HANDLE output = GetStdHandle(STD_ERROR_HANDLE);
            DWORD written;
            WriteFile(output, message.data(), DWORD(message.size()), &written, nullptr);
        }
        return 1;
    }
}
