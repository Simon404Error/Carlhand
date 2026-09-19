// FSDTrainerLoader.cpp - 一体化启动器：查找/启动游戏 → 注入（内含 DLL 资源）→ 自动部署配置
// 构建产物：FSDTrainer.exe（单文件，无需 Xenos）
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdarg>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shell32.lib")

#define RES_DLL  101
#define RES_INI  102
#define RES_UE4SS_DLL 103
#define RES_UE4SS_INI 104

static const wchar_t* kTargetExe = L"FSD-Win64-Shipping.exe";
static const wchar_t* kSteamUrl = L"steam://rungameid/548430";
static HANDLE g_log = INVALID_HANDLE_VALUE;

static void LogOpen(const std::wstring& path) {
    g_log = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}
static void Log(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf_s(buf, 2048, _TRUNCATE, fmt, ap);
    va_end(ap);
    if (g_log != INVALID_HANDLE_VALUE) {
        SYSTEMTIME st; GetLocalTime(&st);
        wchar_t line[2200];
        int n = _snwprintf_s(line, 2200, _TRUNCATE, L"[%02d:%02d:%02d] %s\r\n", st.wHour, st.wMinute, st.wSecond, buf);
        char u8[4400] = "";
        int m = WideCharToMultiByte(CP_UTF8, 0, line, n, u8, sizeof(u8), nullptr, nullptr);
        DWORD wr = 0;
        WriteFile(g_log, u8, (DWORD)m, &wr, nullptr);
    }
    OutputDebugStringW(buf);
}

static std::wstring ExeDir() {
    wchar_t buf[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring s(buf);
    size_t p = s.find_last_of(L"\\/");
    return (p == std::wstring::npos) ? std::wstring(L".\\") : s.substr(0, p + 1);
}
static std::wstring ExePath() {
    wchar_t buf[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}
static bool FileExists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
static bool IsElevated() {
    HANDLE t = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) return false;
    TOKEN_ELEVATION te = {}; DWORD sz = 0;
    BOOL ok = GetTokenInformation(t, TokenElevation, &te, sizeof(te), &sz);
    CloseHandle(t);
    return ok && te.TokenIsElevated;
}

static void EnableDebugPrivilege() {
    HANDLE t = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &t)) return;
    TOKEN_PRIVILEGES tp = {};
    tp.PrivilegeCount = 1;
    if (LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &tp.Privileges[0].Luid)) {
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(t, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    }
    CloseHandle(t);
}

static DWORD FindProcess(const wchar_t* exeName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe = {}; pe.dwSize = sizeof(pe);
    DWORD best = 0; SIZE_T bestMem = 0; int count = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName) == 0) {
                count++;
                SIZE_T mem = 0;
                HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
                if (h) {
                    PROCESS_MEMORY_COUNTERS pmc = {};
                    if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) mem = pmc.WorkingSetSize;
                    CloseHandle(h);
                }
                // 同名进程可能有多个（含崩溃残留的僵尸进程，内存约 0）：选内存占用最大的那个
                if (mem > bestMem) { bestMem = mem; best = pe.th32ProcessID; }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    Log(L"同名进程数=%d 选中 PID=%u 内存=%llu MB", count, best, (unsigned long long)(bestMem / 1048576));
    return best;
}
static uintptr_t RemoteModuleBase(DWORD pid, const wchar_t* moduleName) {
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) return 0;
    HMODULE mods[512] = {}; DWORD needed = 0;
    uintptr_t base = 0;
    if (EnumProcessModulesEx(h, mods, sizeof(mods), &needed, LIST_MODULES_ALL)) {
        DWORD count = needed / sizeof(HMODULE);
        for (DWORD i = 0; i < count && i < 512; ++i) {
            wchar_t name[MAX_PATH] = L"";
            if (GetModuleBaseNameW(h, mods[i], name, MAX_PATH) && _wcsicmp(name, moduleName) == 0) {
                MODULEINFO mi = {};
                if (GetModuleInformation(h, mods[i], &mi, sizeof(mi))) { base = (uintptr_t)mi.lpBaseOfDll; break; }
            }
        }
    }
    CloseHandle(h);
    return base;
}

// 释放内嵌资源
static bool ExtractResource(int id, const std::wstring& outPath) {
    HMODULE self = GetModuleHandleW(nullptr);
    HRSRC res = FindResourceW(self, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));   // 10 = RT_RCDATA
    if (!res) { Log(L"内嵌资源 %d 不存在", id); return false; }
    DWORD size = SizeofResource(self, res);
    HGLOBAL h = LoadResource(self, res);
    const void* data = h ? LockResource(h) : nullptr;
    if (!size || !data) return false;
    HANDLE f = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { Log(L"写出失败: %s", outPath.c_str()); return false; }
    DWORD wr = 0;
    WriteFile(f, data, size, &wr, nullptr);
    CloseHandle(f);
    Log(L"已释放 %d 字节 -> %s", (int)size, outPath.c_str());
    return wr == size;
}

static bool InjectDll(DWORD pid, const std::wstring& dllPath) {
    SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    HANDLE h = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!h) {
        DWORD e = GetLastError();
        Log(L"OpenProcess 失败 (错误 %u)，尝试降级权限", e);
        h = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                        PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    }
    if (!h) { Log(L"OpenProcess 最终失败 (错误 %u)", GetLastError()); return false; }

    BOOL wow = FALSE;
    if (IsWow64Process(h, &wow) && wow) { Log(L"目标为 32 位进程，架构不匹配"); CloseHandle(h); return false; }

    void* remote = VirtualAllocEx(h, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) { Log(L"VirtualAllocEx 失败 (错误 %u)", GetLastError()); CloseHandle(h); return false; }
    if (!WriteProcessMemory(h, remote, dllPath.c_str(), bytes, nullptr)) {
        Log(L"WriteProcessMemory 失败 (错误 %u)", GetLastError());
        VirtualFreeEx(h, remote, 0, MEM_RELEASE); CloseHandle(h); return false;
    }

    // 计算目标进程内的 LoadLibraryW 地址：优先使用目标进程 kernel32 基址 + 本地偏移
    uintptr_t remoteK32 = RemoteModuleBase(pid, L"kernel32.dll");
    HMODULE localK32 = GetModuleHandleW(L"kernel32.dll");
    uintptr_t localLoad = (uintptr_t)GetProcAddress(localK32, "LoadLibraryW");
    uintptr_t addr = localLoad;
    if (remoteK32 && localK32) {
        addr = remoteK32 + (localLoad - (uintptr_t)localK32);
        Log(L"kernel32: 本地=0x%llX 远程=0x%llX LoadLibraryW=0x%llX",
            (unsigned long long)(uintptr_t)localK32, (unsigned long long)remoteK32, (unsigned long long)addr);
    } else {
        Log(L"未能读取目标模块列表，直接使用本地 LoadLibraryW=0x%llX", (unsigned long long)addr);
    }

    HANDLE th = CreateRemoteThread(h, nullptr, 0, (LPTHREAD_START_ROUTINE)addr, remote, 0, nullptr);
    if (!th) {
        Log(L"CreateRemoteThread 失败 (错误 %u)", GetLastError());
        VirtualFreeEx(h, remote, 0, MEM_RELEASE); CloseHandle(h); return false;
    }
    DWORD wait = WaitForSingleObject(th, 30000);
    DWORD code = 0;
    GetExitCodeThread(th, &code);
    Log(L"远程线程结束: wait=%u LoadLibraryW 返回=0x%llX", wait, (unsigned long long)code);
    CloseHandle(th);

    // 等待 DLL 出现在目标模块列表（简单确认）
    bool loaded = false;
    for (int i = 0; i < 20 && !loaded; ++i) {
        std::vector<HMODULE> mods(1024);
        DWORD needed = 0;
        if (EnumProcessModulesEx(h, mods.data(), (DWORD)(mods.size() * sizeof(HMODULE)), &needed, LIST_MODULES_ALL)) {
            DWORD count = needed / sizeof(HMODULE);
            for (DWORD k = 0; k < count; ++k) {
                wchar_t name[MAX_PATH] = L"";
                if (GetModuleBaseNameW(h, mods[k], name, MAX_PATH) && _wcsicmp(name, L"FSDTrainer.dll") == 0) { loaded = true; break; }
            }
        }
        if (!loaded) Sleep(100);
    }
    Log(L"DLL 模块确认: %s", loaded ? L"已加载" : L"未在模块列表中(可能已手动映射/已退出)");
    VirtualFreeEx(h, remote, 0, MEM_RELEASE);
    CloseHandle(h);
    return code != 0;
}

static void RelaunchElevated() {
    wchar_t args[1024] = L"";
    wcscat_s(args, L"--elevated");
    HINSTANCE r = ShellExecuteW(nullptr, L"runas", ExePath().c_str(), args, nullptr, SW_SHOWNORMAL);
    Log(L"以管理员身份重启: 结果=%llu", (unsigned long long)(uintptr_t)r);
}

int wmain(int argc, wchar_t** argv) {
    std::wstring target = kTargetExe;
    std::wstring dllPath;
    int timeoutSec = 300;
    bool silent = false, elevated = false, waitOnly = false, noLaunch = false;

    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--target" && i + 1 < argc) target = argv[++i];
        else if (a == L"--dll" && i + 1 < argc) dllPath = argv[++i];
        else if (a == L"--timeout" && i + 1 < argc) timeoutSec = _wtoi(argv[++i]);
        else if (a == L"--wait") timeoutSec = 600;
        else if (a == L"--silent") silent = true;
        else if (a == L"--elevated") elevated = true;
        else if (a == L"--no-launch") noLaunch = true;
        else if (a == L"--help") {
            MessageBoxW(nullptr,
                L"FSDTrainer.exe [选项]\n"
                L"  --target <exe>   目标进程名（默认 FSD-Win64-Shipping.exe）\n"
                L"  --dll <path>     指定要注入的 DLL（默认用内置资源）\n"
                L"  --timeout <秒>   等待游戏启动的最长时间（默认 300）\n"
                L"  --no-launch      不自动通过 Steam 启动游戏\n"
                L"  --silent         不弹提示框",
                L"FSDTrainer", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
    }

    std::wstring dir = ExeDir();
    LogOpen(dir + L"FSDTrainer_loader.log");
    Log(L"==== FSDTrainer 启动器 ==== 目标=%s 架构=%d 管理员=%d", target.c_str(), (int)sizeof(void*) * 8, (int)IsElevated());
    EnableDebugPrivilege();

    // 1) 目标 DLL
    if (dllPath.empty()) {
        std::wstring sibling = dir + L"FSDTrainer.dll";
        if (FileExists(sibling)) dllPath = sibling;
        else {
            wchar_t tmp[MAX_PATH] = L"";
            GetTempPathW(MAX_PATH, tmp);
            dllPath = std::wstring(tmp) + L"FSDTrainer.dll";
            if (!FileExists(dllPath) || true) {
                if (!ExtractResource(RES_DLL, dllPath)) {
                    Log(L"无法取得 FSDTrainer.dll");
                    if (!silent) MessageBoxW(nullptr, L"未找到 FSDTrainer.dll，且内置资源释放失败。", L"FSDTrainer", MB_OK | MB_ICONERROR);
                    return 3;
                }
            }
        }
    } else if (!FileExists(dllPath)) {
        Log(L"指定的 DLL 不存在: %s", dllPath.c_str());
        if (!silent) MessageBoxW(nullptr, L"指定的 DLL 不存在。", L"FSDTrainer", MB_OK | MB_ICONERROR);
        return 3;
    }
    Log(L"使用 DLL: %s", dllPath.c_str());

    // 2) 找进程
    DWORD pid = FindProcess(target.c_str());
    if (!pid) {
        Log(L"未找到进程 %s", target.c_str());
        // 按用户要求：没开游戏时不再自动拉起 Steam，直接提示先开游戏
        if (!silent) {
            MessageBoxW(nullptr,
                L"先打开游戏，在太空站中启动 Carlhand。\n\n"
                L"步骤：\n"
                L"  1) 启动《深岩银河》并进入到太空站（大厅）\n"
                L"  2) 再运行 Carlhand.exe\n\n"
                L"（现在没有检测到 FSD-Win64-Shipping.exe 进程）",
                L"卡尔的大手 (Carlhand)", MB_OK | MB_ICONINFORMATION);
        }
        return 4;
    }
    Log(L"找到进程 PID=%u", pid);

    // 3) 部署配置（游戏目录下 FSDTrainer.ini）
    std::wstring gameDir;          // 3.5) 释放内嵌 UE4SS 时要用
    {
        std::wstring gameIni;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (h) {
            wchar_t path[MAX_PATH] = L""; DWORD sz = MAX_PATH;
            if (QueryFullProcessImageNameW(h, 0, path, &sz)) {
                std::wstring s(path);
                size_t p = s.find_last_of(L"\\/");
                if (p != std::wstring::npos) { gameDir = s.substr(0, p + 1); gameIni = gameDir + L"FSDTrainer.ini"; }
            }
            CloseHandle(h);
        }
        if (!gameIni.empty()) {
            bool stale = false;
            if (FileExists(gameIni)) {
                HANDLE f = CreateFileW(gameIni.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (f != INVALID_HANDLE_VALUE) {
                    DWORD sz = GetFileSize(f, nullptr);
                    if (sz > 4u * 1024 * 1024) sz = 4u * 1024 * 1024;
                    std::vector<char> buf(sz > 2 ? sz : 2, 0);
                    DWORD rd = 0;
                    ReadFile(f, buf.data(), sz, &rd, nullptr);
                    CloseHandle(f);
                    std::wstring ws;
                    if (rd >= 2 && (unsigned char)buf[0] == 0xFF && (unsigned char)buf[1] == 0xFE)
                        ws.assign((const wchar_t*)(buf.data() + 2), (rd - 2) / 2);
                    else
                        ws.assign(buf.begin(), buf.begin() + rd);
                    stale = ws.find(L"[feature.") == std::wstring::npos
                         || ws.find(L"ini_ver=46") == std::wstring::npos;   // 版本不符 → 重新部署功能表
                }
            }
            if (!FileExists(gameIni) || stale) {
                if (FileExists(gameIni)) {
                    std::wstring bak = gameIni + L".bak";
                    DeleteFileW(bak.c_str());
                    if (CopyFileW(gameIni.c_str(), bak.c_str(), FALSE)) Log(L"旧配置已备份: %s.bak", gameIni.c_str());
                }
                if (ExtractResource(RES_INI, gameIni)) Log(L"已部署功能表配置: %s", gameIni.c_str());
            } else {
                Log(L"配置已存在，保持不变: %s", gameIni.c_str());
            }
        }
    }

    // 3.5) 开箱即用：把内嵌的 UE4SS v3.0.1 释放到游戏目录（缺失才写，不覆盖别人的）
    if (!gameDir.empty()) {
        std::wstring u = gameDir + L"UE4SS.dll";
        std::wstring us = gameDir + L"UE4SS-settings.ini";
        if (!FileExists(u)) { if (ExtractResource(RES_UE4SS_DLL, u)) Log(L"已部署 UE4SS.dll（游戏内引擎依赖它）"); }
        else Log(L"UE4SS.dll 已存在，保持不动");
        if (!FileExists(us)) { if (ExtractResource(RES_UE4SS_INI, us)) Log(L"已部署 UE4SS-settings.ini"); }
    } else {
        Log(L"警告: 取不到游戏目录，跳过 UE4SS 部署");
    }

    // 4) 注入
    if (!InjectDll(pid, dllPath)) {
        DWORD e = GetLastError();
        Log(L"注入失败 (最后错误 %u, 管理员=%d)", e, (int)IsElevated());
        if (!IsElevated() && !elevated) {
            Log(L"尝试以管理员身份重启自身");
            RelaunchElevated();
            return 5;
        }
        if (!silent) MessageBoxW(nullptr, L"注入失败，详见 FSDTrainer_loader.log。", L"FSDTrainer", MB_OK | MB_ICONERROR);
        return 6;
    }

    Log(L"注入成功，进入游戏后按 INSERT 呼出菜单");
    if (!silent)
        MessageBoxW(nullptr, L"注入成功！\n\n进入游戏场景后按 INSERT 呼出 FSDTrainer 菜单。", L"FSDTrainer", MB_OK | MB_ICONINFORMATION);
    return 0;
}
