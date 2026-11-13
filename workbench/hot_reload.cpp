// SPDX-License-Identifier: MIT
#include "hot_reload.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <delayimp.h>

#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace
{
constexpr const char* kDllName = "dragdoll.dll"; // As named in the viewer's import table

fs::path g_source;                 // bin/dragdoll.dll, written by the build
fs::path g_shadow;                 // The copy currently loaded (or about to be)
fs::file_time_type g_loadedTime{}; // Time stamp of the build the shadow was copied from
fs::file_time_type g_pendingTime{};
int g_generation = 0;

fs::path exeDirectory()
{
    wchar_t buffer[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return fs::path(buffer).parent_path();
}

bool makeShadow(std::string& error)
{
    std::error_code ec;
    const fs::path dir = exeDirectory() / "hot";
    fs::create_directories(dir, ec);
    const fs::file_time_type time = fs::last_write_time(g_source, ec);
    if (ec)
    {
        error = "cannot read " + g_source.string() + ": " + ec.message();
        return false;
    }
    const fs::path shadow = dir / ("dragdoll_" + std::to_string(GetCurrentProcessId()) + "_" +
                                   std::to_string(g_generation + 1) + ".dll");
    if (!fs::copy_file(g_source, shadow, fs::copy_options::overwrite_existing, ec))
    {
        error = "cannot copy the library: " + ec.message(); // e.g. the linker is still writing it: retried later
        return false;
    }
    const fs::path previous = g_shadow;
    g_shadow = shadow;
    g_loadedTime = time;
    g_pendingTime = time;
    ++g_generation;
    if (!previous.empty())
    {
        fs::remove(previous, ec); // Unloaded by now; ignore failures
    }
    return true;
}

FARPROC WINAPI delayLoadHook(unsigned notify, PDelayLoadInfo info)
{
    if (notify == dliNotePreLoadLibrary && _stricmp(info->szDll, kDllName) == 0 && !g_shadow.empty())
    {
        return reinterpret_cast<FARPROC>(LoadLibraryW(g_shadow.c_str()));
    }
    return nullptr; // Default behaviour
}
} // namespace

extern "C" const PfnDliHook __pfnDliNotifyHook2 = delayLoadHook;

namespace hot
{
bool start(std::string& error)
{
    g_source = exeDirectory() / kDllName;
    // Leftover copies from earlier runs (ones still loaded by another viewer just fail to delete).
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(exeDirectory() / "hot", ec))
    {
        std::error_code ignored;
        fs::remove(entry.path(), ignored);
    }
    return makeShadow(error);
}

bool changed()
{
    if (g_generation == 0)
    {
        return false;
    }
    std::error_code ec;
    const fs::file_time_type time = fs::last_write_time(g_source, ec);
    if (ec || time == g_loadedTime)
    {
        return false;
    }
    const bool settled = time == g_pendingTime; // Same new time stamp as at the previous poll
    g_pendingTime = time;
    return settled;
}

bool swap(std::string& error)
{
    if (!__FUnloadDelayLoadedDLL2(kDllName))
    {
        error = "the old library could not be unloaded";
        return false;
    }
    return makeShadow(error); // On failure the old copy is still on disk and loads again on the next call
}

int generation()
{
    return g_generation;
}
} // namespace hot
