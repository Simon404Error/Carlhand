#include "patches.h"
#include "scanner.h"
#include "mem.h"
#include "log.h"

namespace fsd {

    // 左键状态（由 dllmain 的输入线程更新），随功能状态文件一起写给游戏内引擎
    static bool g_inputLmb = false;
    void SetInputLmb(bool down) { g_inputLmb = down; }
    // 跳跃键（空格）状态：喷气靴"按住才喷、松开就停"要用
    static bool g_inputJump = false;
    void SetInputJump(bool down) { g_inputJump = down; }

    void PatchEngine::Load(std::vector<PatchDef>&& src) {
        defs = std::move(src);
        for (auto& p : defs) {
            p.addr = 0;
            uintptr_t hit = FindPatternInMain(p.pattern.c_str());
            if (!hit) { Log("补丁[%s]: 特征码未命中", p.name.c_str()); continue; }
            p.addr = hit + (uintptr_t)p.offset;
            p.original.resize(p.bytes.size());
            if (!MemRead(p.addr, p.original.data(), p.original.size())) {
                Log("补丁[%s]: 读取原字节失败 @%p", p.name.c_str(), (void*)p.addr);
                p.addr = 0; continue;
            }
            Log("补丁[%s]: 已定位 @%p", p.name.c_str(), (void*)p.addr);
            if (p.enabled) Apply((size_t)(&p - defs.data()));
        }
    }

    bool PatchEngine::Apply(size_t idx) {
        if (idx >= defs.size()) return false;
        auto& p = defs[idx];
        if (!p.addr) { err = "未定位"; return false; }
        if (!MemWrite(p.addr, p.bytes.data(), p.bytes.size())) { err = "写入失败"; return false; }
        p.applied = true;
        Log("补丁[%s] 已开启", p.name.c_str());
        return true;
    }

    bool PatchEngine::Revert(size_t idx) {
        if (idx >= defs.size()) return false;
        auto& p = defs[idx];
        if (!p.addr || p.original.empty()) { err = "无原始字节"; return false; }
        if (!MemWrite(p.addr, p.original.data(), p.original.size())) { err = "写入失败"; return false; }
        p.applied = false;
        Log("补丁[%s] 已关闭", p.name.c_str());
        return true;
    }

    bool PatchEngine::Toggle(size_t idx) {
        if (idx >= defs.size()) return false;
        return defs[idx].applied ? Revert(idx) : Apply(idx);
    }

    void PatchEngine::RevertAll() {
        for (size_t i = 0; i < defs.size(); ++i) if (defs[i].applied) Revert(i);
    }

    size_t PatchEngine::ActiveCount() {
        size_t n = 0;
        for (auto& p : defs) if (p.applied) ++n;
        return n;
    }

    // 一键恢复游戏原状：撤销全部补丁 + 解除全部冻结并写回原值
    void RestoreGameState() {
        g_patches.RevertAll();
        g_freezer.RestoreAll();
        Log("已执行一键恢复：补丁全部撤销、冻结全部解除并写回原值");
    }

    // ---------------- 功能面板 ----------------
    static std::vector<BYTE> ParseBytesHex(const std::string& s) {
        std::vector<BYTE> out;
        const char* p = s.c_str();
        while (*p) {
            while (*p == ' ' || *p == ',' || *p == '\t') ++p;
            if (!*p) break;
            char* end = nullptr;
            long v = strtol(p, &end, 16);
            if (end == p) break;
            out.push_back((BYTE)(v & 0xFF));
            p = end;
        }
        return out;
    }

    bool FeatureTable::Load(const std::wstring& iniPath) {
        list.clear();
        wchar_t secs[16384] = L"";
        DWORD n = GetPrivateProfileSectionNamesW(secs, 16384, iniPath.c_str());
        for (wchar_t* s = secs; n && *s; s += wcslen(s) + 1) {
            if (wcsncmp(s, L"feature.", 8) != 0) continue;
            Feature f;
            char tmp[512] = "";
            WideCharToMultiByte(CP_UTF8, 0, s + 8, -1, tmp, sizeof(tmp), nullptr, nullptr);
            f.name = tmp;
            auto Get = [&](const wchar_t* key, const wchar_t* def) -> std::string {
                wchar_t buf[1024] = L"";
                GetPrivateProfileStringW(s, key, def, buf, 1024, iniPath.c_str());
                char a[1024] = "";
                WideCharToMultiByte(CP_UTF8, 0, buf, -1, a, sizeof(a), nullptr, nullptr);
                return std::string(a);
            };
            f.cat = Get(L"cat", L"未分类");
            f.kind = Get(L"kind", L"patch");
            f.ui = Get(L"ui", L"toggle");
            f.unit = Get(L"unit", L"");
            f.def = atof(Get(L"def", L"1").c_str());
            f.cur = f.def;
            // 开关型（ui=toggle）的"开/关"状态存在 f.on 上（状态文件写的就是它），
            // 所以 ini 里 def=1 的开关要在加载时就置成"开"，否则界面/引擎都当成关。
            if (f.ui == "toggle" && f.kind == "cheat") f.on = (f.def != 0.0);
            f.mult = GetPrivateProfileIntW(s, L"mult", 0, iniPath.c_str()) != 0;
            f.clientOnly = GetPrivateProfileIntW(s, L"client", 0, iniPath.c_str()) != 0;
            f.sealed = GetPrivateProfileIntW(s, L"sealed", 0, iniPath.c_str()) != 0;
            f.lo = atof(Get(L"lo", L"0").c_str());
            f.hi = atof(Get(L"hi", L"1").c_str());
            f.pattern = Get(L"pattern", L"");
            f.offset = (uint64_t)GetPrivateProfileIntW(s, L"offset", 0, iniPath.c_str());
            f.bytes = ParseBytesHex(Get(L"bytes", L""));
            f.hotkeyName = Get(L"hotkey", L"");
            f.hotkey = ParseHotkey(f.hotkeyName);
            f.key = Get(L"key", L"");
            std::string tp = Get(L"type", L"i32");
            f.type = tp == "f32" ? VT_F32 : (tp == "f64" ? VT_F64 : (tp == "i64" ? VT_I64 : VT_I32));
            f.value = atof(Get(L"value", L"0").c_str());
            f.addr = (uintptr_t)_strtoui64(Get(L"addr", L"0").c_str(), nullptr, 16);
            {
                wchar_t wbuf[1024] = L"";
                GetPrivateProfileStringW(s, L"cmd", L"", wbuf, 1024, iniPath.c_str());
                f.cmd = wbuf;
            }
            // 定位 / 读取原字节
            if (f.kind == "patch") {
                if (f.pattern.empty() || f.bytes.empty()) { list.push_back(f); continue; }
                uintptr_t hit = FindPatternInMain(f.pattern.c_str());
                if (hit) {
                    f.addr = hit + (uintptr_t)f.offset;
                    f.original.resize(f.bytes.size());
                    if (!MemRead(f.addr, f.original.data(), f.original.size())) f.addr = 0;
                }
            } else if (f.kind == "freeze" && f.addr) {
                double cur = 0;
                if (ReadTypedValue(f.addr, f.type, cur)) { f.orig = cur; f.hasOrig = true; }
            }
            list.push_back(f);
        }
        Log("功能面板载入: %zu 项", list.size());
        return true;
    }

    bool FeatureTable::Save(const std::wstring& iniPath) const {
        // 把 addr 与开关状态写回 ini，保证“绑定一次，之后一键可用”
        for (auto& f : list) {
            std::wstring sec = L"feature.";
            {
                wchar_t w[512] = L"";
                MultiByteToWideChar(CP_UTF8, 0, f.name.c_str(), -1, w, 512);
                sec += w;
            }
            wchar_t hex[32] = L"";
            swprintf_s(hex, L"%llX", (unsigned long long)f.addr);
            WritePrivateProfileStringW(sec.c_str(), L"addr", hex, iniPath.c_str());
        }
        return true;
    }

    bool FeatureTable::Toggle(size_t idx) {
        if (idx >= list.size()) return false;
        Feature& f = list[idx];
        if (f.kind == "cheat") {
            f.on = !f.on;
            WriteFeaturesFile();
            Log("功能[%s] %s（已同步给游戏内引擎）", f.name.c_str(), f.on ? "已开启" : "已关闭，游戏状态已恢复");
            return true;
        }
        if (f.kind == "patch") {
            if (!f.addr) { Log("功能[%s]: 特征码未命中或未配置", f.name.c_str()); return false; }
            if (f.on) {
                if (!f.original.empty()) MemWrite(f.addr, f.original.data(), f.original.size());
                f.on = false; Log("功能[%s] 已关闭（写回原字节）", f.name.c_str());
            } else {
                if (MemWrite(f.addr, f.bytes.data(), f.bytes.size())) { f.on = true; Log("功能[%s] 已开启", f.name.c_str()); }
            }
            return true;
        }
        if (f.kind == "freeze") {
            if (!f.addr) { Log("功能[%s]: 未绑定地址", f.name.c_str()); return false; }
            if (f.on) {
                g_freezer.DisableAddr(f.addr);            // 解除锁定并写回原值
                f.on = false; Log("功能[%s] 已关闭（写回原值）", f.name.c_str());
            } else {
                double cur = 0;
                if (!f.hasOrig && ReadTypedValue(f.addr, f.type, cur)) { f.orig = cur; f.hasOrig = true; }
                double target = (f.value != 0.0) ? f.value : (f.hasOrig ? f.orig : 0.0);   // value=0 表示锁定原值
                f.value = target;
                g_freezer.Set(f.addr, f.type, f.value);   // 交给冻结器周期写回
                f.on = true; Log("功能[%s] 已开启（锁定 %.7g）", f.name.c_str(), f.value);
            }
            return true;
        }
        if (f.kind == "cmd") {
            if (f.cmd.empty()) return false;
            if (f.on) { f.on = false; return true; }
            f.on = game::ExecCommand(f.cmd.c_str());
            Log("功能[%s] 命令执行: %s", f.name.c_str(), f.on ? "成功" : "失败");
            return true;
        }
        return false;
    }

    // 回归善良人格：开关全关 + 数值型全部恢复默认值（否则输入框还留着旧值、游戏里也还在生效）
    void FeatureTable::AllOff() {
        for (size_t i = 0; i < list.size(); ++i) {
            Feature& f = list[i];
            if (f.kind == "cheat") {
                f.cur = f.def;          // 数值型/动作型都回到默认
                f.on = false;
                continue;
            }
            if (f.on) Toggle(i);
        }
        WriteFeaturesFile();
        Log("已回归善良人格：所有开关关闭、数值恢复默认");
    }

    void FeatureTable::SetBridge(const std::wstring& featuresPath, const std::wstring& cmdFile, const std::wstring& luaFile) {
        featPath = featuresPath; cmdFilePath = cmdFile; luaFilePath = luaFile;
    }

    bool FeatureTable::Fire(size_t idx) {
        if (idx >= list.size()) return false;
        Feature& f = list[idx];
        if (f.kind == "cheat") {
            f.cur = (f.cur < 1.0) ? 1.0 : f.cur + 1.0;   // 动作型：每次点击都变化，游戏内引擎按变化触发
            f.on = true;
            WriteFeaturesFile();
            Log("功能[%s] 已触发（第 %.0f 次）", f.name.c_str(), f.cur);
            return true;
        }
        return false;
    }

    // 把全部 cheat 型功能写成 <游戏目录>\...\Win64\FSDTrainer_features.txt（UTF-8 无 BOM）
    bool FeatureTable::WriteFeaturesFile() {
        if (featPath.empty()) return false;
        std::string body = "# FSDTrainer 功能状态（菜单写入，游戏内引擎每 300ms 读取）\r\n";
        char line[512] = "";
        for (auto& f : list) {
            if (f.kind != "cheat" || f.key.empty()) continue;
            if (f.ui == "value") sprintf_s(line, "%s=%g\r\n", f.key.c_str(), f.cur);
            else if (f.ui == "action")                            // 动作型：没点过就是 0，点过是次数（引擎按变化触发）
                sprintf_s(line, "%s=%g\r\n", f.key.c_str(), f.on ? f.cur : 0.0);
            else sprintf_s(line, "%s=%d\r\n", f.key.c_str(), f.on ? 1 : 0);
            body += line;
        }
        // 鼠标左键状态（自瞄"左键触发"用）：不是功能项，只是借这个文件传给游戏内引擎
        body += (g_inputLmb ? "lmb=1\r\n" : "lmb=0\r\n");
        body += (g_inputJump ? "jmp=1\r\n" : "jmp=0\r\n");
        FILE* fp = nullptr;
        if (_wfopen_s(&fp, featPath.c_str(), L"wb") != 0 || !fp) { Log("状态文件写入失败: %s", Narrow(featPath).c_str()); return false; }
        fwrite(body.data(), 1, body.size(), fp);
        fclose(fp);
        return true;
    }

    bool FeatureTable::BindAddress(size_t idx, uintptr_t addr, ValType t, double cur) {
        if (idx >= list.size()) return false;
        Feature& f = list[idx];
        f.kind = "freeze";
        f.addr = addr;
        f.type = t;
        f.orig = cur;
        f.hasOrig = true;
        // 默认锁定成当前值：血量类保持当前值，弹药类可后续在 ini 里改 value
        f.value = cur;
        Log("功能[%s] 已绑定地址 0x%llX（类型 %d, 当前值 %.7g）", f.name.c_str(), (unsigned long long)addr, (int)t, cur);
        return true;
    }

    // value 型：写入/锁定“倍率 × 原始值”（mult=1）或直接写入数值
    bool FeatureTable::SetValue(size_t idx, double v) {
        if (idx >= list.size()) return false;
        Feature& f = list[idx];
        f.cur = v;
        if (f.kind == "cheat") {
            f.on = (v != f.def);
            WriteFeaturesFile();
            Log("功能[%s] = %.6g（已同步给游戏内引擎）", f.name.c_str(), v);
            return true;
        }
        if (!f.addr) { Log("功能[%s]: 未绑定地址（先扫描并选中，再点该功能绑定）", f.name.c_str()); return false; }
        double target = v;
        if (f.mult) {
            double base = f.orig;
            if (!f.hasOrig && ReadTypedValue(f.addr, f.type, base)) { f.orig = base; f.hasOrig = true; }
            target = base * v;
        }
        if (f.kind == "freeze") g_freezer.Set(f.addr, f.type, target);   // 周期写回
        else WriteTypedValue(f.addr, f.type, target);
        f.on = true;
        Log("功能[%s] 应用 %.4g -> 写入 %.7g", f.name.c_str(), v, target);
        return true;
    }

    bool FeatureTable::ResetValue(size_t idx) {
        if (idx >= list.size()) return false;
        Feature& f = list[idx];
        f.cur = f.def;
        if (f.kind == "cheat") {
            f.on = false;
            WriteFeaturesFile();
            Log("功能[%s] 已恢复默认（%.6g，已同步给游戏内引擎）", f.name.c_str(), f.def);
            return true;
        }
        if (!f.addr) return false;
        if (f.kind == "freeze") g_freezer.DisableAddr(f.addr);
        if (f.hasOrig) WriteTypedValue(f.addr, f.type, f.orig);
        f.on = false;
        Log("功能[%s] 已恢复默认（写回原始值 %.7g）", f.name.c_str(), f.orig);
        return true;
    }

    size_t FeatureTable::OnCount() {
        size_t n = 0;
        for (auto& f : list) if (f.on) ++n;
        return n;
    }

    std::vector<std::string> FeatureTable::Categories() const {
        std::vector<std::string> cats;
        for (auto& f : list) {
            bool has = false;
            for (auto& c : cats) if (c == f.cat) { has = true; break; }
            if (!has) cats.push_back(f.cat);
        }
        return cats;
    }

    // UE 控制台（可选功能，需在 ini 中配置特征码）
    namespace game {
        static uintptr_t g_genginePtr = 0;     // 存放 UEngine* 的地址
        static uintptr_t g_execFn = 0;
        static std::wstring g_last;

        static uintptr_t ResolveRip(uintptr_t at) {
            // 形如 48 8B 0D ?? ?? ?? ?? ：读取 rip 相对偏移
            int32_t disp = 0;
            if (!MemRead(at + 3, &disp, 4)) return 0;
            uintptr_t target = at + 7 + disp;
            uintptr_t val = 0;
            if (!MemRead(target, &val, 8)) return 0;
            return val;
        }

        void Init(const Config& cfg) {
            g_genginePtr = 0; g_execFn = 0;
            if (!cfg.genginePattern.empty()) {
                uintptr_t hit = FindPatternInMain(cfg.genginePattern.c_str());
                if (hit) {
                    g_genginePtr = hit;                  // 命中处即读取点
                    uintptr_t eng = ResolveRip(hit);
                    Log("GEngine 定位: 读取点=%p 实例=%p", (void*)hit, (void*)eng);
                } else Log("GEngine 特征码未命中");
            }
            if (!cfg.execPattern.empty()) {
                g_execFn = FindPatternInMain(cfg.execPattern.c_str());
                Log("UEngine::Exec 定位=%p", (void*)g_execFn);
            }
        }

        bool ConsoleReady() { return g_genginePtr != 0 && g_execFn != 0; }

        typedef int(__fastcall* ExecFn)(void* self, void* world, const wchar_t* cmd, void* ar);

        bool ExecCommand(const wchar_t* cmd) {
            if (!ConsoleReady()) { g_last = L"未配置 gengine_pattern / exec_pattern"; return false; }
            uintptr_t eng = 0;
            if (!MemRead(g_genginePtr, &eng, 8) || !eng) { g_last = L"GEngine 实例为空"; return false; }
            uint8_t ar[0x200] = { 0 };               // 哑 FOutputDevice（只需不崩）
            bool ok = false;
            __try {
                ExecFn fn = (ExecFn)g_execFn;
                ok = fn((void*)eng, nullptr, cmd, ar) != 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                g_last = L"执行时发生异常（偏移/签名可能不匹配当前版本）";
                Log("Exec 命令异常");
                return false;
            }
            g_last = ok ? L"已执行" : L"已发送（返回 false，命令可能不存在）";
            Log("Exec: ok=%d", (int)ok);
            return ok;
        }

        std::wstring LastConsoleResult() { return g_last; }
    }
}
