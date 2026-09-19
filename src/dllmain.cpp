// dllmain.cpp - 模块入口：初始化日志/配置/扫描器/补丁/覆盖层/热键
#include "common.h"
#include "log.h"
#include "config.h"
#include "mem.h"
#include "scanner.h"
#include "patches.h"
#include "cheat_engine.h"
#include "ui.h"
#include "overlay.h"

namespace fsd {
    Config      g_cfg;
    Scanner     g_scanner;
    Freezer     g_freezer;
    PatchEngine g_patches;
    FeatureTable g_features;
    volatile bool g_running = false;
}

using namespace fsd;

static HMODULE g_selfModule = nullptr;
static void CursorShow(bool show);

// 按 END 键“杀死修改器”：解除全部挂钩、恢复窗口过程、停线程、卸载本 DLL
static void KillTrainer() {
    Log("=== 收到杀死键 %s：正在卸载修改器 ===", HotkeyName(g_cfg.killKey));
    g_running = false;
    g_unloading = true;                 // 渲染钩子立刻空转
    ClipCursor(nullptr);
    CursorShow(true);
    g_scanner.cancel = true;            // 停止进行中的扫描
    g_freezer.Stop();                   // 停止冻结写入
    g_overlay.UninstallHooks();         // 恢复窗口过程 + 解除 Present/Resize/Present1/ECL 挂钩
    Sleep(300);                         // 等渲染线程离开钩子
    for (int i = 0; i < 40 && g_scanner.busy.load(); ++i) Sleep(10);
    Log("修改器已卸载：挂钩全部解除、游戏窗口过程已恢复、冻结与扫描已停止，游戏继续正常运行。");
    Log("提示：被 user32 钉住的模块映像可能仍驻留进程内（系统行为），但已没有任何本工具的代码在运行。");
    LogClose();
    if (g_selfModule) FreeLibraryAndExitThread(g_selfModule, 0);
}

static std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH] = L"";
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring s(buf);
    size_t p = s.find_last_of(L"\\/");
    return (p == std::wstring::npos) ? std::wstring(L".\\") : s.substr(0, p + 1);
}

static std::wstring PickConfigPath() {
    wchar_t env[MAX_PATH] = L"";
    if (GetEnvironmentVariableW(L"FSDTRAINER_INI", env, MAX_PATH) > 0) return env;
    return GetExeDir() + L"FSDTrainer.ini";
}

static std::wstring ShortPathOf(const std::wstring& p) {
    wchar_t buf[MAX_PATH * 2] = L"";
    DWORD n = GetShortPathNameW(p.c_str(), buf, MAX_PATH * 2);
    return (n && n < MAX_PATH * 2 && buf[0]) ? std::wstring(buf) : p;
}


// ---------------- 游戏内执行器 mod 的自动部署 ----------------
// UE4SS 只从 <游戏目录>\Mods\<名字>\Scripts\main.lua 加载 Lua mod；mods.txt 里要有一行 <名字> : 1。
// 用户的第三方工具包可能把 UE4SS 换成不支持 Lua 的版本（例如 UE4SSL）或整个删掉 Mods\，
// 那样我们的 Lua 引擎永远不会被加载（菜单还能开、但所有游戏功能静默失效）。
// 所以每次注入都把执行器补齐，并检查 UE4SS.dll 是否还在。
static const char* kExecutorLua = R"FSDEXEC(-- ===== 执行器 v7 =====
-- 相对 v6 的修改（修"引擎莫名其妙不再运行"）：
--   1) 命令文件里没有可执行行时，完全不去碰游戏对象（v6 每 300ms 都 FindAllOf，早期初始化阶段可能打死 UE4SS 的 Lua 线程）
--   2) 只有 lock/cheat 这类真的需要对象的命令才去取 PlayerController/Pawn
--   3) 每 30 秒写一次心跳，日志里能直接看出执行器是否还活着
--   4) 所有异常都被 pcall 兜住并记录，循环永不退出
local DIR = "E:/SteamLibrary/steamapps/common/Deep Rock Galactic/FSD/Binaries/Win64/"
local CMD, LOG = DIR.."FSDTrainer_cmd.txt", DIR.."FSDTrainer_dump.txt"
local function W(s) local ok,f=pcall(io.open,LOG,"a") if ok and f then f:write(tostring(s).."\n") f:flush() f:close() end end
local function readf(p) local ok,f=pcall(io.open,p,"r") if not ok or not f then return "" end local s=f:read("*a") f:close() return s or "" end
local function nm(o) if not o then return end local ok,v=pcall(function() return o:GetFullName() end) if ok then return v end end
local function gp(o,n) if not o then return end local ok,v=pcall(function() return o:GetPropertyValue(n) end) if ok then return v end end
local function sp(o,n,v) if not o then return end pcall(function() o:SetPropertyValue(n,v) end) end
local function first(c) local ok,a=pcall(FindAllOf,c) if ok and type(a)=="table" and #a>0 then return a[1] end end
local function localPawn()
  local pc=first("PlayerController"); if pc then local p=gp(pc,"Pawn"); if p and nm(p) then return p,pc end end
  local a=first("PlayerCharacter"); if a then local n=nm(a) if n and string.find(n,"Navigator") then return a,first("PlayerController") end return a,first("PlayerController") end
end
local CLS={"HealthComponent","ShieldComponent","CharacterMovementComponent","PlayerMovementComponent","InventoryComponent"}
local function compOf(short,cls) local ok,l=pcall(FindAllOf,cls) if not ok or type(l)~="table" then return end
  for _,o in ipairs(l) do local n=nm(o) if n and string.find(n,short,1,true) then return o end end end
local function shortOf(p) local n=nm(p) return n and (string.match(n,"PersistentLevel%.([^%.]+)$") or n) end
local function resolve(p,short,prop)
  local v=gp(p,prop); if v~=nil and type(v)=="number" then return p end
  for _,c in ipairs(CLS) do local o=compOf(short,c) if o then local vv=gp(o,prop) if vv~=nil and type(vv)=="number" then return o end end end
end
local function doCheat(p,pc,cmd)
  W("  [cheat] "..cmd)
  local cm = pc and gp(pc,"CheatManager")
  if cm and nm(cm) then local ok,r=pcall(function() return cm[cmd](cm) end) W("    cm."..cmd.."() -> ok="..tostring(ok).." r="..tostring(r)) end
  local k=StaticFindObject("/Script/Engine.Default__KismetSystemLibrary")
  if k then local ok,r=pcall(function() return k:ExecuteConsoleCommand(p,cmd,nil) end) W("    Kismet -> ok="..tostring(ok).." r="..tostring(r)) end
  if pc then local ok,r=pcall(function() return pc:ConsoleCommand(cmd) end) W("    ConsoleCommand -> ok="..tostring(ok).." r="..tostring(r)) end
end

local locks,lastCmd,lastPawn,lastBeat = {}, "", "", os.time()
W("\n===== 执行器 v7 "..os.date("%H:%M:%S").." =====")
LoopAsync(300, function()
  local ok,err = pcall(function()
    if os.time()-lastBeat >= 30 then lastBeat=os.time() W("  <心跳> 执行器存活 "..os.date("%H:%M:%S")) end
    local txt = readf(CMD)
    if txt ~= lastCmd then
      lastCmd = txt
      local lines, needObj = {}, false
      for line in txt:gmatch("[^\r\n]+") do
        local a = line:match("^%s*(%a+)")
        if a then
          lines[#lines+1] = line
          if a=="lock" or a=="cheat" then needObj = true end
        end
      end
      W("[命令] "..txt:gsub("\r?\n"," | "))
      local p, pc
      if needObj then p, pc = localPawn() end
      for _,line in ipairs(lines) do
        local cmd, rest = line:match("^%s*(%a+)%s*(.*)$")
        if cmd=="clear" then locks={} W("  清空")
        elseif cmd=="off" then local pr=rest:match("^(%S+)"); locks[pr]=nil W("  解除 "..tostring(pr))
        elseif cmd=="cheat" then local c=rest:match("^([%w_]+)"); if c and p then doCheat(p,pc,c) end
        elseif cmd=="lua" then
          local f=rest:match("^(%S+)"); local src=readf(f)
          if src=="" then W("  lua 文件为空/不存在: "..tostring(f)) else
            local fn,e1=load(src,"hotload")
            if fn then local ok2,e2=pcall(fn) W("  lua "..f.." -> ok="..tostring(ok2).." "..tostring(e2)) else W("  lua 编译失败: "..tostring(e1)) end
          end
        elseif cmd=="lock" then
          local prop,mode,val = rest:match("^(%S+)%s+(%a+)%s*(%S*)$")
          if prop and p then
            local short = shortOf(p)
            local o = resolve(p,short,prop)
            if not o then W("  锁定失败："..prop)
            elseif not locks[prop] or locks[prop].orig==nil then
              locks[prop]={obj=o,mode=mode or "mult",val=tonumber(val) or 1.5,orig=gp(o,prop)}
              W("  锁定 "..prop.." "..tostring(mode).."="..tostring(val).." 原始="..tostring(locks[prop].orig))
            end
          end
        end
      end
    end
    if next(locks) then
      local p = localPawn()
      if p then
        local short = shortOf(p)
        for prop,L in pairs(locks) do
          if L.obj and L.orig~=nil then
            local t = L.orig
            if L.mode=="mult" then t=L.orig*L.val elseif L.mode=="set" then t=L.val end
            sp(L.obj,prop,t)
          elseif not L.obj then
            local o = resolve(p,short,prop)
            if o then L.obj=o L.orig=gp(o,prop) end
          end
        end
      end
    end
  end)
  if not ok then W("  !!! 执行器异常(已继续): "..tostring(err)) end
  return false
end)
)FSDEXEC";

// UE4SS 自带的“作弊管理器创建器”：没有它，C_XXX 系列作弊命令没人接
static const char* kCheatEnablerLua = R"FSDCHEAT(RegisterHook("/Script/Engine.PlayerController:ClientRestart", function(self, NewPawn)
    local PlayerController = self:get()
    
    local function clsName(o)
        if not o then return "nil" end
        local ok,c=pcall(function() return o:GetClass():GetFullName() end)
        if ok and type(c)=="string" then return c end
        return "?"
    end
    -- 已经有游戏的 FSDCheatManager 就别重复造（否则会把真正带作弊函数的那个换掉）
    local existing = PlayerController.CheatManager
    if existing ~= nil then
        local okv,v = pcall(function() return existing:IsValid() end)
        if okv and v and string.find(clsName(existing) or "", "FSDCheatManager", 1, true) then
            print("[CheatManager Creator] 已有 FSDCheatManager，跳过\n")
            return
        end
    end
    -- 优先游戏自己的 FSDCheatManager（C_XXX 那些函数都在它身上）
    local CheatManagerClass = StaticFindObject("/Script/FSD.FSDCheatManager")
    if not (CheatManagerClass and CheatManagerClass:IsValid()) then
        CheatManagerClass = PlayerController.CheatClass
    end
    if not (CheatManagerClass and CheatManagerClass:IsValid()) then
        print("[CheatManager Creator] Controller:CheatClass is nullptr, using default CheatClass instead\n")
        CheatManagerClass = StaticFindObject("/Script/Engine.CheatManager")
    end
    
    if not CheatManagerClass:IsValid() then
        print("[CheatManager Creator] Couldn't find default CheatClass, therefore, could not enable Cheat Manager\n")
        return
    end
    
    local CreatedCheatManager = StaticConstructObject(CheatManagerClass, PlayerController, 0, 0, 0, nil, false, false, nil)
    if CreatedCheatManager:IsValid() then
        print(string.format("[CheatManager Creator] Constructed %s [0x%X]\n", clsName(CreatedCheatManager), CreatedCheatManager:GetAddress()))
        
        PlayerController.CheatManager = CreatedCheatManager
        print("[CheatManager Creator] Enabled CheatManager\n")
    else
        print("[CheatManager Creator] Was unable to construct CheatManager, therefor, could not enable Cheat Manager\n")
    end
end)
)FSDCHEAT";

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
        WriteIfDifferent(mt, std::string("FSDTrainerMod : 1\r\n") + txt);
        Log("mods.txt 已加入 FSDTrainerMod");
    }    // 作弊管理器创建器（C_XXX 命令需要它）
    {
        std::wstring m2 = modsDir + L"\\CheatManagerEnablerMod";
        std::wstring d2 = m2 + L"\\Scripts";
        CreateDirectoryW(m2.c_str(), nullptr);
        CreateDirectoryW(d2.c_str(), nullptr);
        if (WriteIfDifferent(d2 + L"\\main.lua", std::string(kCheatEnablerLua)))
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
        if (txt2.find("FSDTrainerMod") == std::string::npos) { want += "FSDTrainerMod : 1\r\n"; changed = true; }
        if (txt2.find("CheatManagerEnablerMod") == std::string::npos) { want += "CheatManagerEnablerMod : 1\r\n"; changed = true; }
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
}

// 延迟把命令文件置空：避免"下次单独启动游戏"时执行器自动加载磁盘上的旧引擎脚本。
// 旧版 Lua 循环一旦载入就无法卸载（LoopAsync 无法从外部取消），新旧两个循环会同时跑、
// 互相覆盖共享状态 → 进任务时崩溃。引擎只应在训练器注入后由本函数加载。
static DWORD WINAPI NeutralizeCmdThread(LPVOID param) {
    std::wstring* path = (std::wstring*)param;
    Sleep(15000);
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path->c_str(), L"wb") == 0 && fp) {
        const char* idle = "# 引擎已由训练器载入；此处刻意留空，防止单独启动游戏时自动加载旧引擎\r\nclear\r\n";
        fwrite(idle, 1, strlen(idle), fp);
        fclose(fp);
        Log("命令文件已置空（防止下次单独启动游戏时自动加载旧引擎）");
    }
    delete path;
    return 0;
}

// 部署游戏内 Lua 引擎，并保证命令文件里有加载指令
static void DeployLuaEngine(const std::wstring& luaPath, const std::wstring& cmdPath) {
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, luaPath.c_str(), L"wb") == 0 && fp) {
        // ★v96：用数组实际长度算块数（不再信任那个常量），双保险
        const int nParts = (int)(sizeof(kCheatEngineLuaParts) / sizeof(kCheatEngineLuaParts[0]));
        for (int i = 0; i < nParts; ++i) {
            const char* part = kCheatEngineLuaParts[i];
            fwrite(part, 1, strlen(part), fp);
        }
        fclose(fp);
        Log("游戏内引擎脚本已部署: %s（%d 块）", Narrow(luaPath).c_str(), nParts);
    } else {
        Log("引擎脚本写入失败: %s", Narrow(luaPath).c_str());
    }
    std::string content, luaCmd = "lua " + Narrow(ShortPathOf(luaPath));
    FILE* rf = nullptr;
    if (_wfopen_s(&rf, cmdPath.c_str(), L"rb") == 0 && rf) {
        char buf[8192]; size_t n = 0;
        while ((n = fread(buf, 1, sizeof(buf), rf)) > 0) content.append(buf, n);
        fclose(rf);
    }
    // 去掉旧的加载指令与标记行，然后写入一条全新的加载指令：
    // 每次启动训练器都会让游戏内执行器重新加载引擎脚本（脚本自带重复加载保护）
    if (!content.empty()) {
        std::string keep;
        size_t pos = 0;
        while (pos < content.size()) {
            size_t eol = content.find('\n', pos);
            if (eol == std::string::npos) eol = content.size();
            std::string line = content.substr(pos, eol - pos);
            pos = eol + 1;
            std::string t = line;
            while (!t.empty() && (t[0] == ' ' || t[0] == '\t' || t[0] == '\r')) t.erase(0, 1);
            if (t.compare(0, 4, "lua ") == 0) continue;      // 旧加载指令
            if (t.compare(0, 5, "; run") == 0) continue;     // 旧标记行
            keep += line; keep += "\r\n";
        }
        content = keep;
    }
    {
        char mark[64] = "";
        sprintf_s(mark, "; run%lu\r\n", (unsigned long)(GetTickCount() % 100000));
        std::string out = luaCmd + "\r\n" + mark + content;
        FILE* fp2 = nullptr;
        if (_wfopen_s(&fp2, cmdPath.c_str(), L"wb") == 0 && fp2) {
            fwrite(out.data(), 1, out.size(), fp2);
            fclose(fp2);
            Log("命令文件已写入加载指令: %s", luaCmd.c_str());
            HANDLE h = CreateThread(nullptr, 0, NeutralizeCmdThread, new std::wstring(cmdPath), 0, nullptr);
            if (h) CloseHandle(h);
        }
    }
}

static void CursorShow(bool show) {
    if (show) { while (ShowCursor(TRUE) < 0) {} }
    else { while (ShowCursor(FALSE) >= 0) {} }
}

static void HotkeyThread() {
    bool lastMenu = false;
    bool lastKill = false;
    bool lastRestore = false;
    bool lastReset = false;
    std::vector<bool> last(g_cfg.patches.size(), false);
    std::vector<bool> lastF(g_features.list.size(), false);
    Log("热键线程启动 菜单键=%s 杀死键=%s 恢复键=%s 补丁热键=%zu", HotkeyName(g_cfg.menuKey), HotkeyName(g_cfg.killKey),
        g_cfg.restoreKey ? HotkeyName(g_cfg.restoreKey) : "无", g_cfg.patches.size());
    while (g_running) {
        if (g_cfg.killKey) {
            bool cur = (GetAsyncKeyState((int)g_cfg.killKey) & 0x8000) != 0;
            if (cur && !lastKill) { KillTrainer(); return; }
            lastKill = cur;
        }
        if (g_cfg.restoreKey) {
            bool cur = (GetAsyncKeyState((int)g_cfg.restoreKey) & 0x8000) != 0;
            if (cur && !lastRestore) {
                RestoreGameState();
                Ui* ui = g_overlay.GetUi();
                if (ui) ui->SetNote(L"已一键恢复：补丁撤销、冻结解除并写回原值");
            }
            lastRestore = cur;
        }
        // 备用菜单键 F6（用户的 DX11 修改器用 Insert/F6，我们跟上）
        {
            static bool lastF6 = false;
            bool f6 = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
            if (f6 && !lastF6) {
                Ui* ui = g_overlay.GetUi();
                if (ui) { ui->Toggle(); CursorShow(ui->visible); Log("热键 F6: 菜单%s", ui->visible ? "打开" : "关闭"); }
            }
            lastF6 = f6;
        }
        if (g_cfg.menuKey) {
            bool cur = (GetAsyncKeyState((int)g_cfg.menuKey) & 0x8000) != 0;
            if (cur && !lastMenu) {
                Ui* ui = g_overlay.GetUi();
                if (ui) {
                    ui->Toggle();
                    CursorShow(ui->visible);
                    Log("热键 %s: 菜单%s（后端=%d 分辨率=%dx%d 渲染帧=%d）", HotkeyName(g_cfg.menuKey),
                        ui->visible ? "打开" : "关闭", g_ovl.backend.load(), g_ovl.bbW.load(), g_ovl.bbH.load(), g_ovl.frame.load());
                } else {
                    Log("热键 %s 按下，但 UI 未建立（覆盖层安装失败？后端=%d）", HotkeyName(g_cfg.menuKey), g_ovl.backend.load());
                }
            }
            lastMenu = cur;
        }
        // 菜单复位键（固定 HOME）：菜单被拖到屏幕外时按一下就能找回来
        {
            bool cur = (GetAsyncKeyState(VK_HOME) & 0x8000) != 0;
            if (cur && !lastReset) {
                Ui* ui = g_overlay.GetUi();
                if (ui) { ui->ResetWindow(); CursorShow(true); Log("热键 HOME: 菜单已复位到屏幕中央"); }
            }
            lastReset = cur;
        }
        // 鼠标左键状态 → 写给游戏内引擎（自瞄的"左键触发"）：只在变化时重写状态文件，避免刷盘
        {
            static bool lastLmb = false;
            bool cur = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            if (cur != lastLmb) {
                lastLmb = cur;
                SetInputLmb(cur);
                g_features.WriteFeaturesFile();
            }
        }
        // 跳跃键（空格）状态 → 写给引擎（喷气靴"按住才喷、松开就停"）：同样只在变化时写
        {
            static bool lastJump = false;
            bool cur = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
            if (cur != lastJump) {
                lastJump = cur;
                SetInputJump(cur);
                g_features.WriteFeaturesFile();
            }
        }
        for (size_t i = 0; i < g_patches.defs.size(); ++i) {
            UINT k = g_patches.defs[i].hotkey;
            if (!k) continue;
            bool cur = (GetAsyncKeyState((int)k) & 0x8000) != 0;
            if (cur && i < last.size() && !last[i]) g_patches.Toggle(i);
            if (i < last.size()) last[i] = cur;
        }
        // 功能热键（ini 里 feature.* 的 hotkey=）：动作型直接触发，开关型切换
        for (size_t i = 0; i < g_features.list.size(); ++i) {
            UINT k = g_features.list[i].hotkey;
            if (!k) continue;
            bool cur = (GetAsyncKeyState((int)k) & 0x8000) != 0;
            if (i < lastF.size()) {
                if (cur && !lastF[i]) {
                    if (g_features.list[i].ui == "action") g_features.Fire(i);
                    else g_features.Toggle(i);
                    Ui* ui2 = g_overlay.GetUi();
                    if (ui2) ui2->SetNote(L"热键已触发");
                }
                lastF[i] = cur;
            }
        }
        Sleep(25);
    }
}

static DWORD WINAPI InitThread(LPVOID) {
    std::wstring cfgPath = PickConfigPath();
    std::wstring logPath = cfgPath;
    size_t dot = logPath.find_last_of(L'.');
    if (dot != std::wstring::npos) logPath = logPath.substr(0, dot);
    logPath += L".log";
    LogInit(logPath.c_str());
    Log("==== 注入成功 PID=%u ====", GetCurrentProcessId());
    Log("配置文件: %s", Narrow(cfgPath).c_str());

    g_cfg.DefaultPath();
    g_cfg.Load(cfgPath);

    size_t msz = 0;
    uintptr_t mb = MainBase(&msz);
    Log("主模块基址=0x%llX 大小=%zu 字节", (unsigned long long)mb, msz);

    g_deps.sc = &g_scanner;
    g_deps.fz = &g_freezer;
    g_deps.pe = &g_patches;
    g_deps.uiScale = g_cfg.uiScale;
    g_deps.menuOpen = g_cfg.menuOpen;
    g_scanner.moduleOnly = g_cfg.moduleOnly;

    g_freezer.Start();
    g_patches.Load(std::move(g_cfg.patches));
    g_features.Load(cfgPath);
    {
        // 打通「菜单 → 游戏内引擎」：状态文件 + 引擎脚本 + 命令文件加载指令
        std::wstring dir  = GetExeDir();
        std::wstring feat = dir + L"FSDTrainer_features.txt";
        std::wstring cmdf = dir + L"FSDTrainer_cmd.txt";
        std::wstring lua  = dir + L"FSDTrainer_code.lua";
        DeployExecutorMod(dir);
        DeployLuaEngine(lua, cmdf);
        g_features.SetBridge(feat, cmdf, lua);
        g_features.WriteFeaturesFile();
        Log("功能状态文件: %s", Narrow(feat).c_str());
    }
    game::Init(g_cfg);

    if (!g_cfg.overlay) Log("配置已关闭覆盖层，仅热键补丁生效");
    else if (!g_overlay.Install()) Log("覆盖层安装失败（游戏可能未在 DX12/DX11 模式下运行）");

    // 自检：FSDTRAINER_SELFTEST=1 时验证“特征码生成 + 指针扫描”链路
    {
        wchar_t st[8] = L"";
        if (GetEnvironmentVariableW(L"FSDTRAINER_SELFTEST", st, 8) > 0) {
            std::thread([]() {
                Sleep(3000);
                g_scanner.FirstScan(VT_I32, (double)0x5A5A1234, false);
                while (g_scanner.busy.load()) Sleep(200);
                Log("自检：魔法值 0x5A5A1234 命中 %zu 条", g_scanner.Count());
                uintptr_t a = 0;
                { std::lock_guard<std::mutex> lk(g_scanner.mtx); if (!g_scanner.hits.empty()) a = g_scanner.hits[0].addr; }
                if (!a) { Log("自检：未命中，跳过特征码/指针检查"); return; }
                std::string sig = MakeSignature(a);
                Log("自检：特征码 = %.60s ...", sig.c_str());
                auto ptrs = FindPointersTo(a, 4);
                Log("自检：指向该地址的指针 %zu 个", ptrs.size());
                for (auto p : ptrs) Log("   0x%llX", (unsigned long long)p);
            }).detach();
        }
    }
    g_running = true;
    std::thread(HotkeyThread).detach();
    Log("初始化完成，按 %s 呼出菜单", HotkeyName(g_cfg.menuKey));
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        g_selfModule = hModule;
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        // 只置标志：真正的清理已在 KillTrainer 完成；在加载器锁内做阻塞操作易死锁并导致 DLL 无法卸载
        g_running = false;
        g_unloading = true;
        break;
    }
    return TRUE;
}
