# 把"执行器 mod 自动部署"补进 dllmain.cpp：
#   1) 在 DeployLuaEngine 之前插入 kExecutorLua（来自 reference\FSDTrainerMod_main_v7.lua）+ DeployExecutorMod()
#   2) 在 InitThread 里调用它，并在缺少 UE4SS.dll 时打警告
import io, re

root = r'D:\Deepseek-Harness\work\260918_143041'
dll = root + r'\src\dllmain.cpp'
exec_lua = io.open(root + r'\reference\FSDTrainerMod_main_v7.lua', encoding='utf-8').read()

s = io.open(dll, encoding='utf-8').read()
assert 'DeployExecutorMod' not in s, 'already patched'

helper = '''
// ---------------- 游戏内执行器 mod 的自动部署 ----------------
// UE4SS 只从 <游戏目录>\\Mods\\<名字>\\Scripts\\main.lua 加载 Lua mod；mods.txt 里要有一行 <名字> : 1。
// 用户的第三方工具包可能把 UE4SS 换成不支持 Lua 的版本（例如 UE4SSL）或整个删掉 Mods\\，
// 那样我们的 Lua 引擎永远不会被加载（菜单还能开、但所有游戏功能静默失效）。
// 所以每次注入都把执行器补齐，并检查 UE4SS.dll 是否还在。
static const char* kExecutorLua = R"FSDEXEC(
)FSDEXEC" ;   // 内容在下面替换

static bool WriteIfDifferent(const std::wstring& path, const std::string& body) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") == 0 && f) {
        std::string cur; char b[8192]; size_t n = 0;
        while ((n = fread(b, 1, sizeof(b), f)) > 0) cur.append(b, n);
        fclose(f);
        if (cur == body) return false;              // 内容一样就不动文件
    }
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;
    fwrite(body.data(), 1, body.size(), f);
    fclose(f);
    return true;
}

static void DeployExecutorMod(const std::wstring& dir) {
    std::wstring modsDir = dir + L"Mods";
    std::wstring modDir  = modsDir + L"\\FSDTrainerMod";
    std::wstring scrDir  = modDir + L"\\Scripts";
    CreateDirectoryW(modsDir.c_str(), nullptr);
    CreateDirectoryW(modDir.c_str(), nullptr);
    CreateDirectoryW(scrDir.c_str(), nullptr);
    if (WriteIfDifferent(scrDir + L"\\main.lua", std::string(kExecutorLua)))
        Log("已部署执行器 mod: %s", Narrow(scrDir + L"\\main.lua").c_str());
    std::wstring mt = modsDir + L"\\mods.txt";
    std::string txt;
    {
        FILE* f = nullptr;
        if (_wfopen_s(&f, mt.c_str(), L"rb") == 0 && f) {
            char b[8192]; size_t n = 0;
            while ((n = fread(b, 1, sizeof(b), f)) > 0) txt.append(b, n);
            fclose(f);
        }
    }
    if (txt.find("FSDTrainerMod") == std::string::npos) {
        WriteIfDifferent(mt, std::string("FSDTrainerMod : 1\\r\\n") + txt);
        Log("mods.txt 已加入 FSDTrainerMod");
    }
    // 诊断：没有 UE4SS.dll 说明装的是不支持 Lua 的加载器（如 UE4SSL），引擎不可能加载
    std::wstring ue4 = dir + L"UE4SS.dll";
    if (GetFileAttributesW(ue4.c_str()) == INVALID_FILE_ATTRIBUTES)
        Log("警告: 游戏目录没有 UE4SS.dll（Lua 版）——游戏内引擎无法加载，请恢复 UE4SS v3.0.1");
}

'''

# 用真正的执行器内容替换占位（用 Python 的行拼接，避免 raw string 里出现 )FSDEXEC" 的意外）
helper = helper.replace('R"FSDEXEC(\n)FSDEXEC" ;   // 内容在下面替换',
                        'R"FSDEXEC(' + exec_lua + ')FSDEXEC"')

anchor = '// 部署游戏内 Lua 引擎，并保证命令文件里有加载指令'
assert anchor in s
s = s.replace('// 延迟把命令文件置空', helper + '// 延迟把命令文件置空', 1)

call_anchor = '        DeployLuaEngine(lua, cmdf);'
assert call_anchor in s
s = s.replace(call_anchor, '        DeployExecutorMod(dir);\n' + call_anchor, 1)

io.open(dll, 'w', encoding='utf-8', newline='').write(s)
print('patched dllmain.cpp, executor bytes =', len(exec_lua))
