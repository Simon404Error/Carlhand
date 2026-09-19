#include "config.h"
#include "log.h"

namespace fsd {

    std::wstring g_configPathW;
    UINT         g_killKey = VK_END;
    UINT         g_menuKey = VK_INSERT;
    UINT         g_restoreKey = 0;
    bool         g_advanced = false;

    static const struct { const char* n; UINT vk; } kKeys[] = {
        {"VK_LBUTTON",VK_LBUTTON},{"VK_RBUTTON",VK_RBUTTON},{"VK_MBUTTON",VK_MBUTTON},
        {"VK_BACK",VK_BACK},{"VK_TAB",VK_TAB},{"VK_RETURN",VK_RETURN},{"VK_ESCAPE",VK_ESCAPE},
        {"VK_SPACE",VK_SPACE},{"VK_PRIOR",VK_PRIOR},{"VK_NEXT",VK_NEXT},{"VK_END",VK_END},
        {"VK_HOME",VK_HOME},{"VK_LEFT",VK_LEFT},{"VK_UP",VK_UP},{"VK_RIGHT",VK_RIGHT},
        {"VK_DOWN",VK_DOWN},{"VK_INSERT",VK_INSERT},{"VK_DELETE",VK_DELETE},
        {"VK_NUMPAD0",VK_NUMPAD0},{"VK_NUMPAD1",VK_NUMPAD1},{"VK_NUMPAD2",VK_NUMPAD2},
        {"VK_NUMPAD3",VK_NUMPAD3},{"VK_NUMPAD4",VK_NUMPAD4},{"VK_NUMPAD5",VK_NUMPAD5},
        {"VK_NUMPAD6",VK_NUMPAD6},{"VK_NUMPAD7",VK_NUMPAD7},{"VK_NUMPAD8",VK_NUMPAD8},
        {"VK_NUMPAD9",VK_NUMPAD9},{"VK_MULTIPLY",VK_MULTIPLY},{"VK_ADD",VK_ADD},
        {"VK_SUBTRACT",VK_SUBTRACT},{"VK_DIVIDE",VK_DIVIDE},
        {"VK_OEM_1",VK_OEM_1},{"VK_OEM_2",VK_OEM_2},{"VK_OEM_3",VK_OEM_3},
        {"VK_OEM_4",VK_OEM_4},{"VK_OEM_5",VK_OEM_5},{"VK_OEM_6",VK_OEM_6},{"VK_OEM_7",VK_OEM_7},
    };

    UINT ParseHotkey(const std::string& raw) {
        if (raw.empty()) return 0;
        std::string s;
        for (char c : raw) s.push_back((char)toupper((unsigned char)c));
        if (s == "NONE" || s == "0") return 0;
        for (auto& k : kKeys) if (s == k.n) return k.vk;
        if (s.size() >= 2 && s[0] == 'F') {           // F1..F24
            int n = atoi(s.c_str() + 1);
            if (n >= 1 && n <= 24) return VK_F1 + (n - 1);
        }
        if (s.rfind("VK_", 0) == 0) s = s.substr(3);
        if (s.size() == 1) {                          // 单字符 A-Z / 0-9
            char c = s[0];
            if (c >= 'A' && c <= 'Z') return (UINT)c;
            if (c >= '0' && c <= '9') return (UINT)c;
        }
        if (_stricmp(s.c_str(), "OEM_TILDE") == 0) return VK_OEM_3;
        return 0;
    }

    const char* HotkeyName(UINT vk) {
        static char buf[24];
        for (auto& k : kKeys) if (vk == k.vk) return k.n + 3;
        if (vk >= VK_F1 && vk <= VK_F24) { sprintf_s(buf, "F%u", vk - VK_F1 + 1); return buf; }
        if (vk >= 'A' && vk <= 'Z') { sprintf_s(buf, "%c", (char)vk); return buf; }
        if (vk >= '0' && vk <= '9') { sprintf_s(buf, "%c", (char)vk); return buf; }
        sprintf_s(buf, "0x%X", vk);
        return buf;
    }

    static std::string IniStr(const std::wstring& file, const wchar_t* sec, const wchar_t* key, const wchar_t* def = L"") {
        wchar_t buf[2048] = L"";
        GetPrivateProfileStringW(sec, key, def, buf, 2048, file.c_str());
        char a[2048] = "";
        WideCharToMultiByte(CP_UTF8, 0, buf, -1, a, sizeof(a), nullptr, nullptr);
        return std::string(a);
    }
    static int IniInt(const std::wstring& file, const wchar_t* sec, const wchar_t* key, int def) {
        return (int)GetPrivateProfileIntW(sec, key, def, file.c_str());
    }

    static std::vector<BYTE> ParseBytes(const std::string& s) {
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

    void Config::DefaultPath() {
        wchar_t exe[MAX_PATH] = L"";
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring dir(exe);
        size_t pos = dir.find_last_of(L"\\/");
        if (pos != std::wstring::npos) dir = dir.substr(0, pos + 1);
        configPath = dir + L"FSDTrainer.ini";
    }

    bool Config::Load(const std::wstring& path) {
        configPath = path;
        g_configPathW = path;
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            Log("配置不存在: %S", path.c_str());
            return false;
        }
        menuKey = ParseHotkey(IniStr(path, L"general", L"menu_key", L"VK_INSERT"));
        if (!menuKey) menuKey = VK_INSERT;
        killKey = ParseHotkey(IniStr(path, L"general", L"kill_key", L"VK_END"));
        g_killKey = killKey;
        g_menuKey = menuKey;
        restoreKey = ParseHotkey(IniStr(path, L"general", L"restore_key", L""));
        g_restoreKey = restoreKey;
        advanced = IniInt(path, L"general", L"advanced", 1) != 0;   // 默认显示高级页（扫描/冻结）
        g_advanced = advanced;
        overlay    = IniInt(path, L"general", L"overlay", 1) != 0;
        menuOpen   = IniInt(path, L"general", L"menu_open", 0) != 0;
        logEnable  = IniInt(path, L"general", L"log", 1) != 0;
        moduleOnly = IniInt(path, L"general", L"scan_module_only", 0) != 0;
        uiScale    = IniInt(path, L"general", L"ui_scale", 100);
        if (uiScale < 50) uiScale = 50; if (uiScale > 250) uiScale = 250;

        genginePattern = IniStr(path, L"console", L"gengine_pattern");
        execPattern    = IniStr(path, L"console", L"exec_pattern");

        wchar_t secs[8192] = L"";
        DWORD n = GetPrivateProfileSectionNamesW(secs, 8192, path.c_str());
        for (wchar_t* s = secs; n && *s; s += wcslen(s) + 1) {
            if (wcsncmp(s, L"patch.", 6) != 0) continue;
            PatchDef p;
            p.name = "patch";
            char tmp[512] = "";
            WideCharToMultiByte(CP_UTF8, 0, s + 6, -1, tmp, sizeof(tmp), nullptr, nullptr);
            p.name = tmp;
            p.pattern    = IniStr(path, s, L"pattern");
            p.offset     = (uint64_t)IniInt(path, s, L"offset", 0);
            p.bytes      = ParseBytes(IniStr(path, s, L"bytes"));
            p.hotkeyName = IniStr(path, s, L"hotkey");
            p.hotkey     = ParseHotkey(p.hotkeyName);
            p.enabled    = IniInt(path, s, L"enabled", 0) != 0;
            if (!p.pattern.empty() && !p.bytes.empty()) {
                patches.push_back(p);
                Log("载入补丁 [%s] key=%s bytes=%zu", p.name.c_str(), p.hotkeyName.c_str(), p.bytes.size());
            }
        }
        Log("配置载入完成: %S 菜单键=%s 补丁数=%zu", path.c_str(), HotkeyName(menuKey), patches.size());
        return true;
    }
}
