#pragma once
#include "common.h"

namespace fsd {

    UINT ParseHotkey(const std::string& name);      // "VK_F1" / "F1" / "INSERT" -> VK 码，0 表示无效
    const char* HotkeyName(UINT vk);

    extern std::wstring g_configPathW;
    extern UINT g_killKey;          // “杀死修改器”热键（默认 END）
    extern UINT g_menuKey;          // 菜单热键（默认 INSERT）
    extern UINT g_restoreKey;       // “一键恢复游戏原状”热键（默认无）
    extern bool g_advanced;         // 是否显示高级页（扫描/冻结/补丁/命令/参考）

    struct PatchDef {
        std::string        name;
        std::string        pattern;              // AOB 特征码
        uint64_t           offset = 0;           // 命中地址 + offset
        std::vector<BYTE>  bytes;                // 要写入的字节
        std::vector<BYTE>  original;             // 原始字节（自动保存）
        UINT               hotkey = 0;
        std::string        hotkeyName;
        bool               enabled = false;      // 注入后是否默认开启
        // 运行时
        uintptr_t          addr = 0;
        bool               applied = false;
    };

    struct Config {
        UINT        menuKey      = VK_INSERT;    // 呼出/隐藏菜单
        UINT        killKey      = VK_END;       // 杀死修改器（卸载并恢复现场）
        UINT        restoreKey   = 0;            // 一键恢复游戏原状（关闭全部功能）
        bool        advanced     = false;        // 显示高级页
        UINT        scanKey      = 0;
        bool        overlay      = true;
        bool        menuOpen     = false;
        bool        logEnable    = true;
        bool        moduleOnly   = false;        // 扫描仅限主模块
        int         uiScale      = 100;          // UI 缩放百分比
        std::wstring logPath;
        std::wstring configPath;
        std::wstring processName = L"FSD-Win64-Shipping.exe";
        std::string  genginePattern;             // 可选：GEngine 指针特征
        std::string  execPattern;                // 可选：UEngine::Exec 特征
        std::vector<PatchDef> patches;

        bool Load(const std::wstring& path);
        void DefaultPath();
    };
}
