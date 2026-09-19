# 开箱即用（第 2 步）：loader.cpp 释放 UE4SS + dllmain.cpp 部署 CheatManagerEnablerMod 并 LoadLibrary
import io

root = r'D:\Deepseek-Harness\work\260918_143041'
ue4dir = r'D:\Deepseek-Harness\install\UE4SS'

# ---------- loader.cpp ----------
p = root + r'\loader\FSDTrainerLoader.cpp'
s = io.open(p, encoding='utf-8').read()
if 'RES_UE4SS_DLL' not in s:
    s = s.replace('#define RES_INI  102', '#define RES_INI  102\n#define RES_UE4SS_DLL 103\n#define RES_UE4SS_INI 104', 1)
    s = s.replace('std::wstring gameIni;', 'std::wstring gameIni, gameDir;', 1)
    s = s.replace('if (p != std::wstring::npos) gameIni = s.substr(0, p + 1) + L"FSDTrainer.ini";',
                  'if (p != std::wstring::npos) { gameDir = s.substr(0, p + 1); gameIni = gameDir + L"FSDTrainer.ini"; }', 1)
    anchor = '''    // 4) 注入
    if (!InjectDll(pid, dllPath)) {'''
    add = '''    // 3.5) 开箱即用：把内嵌的 UE4SS v3.0.1 释放到游戏目录（缺失才写，不覆盖别人的）
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
    if (!InjectDll(pid, dllPath)) {'''
    assert anchor in s, 'inject anchor missing'
    s = s.replace(anchor, add, 1)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)
    print('loader.cpp patched')
else:
    print('loader.cpp already patched')

# ---------- dllmain.cpp ----------
p = root + r'\src\dllmain.cpp'
s = io.open(p, encoding='utf-8').read()
if 'kCheatEnablerLua' in s:
    print('dllmain.cpp already patched')
else:
    cheat = io.open(ue4dir + r'\解压后\Mods\CheatManagerEnablerMod\Scripts\main.lua', encoding='utf-8').read()
    anchor = 'static bool WriteIfDifferent(const std::wstring& path, const std::string& body) {'
    assert anchor in s
    s = s.replace(anchor,
        '// UE4SS 自带的“作弊管理器创建器”：没有它，C_XXX 系列作弊命令没人接\n'
        'static const char* kCheatEnablerLua = R"FSDCHEAT(' + cheat + ')FSDCHEAT";\n\n' + anchor, 1)

    old_tail = '''    // 诊断：没有 UE4SS.dll 说明装的是不支持 Lua 的加载器（如 UE4SSL），引擎不可能加载
    std::wstring ue4 = dir + L"UE4SS.dll";
    if (GetFileAttributesW(ue4.c_str()) == INVALID_FILE_ATTRIBUTES)
        Log("警告: 游戏目录没有 UE4SS.dll（Lua 版）——游戏内引擎无法加载，请恢复 UE4SS v3.0.1");
}'''
    assert old_tail in s, 'dllmain tail anchor missing'
    new_tail = '''    // 作弊管理器创建器（C_XXX 命令需要它）
    {
        std::wstring m2 = modsDir + L"\\\\CheatManagerEnablerMod";
        std::wstring d2 = m2 + L"\\\\Scripts";
        CreateDirectoryW(m2.c_str(), nullptr);
        CreateDirectoryW(d2.c_str(), nullptr);
        if (WriteIfDifferent(d2 + L"\\\\main.lua", std::string(kCheatEnablerLua)))
            Log("已部署 CheatManagerEnablerMod（作弊命令需要它）");
        std::string txt2;
        {
            FILE* f = nullptr;
            if (_wfopen_s(&f, mt.c_str(), L"rb") == 0 && f) {
                char b[8192]; size_t n = 0;
                while ((n = fread(b, 1, sizeof(b), f)) > 0) txt2.append(b, n);
                fclose(f);
            }
        }
        std::string want; bool changed = false;
        if (txt2.find("FSDTrainerMod") == std::string::npos) { want += "FSDTrainerMod : 1\\r\\n"; changed = true; }
        if (txt2.find("CheatManagerEnablerMod") == std::string::npos) { want += "CheatManagerEnablerMod : 1\\r\\n"; changed = true; }
        if (changed) WriteIfDifferent(mt, want + txt2);
    }
    // 开箱即用：UE4SS 没被加载时我们自己把它 LoadLibrary 进来（这一局就能用，不用重启游戏）
    {
        std::wstring ue4 = dir + L"UE4SS.dll";
        if (GetModuleHandleW(L"UE4SS.dll") != nullptr) {
            Log("UE4SS.dll 已在本进程中");
        } else if (GetFileAttributesW(ue4.c_str()) != INVALID_FILE_ATTRIBUTES) {
            HMODULE h = LoadLibraryW(ue4.c_str());
            if (h) Log("已加载 UE4SS.dll（游戏内引擎组件）模块=0x%p", (void*)h);
            else Log("警告: UE4SS.dll 加载失败 (错误 %u)", GetLastError());
        } else {
            Log("警告: 游戏目录没有 UE4SS.dll —— 游戏内引擎无法加载");
        }
    }
}'''
    s = s.replace(old_tail, new_tail, 1)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)
    print('dllmain.cpp patched')
