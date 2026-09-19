#pragma once
#include "common.h"
#include "scanner.h"
#include "patches.h"

namespace fsd {

    struct FrameInput {
        int   mx = -1, my = -1;
        bool  ldown = false;
        bool  lclick = false;      // 本帧按下
        bool  rclick = false;
        int   wheel = 0;
        std::vector<UINT>    keys;  // WM_KEYDOWN / WM_SYSKEYDOWN
        std::vector<wchar_t> chars;
    };

    class Ui {
    public:
        bool Init(int w, int h, int uiScale, Scanner* sc, Freezer* fz, PatchEngine* pe);
        void Resize(int w, int h);
        bool Build(const FrameInput& in);      // 返回 true 表示本帧已完成绘制（可清空输入队列）
        void Toggle();
        void ResetWindow();                            // 菜单复位（Home 键）
        void SetNote(const std::wstring& s) { note = s; }

        void* pixels = nullptr;      // BGRA 预乘
        int   width = 0, height = 0;
        int   viewW = 0, viewH = 0;  // 游戏视口尺寸（由 overlay 每帧填入，用于 ESP 坐标换算）
        bool  dirty = false;
        bool  visible = false;
        bool  needClear = false;
        bool  scanModuleOnly = false;
        bool  advanced = false;
        int   scale = 100;
        int   opacity = 100;         // 菜单整体不透明度（%），100=完全不透明（默认）

        bool  EspWanted();           // 菜单里"显示敌人 / 显示采集"是否开启

    private:
        HDC       memDC = nullptr, txtDC = nullptr;
        HBITMAP   memBmp = nullptr, txtBmp = nullptr;
        uint32_t* bits = nullptr;
        uint32_t* tbits = nullptr;
        HFONT     font = nullptr, fontBold = nullptr;

        Scanner*     sc = nullptr;
        Freezer*     fz = nullptr;
        PatchEngine* pe = nullptr;
        const FrameInput* in = nullptr;

        int          tab = 0;
        ValType      vtype = VT_I32;
        CmpMode      cmp = CMP_EXACT;
        std::wstring scanValue = L"100";
        std::wstring writeValue = L"100";
        std::wstring cmdValue = L"stat fps";
        int          activeInput = -1;
        int          selected = -1;
        int          scroll = 0;
        int          fscroll = 0;
        int          winPosX = -1, winPosY = -1;   // 菜单左上角（posInit=false 时居中）
        int          winSizeW = 0, winSizeH = 0;   // 用户拖边框设定的大小（0=按屏幕自动）
        int          resizeZone = 0;               // 正在拖哪条边：0=没有
        int          rzW0 = 0, rzH0 = 0, rzX0 = 0, rzY0 = 0, rzMX0 = 0, rzMY0 = 0;
        int          gscroll = 0, gscrollMax = 0;   // 功能页滚动
        bool         posInit = false;
        int          wizAmmoStep = 0;              // 保留字段（向导已移除）
        int          wizGodStep = 0;
        int          catSel = 0;                   // 当前分类页
        std::vector<std::wstring> valBuf;          // 数值型功能的输入缓存
        std::wstring sigText;                      // 特征码/指针扫描结果
        bool         dragging = false;
        int          dragGrabX = 0, dragGrabY = 0;
        ULONGLONG    lastDraw = 0;
        std::vector<wchar_t> qchars;
        std::vector<UINT>    qkeys;
        bool                 inSelAll = false;      // 输入框：当前内容是否处于"全选"状态（打字即替换）
        int                  inCursor = 0;          // 输入框：光标位置（可像记事本一样左右移动后再插入）
        int                  pendingFocus = -1;     // Tab 跳到下一个输入框用（下一行的输入框 id）
        bool                 tabPressed = false;    // 本帧在输入框里按了 Tab
        std::wstring note;           // 上一次操作反馈

        int  S(int v) const { return MulDiv(v, scale, 100); }
        BYTE Aa(BYTE a) const { int v = (int)a * opacity / 100; return (BYTE)(v < 0 ? 0 : (v > 255 ? 255 : v)); }
        int  LineH() const { return S(24); }
        void MakeGdi(int w, int h);
        void FreeGdi();
        void RecreateFonts();
        void ClearAll();
        void DumpBmp();
        void MaybeDump();           // 调试：FSDTRAINER_DUMP_UI=1 时周期性导出界面

        void Fill(int x, int y, int w, int h, COLORREF c, BYTE alpha = 255);
        void Border(int x, int y, int w, int h, COLORREF c, BYTE a = 255);
        void Text(int x, int y, const std::wstring& s, COLORREF c, BYTE a = 255, bool bold = false);
        void TextF(int x, int y, COLORREF c, const wchar_t* fmt, ...);
        int  TextW(const std::wstring& s, bool bold = false);

        bool Hit(int x, int y, int w, int h) const;
        bool Hover(int x, int y, int w, int h) const { return Hit(x, y, w, h); }
        bool Button(int x, int y, int w, int h, const std::wstring& label, bool active = false, bool enabled = true);
        bool Checkbox(int x, int y, const std::wstring& label, bool& value);
        bool Slider(int id, int x, int y, int w, int h, double& v, double lo, double hi);
        bool InputBox(int id, int x, int y, int w, int h, std::wstring& buf, bool numeric, bool dec1 = false);
        void Progress(int x, int y, int w, int h, int pct);

        void PageGame(int x, int y, int w, int h, const std::string& cat);   // 游戏功能页（常规/武器道具/…）
        void PageScan(int x, int y, int w, int h);
        void PageFreeze(int x, int y, int w, int h);
        void PagePatch(int x, int y, int w, int h);
        void PageConsole(int x, int y, int w, int h);
        void PageFeatures(int x, int y, int w, int h);
        void PageSettings(int x, int y, int w, int h);

        // ESP：读游戏内 Lua 算好的屏幕包围盒并画框（v55：E/M 敌人矿物、T 队友、R 雷达点）
        struct EspDot { float x1 = 0, y1 = 0, x2 = 0, y2 = 0, d = 0; float hp = -1, sh = -1; int down = 0;
                        wchar_t kind = L'E'; std::wstring name; };
        std::vector<EspDot> espDots;
        ULONGLONG espReadAt = 0, espFileTime = 0;
        bool      espDirty = false;
        void LoadEsp();
        void DrawEsp();

        // 自瞄叠层（功能表 aim_show_range=1 时）：读 FSDTrainer_aim.txt，画索敌圈（R 行，圈上的点）与
        // 当前锁定目标（A 行，黄框 + 名字/距离）。故意和 ESP 用两个文件，避免两者互相覆盖。
        std::vector<EspDot> aimDots;
        ULONGLONG aimReadAt = 0, aimFileTime = 0;
        ULONGLONG aimDrawAt = 0;                 // 菜单隐藏时白圈的重画心跳
        bool      aimDirty = false;
        bool AimWanted();
        void LoadAim();
        void DrawAim();
    };
}
