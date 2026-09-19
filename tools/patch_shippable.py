# 开箱即用：把 UE4SS v3.0.1 内嵌进 exe（资源 103/104），loader 释放到游戏目录，
# DLL 侧再 LoadLibrary(UE4SS.dll)（当前这局就生效），并补部署 CheatManagerEnablerMod。
import io, os, re

root = r'D:\Deepseek-Harness\work\260918_143041'
ue4dir = r'D:\Deepseek-Harness\install\UE4SS'

# ---------- 1) loader.bat：加 103(UE4SS.dll) 104(settings.ini) ----------
p = root + r'\build\loader.bat'
s = io.open(p, encoding='utf-8').read()
old = """  "$dll=('%OUT%\\FSDTrainer.dll').Replace('\\','\\\\'); $ini=('%ROOT%\\config\\FSDTrainer.ini').Replace('\\','\\\\'); $o='%RC_FILE%';" ^
  "$t='101 RCDATA \\"'+$dll+'\\"'+\"`r`n\"+'102 RCDATA \\"'+$ini+'\\"'+\"`r`n\";" ^"""
new = """  "$dll=('%OUT%\\FSDTrainer.dll').Replace('\\','\\\\'); $ini=('%ROOT%\\config\\FSDTrainer.ini').Replace('\\','\\\\'); $o='%RC_FILE%';" ^
  "$u=('%UE4DIR%\\UE4SS.dll').Replace('\\','\\\\'); $us=('%ROOT%\\config\\UE4SS-settings.ini').Replace('\\','\\\\');" ^
  "$t='101 RCDATA \\"'+$dll+'\\"'+\"`r`n\"+'102 RCDATA \\"'+$ini+'\\"'+\"`r`n\"+'103 RCDATA \\"'+$u+'\\"'+\"`r`n\"+'104 RCDATA \\"'+$us+'\\"'+\"`r`n\";" ^"""
assert old in s, 'loader.bat anchor not found'
s = s.replace(old, new, 1)
s = s.replace('set "VSROOT=C:\\BuildTools"',
              'set "VSROOT=C:\\BuildTools"\r\nset "UE4DIR=D:\\Deepseek-Harness\\install\\UE4SS"', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('loader.bat patched')

# ---------- 2) loader.cpp：资源 id + 释放到游戏目录 ----------
p = root + r'\loader\FSDTrainerLoader.cpp'
s = io.open(p, encoding='utf-8').read()
s = s.replace('#define RES_INI  102', '#define RES_INI  102\n#define RES_UE4SS_DLL 103\n#define RES_UE4SS_INI 104', 1)
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
assert anchor in s
s = s.replace(anchor, add, 1)
# gameDir 变量：确认存在
if 'std::wstring gameDir' not in s:
    # 从 gameIni 反推：在 gameIni 赋值处同时记录目录
    s = s.replace('std::wstring gameIni;', 'std::wstring gameIni, gameDir;', 1)
    s = s.replace('if (p != std::wstring::npos) gameIni = s.substr(0, p + 1) + L"FSDTrainer.ini";',
                  'if (p != std::wstring::npos) { gameDir = s.substr(0, p + 1); gameIni = gameDir + L"FSDTrainer.ini"; }', 1)
    print('loader.cpp: gameDir wired')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('loader.cpp patched')

# ---------- 3) dllmain.cpp：CheatManagerEnablerMod + LoadLibrary(UE4SS) ----------
p = root + r'\src\dllmain.cpp'
s = io.open(p, encoding='utf-8').read()
cheat = io.open(ue4dir + r'\解压后\Mods\CheatManagerEnablerMod\Scripts\main.lua', encoding='utf-8').read()
assert 'kCheatEnablerLua' not in s
anchor = 'static bool WriteIfDifferent(const std::wstring& path, const std::string& body) {'
addition = ('// UE4SS 自带的“作弊管理器创建器”：没有它，C_XXX 系列作弊命令没人接\n'
            'static const char* kCheatEnablerLua = R"FSDCHEAT(' + cheat + ')FSDCHEAT";\n\n'
            + anchor)
s = s.replace(anchor, addition, 1)

# 在 DeployExecutorMod 末尾（警告之前）补：写 CheatManagerEnablerMod + 两个 mods.txt 条目 + LoadLibrary(UE4SS)
old_tail = '''    // 诊断：没有 UE4SS.dll 说明装的是不支持 Lua 的加载器（如 UE4SSL），引擎不可能加载
    std::wstring ue4 = dir + L"UE4SS.dll";
    if (GetFileAttributesW(ue4.c_str()) == INVALID_FILE_ATTRIBUTES)
        Log("警告: 游戏目录没有 UE4SS.dll（Lua 版）——游戏内引擎无法加载，请恢复 UE4SS v3.0.1");
}'''
new_tail = '''    // 作弊管理器创建器（C_XXX 命令需要它）
    {
        std::wstring d2 = modsDir + L"\\\\CheatManagerEnablerMod\\\\Scripts";
        CreateDirectoryW((modsDir + L"\\\\CheatManagerEnablerMod").c_str(), nullptr);
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
        std::string want;
        bool changed = false;
        if (txt2.find("FSDTrainerMod") == std::string::npos) { want += "FSDTrainerMod : 1\\r\\n"; changed = true; }
        if (txt2.find("CheatManagerEnablerMod") == std::string::npos) { want += "CheatManagerEnablerMod : 1\\r\\n"; changed = true; }
        if (changed) WriteIfDifferent(mt, want + txt2);
    }
    // 开箱即用：UE4SS 没被加载时，我们自己把它 LoadLibrary 进来（这一局就能用，不用重启游戏）
    {
        std::wstring ue4 = dir + L"UE4SS.dll";
        if (GetModuleHandleW(L"UE4SS.dll") != nullptr) {
            Log("UE4SS.dll 已在本进程中");
        } else if (GetFileAttributesW(ue4.c_str()) != INVALID_FILE_ATTRIBUTES) {
            HMODULE h = LoadLibraryW(ue4.c_str());
            if (h) Log("已加载 UE4SS.dll（游戏内引擎组件），模块=0x%p", (void*)h);
            else Log("警告: UE4SS.dll 加载失败 (错误 %u)", GetLastError());
        } else {
            Log("警告: 游戏目录没有 UE4SS.dll —— 游戏内引擎无法加载（请把 Carlhand.exe 与 UE4SS 一起使用）");
        }
    }
}'''
assert old_tail in s
s = s.replace(old_tail, new_tail, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('dllmain.cpp patched')
