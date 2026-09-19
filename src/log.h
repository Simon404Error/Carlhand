#pragma once
#include "common.h"

namespace fsd {
    void  LogInit(const wchar_t* logPath);
    void  Log(const char* fmt, ...);
    void  LogClose();
    const wchar_t* LogPath();

    // 宽字符串 -> UTF-8 窄字符串（用于日志输出）
    std::string Narrow(const std::wstring& s);
}
