#include "log.h"
#include <cstdarg>

namespace fsd {
    static FILE*      g_fp = nullptr;
    static std::mutex g_mtx;
    static wchar_t    g_path[MAX_PATH] = L"";

    void LogInit(const wchar_t* logPath) {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (logPath) {
            wcsncpy_s(g_path, logPath, _TRUNCATE);
            // 允许其他进程读取日志（游戏运行时也能查看）
            g_fp = _fsopen(Narrow(std::wstring(g_path)).c_str(), "a+, ccs=UTF-8", _SH_DENYWR);
        }
        if (g_fp) {
            SYSTEMTIME st; GetLocalTime(&st);
            fwprintf(g_fp, L"[%02d:%02d:%02d] ===== 卡尔的大手 (Carlhand) %s 启动 =====\n",
                     st.wHour, st.wMinute, st.wSecond, FSDTRAINER_VER);
            fflush(g_fp);
        }
    }

    const wchar_t* LogPath() { return g_path; }

    void LogClose() {
        std::lock_guard<std::mutex> lk(g_mtx);
        if (g_fp) { fflush(g_fp); fclose(g_fp); g_fp = nullptr; }
    }

    std::string Narrow(const std::wstring& s) {
        if (s.empty()) return std::string();
        int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
        if (n <= 0) return std::string();
        std::string out((size_t)n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n, nullptr, nullptr);
        return out;
    }

    static std::wstring WidenUtf8(const char* s) {
        if (!s || !*s) return std::wstring();
        int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
        if (n <= 0) return std::wstring();
        std::wstring out((size_t)n - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s, -1, &out[0], n);
        return out;
    }

    void Log(const char* fmt, ...) {
        char buf[2048] = "";
        va_list ap; va_start(ap, fmt);
        _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
        va_end(ap);
        OutputDebugStringA("[FSDTrainer] "); OutputDebugStringA(buf); OutputDebugStringA("\n");

        std::lock_guard<std::mutex> lk(g_mtx);
        if (g_fp) {
            SYSTEMTIME st; GetLocalTime(&st);
            // 源码为 UTF-8（/utf-8），统一转宽字符后按 UTF-8 文件写入
            std::wstring w = WidenUtf8(buf);
            fwprintf(g_fp, L"[%02d:%02d:%02d] %s\n", st.wHour, st.wMinute, st.wSecond, w.c_str());
            fflush(g_fp);
        }
    }
}
