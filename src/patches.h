#pragma once
#include "config.h"
#include "scanner.h"

namespace fsd {
    class PatchEngine {
    public:
        std::vector<PatchDef> defs;
        void Load(std::vector<PatchDef>&& src);
        bool Toggle(size_t idx);            // 开/关，返回新状态
        bool Apply(size_t idx);
        bool Revert(size_t idx);
        void RevertAll();                   // 全部关闭并写回原始字节（恢复游戏原状）
        size_t ActiveCount();
        const char* LastError() const { return err.c_str(); }
    private:
        std::string err;
    };

    // ---------------- 功能面板（点一下就触发，关掉即恢复原状）----------------
    // kind = patch ：AOB 特征码 + 字节覆盖（关掉写回原字节）
    // kind = freeze：对某个地址持续写回数值（关掉写回原值）
    // kind = cmd   ：执行 UE 控制台命令
    struct Feature {
        std::string        name;      // 显示名
        std::string        cat;       // 页面分类（常规/武器道具/…）
        std::string        kind;      // 机制：patch | freeze | cmd
        std::string        ui;        // 界面形态：value | toggle | action
        std::string        unit;      // 单位文字（倍率 / m / 个身位…）
        double             def = 1;   // value 型默认值（恢复默认用）
        double             cur = 1;   // value 型当前值
        bool               mult = false;   // 倍率型：写入 = 原始值 × 倍率
        double             lo = 0, hi = 1;   // slider 型的取值范围
        bool               clientOnly = false;  // 客机（非主机）时无效 → 名字前加 *
        // patch
        std::string        pattern;
        uint64_t           offset = 0;
        std::vector<BYTE>  bytes;
        std::vector<BYTE>  original;
        uintptr_t          addr = 0;
        // freeze
        ValType            type = VT_I32;
        double             value = 0;
        double             orig = 0;
        bool               hasOrig = false;
        // cmd
        std::wstring       cmd;
        // cheat：游戏内引擎状态文件里的键名（kind=cheat 时使用）
        std::string        key;
        // 运行时
        bool               on = false;
        bool               sealed = false;   // 封印：界面灰掉、不可点
        UINT               hotkey = 0;
        std::string        hotkeyName;
    };

    class FeatureTable {
    public:
        std::vector<Feature> list;
        bool Load(const std::wstring& iniPath);
        bool Save(const std::wstring& iniPath) const;
        bool Toggle(size_t idx);                    // 开关（关闭时恢复原状）
        void AllOff();
        bool SetValue(size_t idx, double v);        // value 型：写入/锁定倍率或数值
        bool ResetValue(size_t idx);                // value 型：恢复默认（原始值 × 默认倍率）
        bool BindAddress(size_t idx, uintptr_t addr, ValType t, double cur);
        bool Fire(size_t idx);                      // action 型：触发一次（cheat 型写状态文件）
        size_t OnCount();
        std::vector<std::string> Categories() const;
        // 与游戏内 Lua 引擎的桥：状态文件 / 命令文件 / 引擎脚本
        void SetBridge(const std::wstring& featuresPath, const std::wstring& cmdFilePath, const std::wstring& luaFilePath);
        bool WriteFeaturesFile();                   // 把全部 cheat 型功能写入状态文件（游戏内每 300ms 读取）
    private:
        std::wstring featPath, cmdFilePath, luaFilePath;
    };

    extern FeatureTable g_features;     // 功能面板表（全局）
    extern Freezer      g_freezer;      // 冻结器（全局，定义在 dllmain.cpp）
    extern PatchEngine  g_patches;      // 补丁引擎（全局）

    // UE 控制台（可选功能，需在 ini 中配置特征码）
    namespace game {
        void Init(const Config& cfg);
        bool ConsoleReady();
        bool ExecCommand(const wchar_t* cmd);
        std::wstring LastConsoleResult();
    }
}
