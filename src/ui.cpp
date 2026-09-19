#include "ui.h"
#include "overlay.h"
#include "mem.h"
#include "log.h"
#include <cstdarg>
#include <cmath>
#include <cstdlib>

namespace fsd {

    static const COLORREF kColBg     = RGB(16, 18, 24);
    static const COLORREF kColPanel  = RGB(28, 32, 44);
    static const COLORREF kColPanel2 = RGB(44, 52, 70);
    static const COLORREF kColLine   = RGB(64, 74, 96);
    static const COLORREF kColText   = RGB(230, 232, 240);
    static const COLORREF kColDim    = RGB(150, 160, 180);
    static const COLORREF kColAcc    = RGB(255, 170, 0);    // DRG 橙
    static const COLORREF kColAcc2   = RGB(0, 255, 160);
    static const COLORREF kColRed    = RGB(255, 80, 80);

    static inline int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

    static std::wstring W(const char* s) {
        if (!s) return L"";
        wchar_t b[1024] = L"";
        MultiByteToWideChar(CP_UTF8, 0, s, -1, b, 1024);
        return b;
    }

    // DRG 常用数值预设（结合 Wand/WeMod 深岩银河修改项）
    struct Preset { const wchar_t* name; ValType t; const wchar_t* hint; };
    static const Preset kPresets[] = {
        { L"硝石",   VT_I32,  L"硝石：4 字节整数，搜到后首次值通常是个位数" },
        { L"黄金",   VT_I32,  L"黄金：4 字节整数，与背包内矿石计数同结构" },
        { L"信用点", VT_I32,  L"信用点：4 字节整数，可能为大数值（先缩小范围）" },
        { L"技能点", VT_I32,  L"技能点/Scrip：4 字节整数" },
        { L"经验值", VT_I32,  L"角色经验：4 字节整数，随结算变化" },
        { L"生命值", VT_F32,  L"生命值：浮点，受伤时变化（配合「减少」筛选）" },
        { L"护盾",   VT_F32,  L"护盾：浮点，受击后回复" },
        { L"弹药",   VT_I32,  L"弹药：当前弹匣 + 备弹两个整数，用「减少」筛选定位" },
        { L"手雷",   VT_I32,  L"手雷数量：投掷后减 1" },
        { L"啤酒原料", VT_I32, L"酒吧原料：4 字节整数" },
        { L"游戏速度", VT_F32, L"游戏速度/时间缩放：浮点，通常为 1.0" },
        { L"重力",   VT_F32,  L"重力强度：浮点，默认约 980 或 1.0（视实现）" },
    };

    // ---------------- 画布与 GDI ----------------
    bool Ui::Init(int w, int h, int uiScale, Scanner* s, Freezer* f, PatchEngine* p) {
        sc = s; fz = f; pe = p;
        scale = uiScale > 0 ? uiScale : 100;
        {
            int op = GetPrivateProfileIntW(L"general", L"ui_opacity", 100, g_configPathW.c_str());
            if (op < 30) op = 30; if (op > 100) op = 100;
            opacity = op;
            int ww = (int)GetPrivateProfileIntW(L"general", L"ui_win_w", 0, g_configPathW.c_str());
            int wh = (int)GetPrivateProfileIntW(L"general", L"ui_win_h", 0, g_configPathW.c_str());
            if (ww > 520) winSizeW = ww;
            if (wh > 320) winSizeH = wh;
        }
        advanced = g_advanced;
        {
            wchar_t e[16] = L"";      // 调试：FSDTRAINER_UI_TAB=6 可直接打开第 N 个页签（自动截图用）
            if (GetEnvironmentVariableW(L"FSDTRAINER_UI_TAB", e, 16) > 0) { int t = _wtoi(e); if (t >= 0) tab = t; }
        }
        MakeGdi(w, h);
        RecreateFonts();
        Log("UI 初始化 %dx%d scale=%d 透明度=%d%%", w, h, scale, opacity);
        return memDC != nullptr;
    }

    void Ui::MakeGdi(int w, int h) {
        if (w <= 0 || h <= 0) return;
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;          // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        memDC = CreateCompatibleDC(nullptr);
        memBmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, (void**)&bits, nullptr, 0);
        SelectObject(memDC, memBmp);

        txtDC = CreateCompatibleDC(nullptr);
        txtBmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, (void**)&tbits, nullptr, 0);
        SelectObject(txtDC, txtBmp);

        width = w; height = h;
        pixels = bits;
        ClearAll();
    }

    void Ui::FreeGdi() {
        if (memBmp) DeleteObject(memBmp);
        if (txtBmp) DeleteObject(txtBmp);
        if (memDC)  DeleteDC(memDC);
        if (txtDC)  DeleteDC(txtDC);
        memBmp = txtBmp = nullptr; memDC = txtDC = nullptr; bits = tbits = nullptr;
        pixels = nullptr;
    }

    void Ui::RecreateFonts() {
        if (font) DeleteObject(font);
        if (fontBold) DeleteObject(fontBold);
        LOGFONTW lf = {};
        lf.lfHeight = -S(16);
        lf.lfWeight = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = ANTIALIASED_QUALITY;
        wcscpy_s(lf.lfFaceName, L"Microsoft YaHei");
        font = CreateFontIndirectW(&lf);
        lf.lfWeight = FW_BOLD;
        fontBold = CreateFontIndirectW(&lf);
    }

    void Ui::Resize(int w, int h) {
        if (w <= 0 || h <= 0 || (w == width && h == height)) return;
        FreeGdi();
        MakeGdi(w, h);
    }

    void Ui::ClearAll() {
        if (!bits) return;
        memset(bits, 0, (size_t)width * height * 4);
        dirty = true;
    }

    // 调试用：把当前界面导出为 BMP（环境变量 FSDTRAINER_DUMP_UI=1）
    void Ui::DumpBmp() {
        if (!bits || width <= 0 || height <= 0) return;
        std::wstring dir(LogPath());
        size_t p = dir.find_last_of(L"\\/");
        dir = (p == std::wstring::npos) ? std::wstring(L"") : dir.substr(0, p + 1);
        std::wstring file = dir + L"FSDTrainer_ui.bmp";

        BITMAPFILEHEADER fh = {};
        BITMAPINFOHEADER ih = {};
        ih.biSize = sizeof(ih);
        ih.biWidth = width;
        ih.biHeight = -height;      // top-down
        ih.biPlanes = 1;
        ih.biBitCount = 32;
        ih.biCompression = BI_RGB;
        ih.biSizeImage = (DWORD)width * height * 4;
        fh.bfType = 0x4D42;
        fh.bfOffBits = sizeof(fh) + sizeof(ih);
        fh.bfSize = fh.bfOffBits + ih.biSizeImage;

        HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) { Log("导出界面失败: %s", Narrow(file).c_str()); return; }
        DWORD wr = 0;
        WriteFile(h, &fh, sizeof(fh), &wr, nullptr);
        WriteFile(h, &ih, sizeof(ih), &wr, nullptr);
        WriteFile(h, bits, ih.biSizeImage, &wr, nullptr);
        CloseHandle(h);
        Log("已导出界面截图: %s (%dx%d)", Narrow(file).c_str(), width, height);
    }

    void Ui::Fill(int x, int y, int w, int h, COLORREF c, BYTE alpha) {
        if (!bits) return;
        alpha = Aa(alpha);                       // 全局不透明度
        int x0 = ClampI(x, 0, width), y0 = ClampI(y, 0, height);
        int x1 = ClampI(x + w, 0, width), y1 = ClampI(y + h, 0, height);
        if (x1 <= x0 || y1 <= y0) return;
        uint32_t r = GetRValue(c), g = GetGValue(c), b = GetBValue(c);
        uint32_t a = alpha;
        uint32_t pr = r * a / 255, pg = g * a / 255, pb = b * a / 255;
        uint32_t val = (a << 24) | (pr << 16) | (pg << 8) | pb;
        for (int yy = y0; yy < y1; ++yy) {
            uint32_t* row = bits + (size_t)yy * width;
            for (int xx = x0; xx < x1; ++xx) row[xx] = val;
        }
    }

    void Ui::Border(int x, int y, int w, int h, COLORREF c, BYTE a) {
        Fill(x, y, w, 1, c, a);
        Fill(x, y + h - 1, w, 1, c, a);
        Fill(x, y, 1, h, c, a);
        Fill(x + w - 1, y, 1, h, c, a);
    }

    int Ui::TextW(const std::wstring& s, bool bold) {
        if (s.empty() || !txtDC) return 0;
        HGDIOBJ old = SelectObject(txtDC, bold ? fontBold : font);
        SIZE sz{ 0, 0 };
        GetTextExtentPoint32W(txtDC, s.c_str(), (int)s.size(), &sz);
        SelectObject(txtDC, old);
        return sz.cx;
    }

    void Ui::Text(int x, int y, const std::wstring& s, COLORREF c, BYTE a, bool bold) {
        if (s.empty() || !bits) return;
        a = Aa(a);
        int tw = TextW(s, bold) + S(6), th = LineH() + S(6);
        int rx0 = ClampI(x - S(2), 0, width), ry0 = ClampI(y - S(3), 0, height);
        int rx1 = ClampI(x + tw, 0, width), ry1 = ClampI(y + th, 0, height);
        if (rx1 <= rx0 || ry1 <= ry0) return;

        HGDIOBJ old = SelectObject(txtDC, bold ? fontBold : font);
        SetBkMode(txtDC, TRANSPARENT);
        SetTextColor(txtDC, RGB(255, 255, 255));
        HBRUSH blk = (HBRUSH)GetStockObject(BLACK_BRUSH);
        RECT r{ rx0, ry0, rx1, ry1 };
        FillRect(txtDC, &r, blk);
        TextOutW(txtDC, x, y + S(3), s.c_str(), (int)s.size());
        SelectObject(txtDC, old);

        int cr = GetRValue(c), cg = GetGValue(c), cb = GetBValue(c);
        for (int yy = ry0; yy < ry1; ++yy) {
            uint32_t* drow = bits + (size_t)yy * width;
            const uint32_t* srow = tbits + (size_t)yy * width;
            for (int xx = rx0; xx < rx1; ++xx) {
                uint32_t sp = srow[xx];
                int sb = sp & 0xFF, sg = (sp >> 8) & 0xFF, sr = (sp >> 16) & 0xFF;
                int cov = sr > sg ? sr : sg; if (sb > cov) cov = sb;
                if (cov <= 0) continue;
                int ca = cov * a;
                int srcA = ca / 255;
                if (srcA <= 0) continue;
                int sR = cr * ca / 65025, sG = cg * ca / 65025, sB = cb * ca / 65025;
                uint32_t dp = drow[xx];
                int iA = 255 - srcA;
                int dB = dp & 0xFF, dG = (dp >> 8) & 0xFF, dR = (dp >> 16) & 0xFF, dA = (dp >> 24) & 0xFF;
                int oB = sB + dB * iA / 255, oG = sG + dG * iA / 255, oR = sR + dR * iA / 255;
                int oA = srcA + dA * iA / 255;
                if (oB > 255) oB = 255; if (oG > 255) oG = 255; if (oR > 255) oR = 255; if (oA > 255) oA = 255;
                drow[xx] = ((uint32_t)oA << 24) | ((uint32_t)oR << 16) | ((uint32_t)oG << 8) | (uint32_t)oB;
            }
        }
    }

    void Ui::TextF(int x, int y, COLORREF c, const wchar_t* fmt, ...) {
        wchar_t buf[1024];
        va_list ap; va_start(ap, fmt);
        _vsnwprintf_s(buf, 1024, _TRUNCATE, fmt, ap);
        va_end(ap);
        Text(x, y, buf, c);
    }

    bool Ui::Hit(int x, int y, int w, int h) const {
        if (!in) return false;
        return in->mx >= x && in->mx < x + w && in->my >= y && in->my < y + h;
    }

    bool Ui::Button(int x, int y, int w, int h, const std::wstring& label, bool active, bool enabled) {
        bool hover = enabled && Hover(x, y, w, h);
        COLORREF bg = !enabled ? RGB(38, 40, 48) : (active ? kColAcc : (hover ? kColPanel2 : kColPanel));
        COLORREF fg = !enabled ? kColDim : (active ? RGB(20, 20, 24) : kColText);
        Fill(x, y, w, h, bg);
        Border(x, y, w, h, (hover && enabled) ? kColAcc : kColLine);
        int tw = TextW(label);
        Text(x + (w - tw) / 2, y + (h - LineH()) / 2, label, fg);
        if (enabled && hover && in && in->lclick) { const_cast<FrameInput*>(in)->lclick = false; return true; }
        return false;
    }

    bool Ui::Checkbox(int x, int y, const std::wstring& label, bool& value) {
        int bs = S(16);
        bool hover = Hover(x, y, bs, bs);
        Fill(x, y, bs, bs, value ? kColAcc2 : kColPanel);
        Border(x, y, bs, bs, hover ? kColAcc : kColLine);
        if (value) Text(x + S(2), y + (bs - LineH()) / 2, L"√", RGB(16, 18, 24), 255, true);
        Text(x + bs + S(6), y + (bs - LineH()) / 2, label, kColText);
        if (hover && in && in->lclick) { const_cast<FrameInput*>(in)->lclick = false; value = !value; return true; }
        return false;
    }

    bool Ui::Slider(int id, int x, int y, int w, int h, double& v, double lo, double hi) {
        bool changed = false;
        Fill(x, y, w, h, RGB(12, 14, 18));
        Border(x, y, w, h, kColLine);
        double t = (hi > lo) ? ((v - lo) / (hi - lo)) : 0.0;
        if (t < 0) t = 0; if (t > 1) t = 1;
        int fw = (int)((w - 2) * t);
        Fill(x + 1, y + 1, fw, h - 2, kColAcc, 255);
        bool hover = Hover(x, y, w, h);
        if (hover && in && in->ldown) {
            double nt = (double)(in->mx - x) / (double)w;
            if (nt < 0) nt = 0; if (nt > 1) nt = 1;
            v = lo + nt * (hi - lo);
            changed = true;
        }
        wchar_t b[48]; swprintf_s(b, L"%.2f", v);
        std::wstring s = b;
        Text(x + S(6), y + (h - LineH()) / 2, s, kColText);
        // 右端显示范围
        wchar_t rb[48]; swprintf_s(rb, L"%.0f~%.0f", lo, hi);
        Text(x + w - TextW(rb) - S(6), y + (h - LineH()) / 2, rb, kColDim);
        (void)id;
        return changed;
    }
    bool Ui::InputBox(int id, int x, int y, int w, int h, std::wstring& buf, bool numeric, bool dec1) {
        bool focused = (activeInput == id);
        // 上一个输入框按了 Tab：焦点跳到这里（内容全选，直接打字替换）
        if (pendingFocus == id) {
            pendingFocus = -1;
            activeInput = id; focused = true; inSelAll = true; inCursor = (int)buf.size();
        }
        bool hover = Hover(x, y, w, h);
        // 点一下进入编辑：全选，光标放到末尾（打字即整体替换）
        if (hover && in && in->lclick) {
            activeInput = id; focused = true; inSelAll = true; inCursor = (int)buf.size();
            const_cast<FrameInput*>(in)->lclick = false;
        }
        Fill(x, y, w, h, RGB(12, 14, 18));
        Border(x, y, w, h, focused ? kColAcc : kColLine);

        bool submitted = false;
        if (focused) {
            if (hover && in && numeric && in->wheel != 0) {                 // 滚轮微调 ±1
                double v = _wtof(buf.c_str()) + (in->wheel > 0 ? 1.0 : -1.0);
                wchar_t b[32]; swprintf_s(b, L"%.2f", v); buf = b;
                inSelAll = false; inCursor = (int)buf.size();
                const_cast<FrameInput*>(in)->wheel = 0;
            }
            bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            for (size_t i = 0; i < qkeys.size();) {
                UINT k = qkeys[i];
                if (ctrl && k == 'A') { inSelAll = true; qkeys.erase(qkeys.begin() + i); }
                // 左右箭头移动光标（像记事本一样，可在任意位置插入）
                else if (k == VK_LEFT) {
                    if (inSelAll) { inCursor = 0; inSelAll = false; }
                    else if (inCursor > 0) --inCursor;
                    qkeys.erase(qkeys.begin() + i);
                }
                else if (k == VK_RIGHT) {
                    if (inSelAll) { inCursor = (int)buf.size(); inSelAll = false; }
                    else if (inCursor < (int)buf.size()) ++inCursor;
                    qkeys.erase(qkeys.begin() + i);
                }
                else if (k == VK_HOME) { inCursor = 0; inSelAll = false; qkeys.erase(qkeys.begin() + i); }
                else if (k == VK_END) { inCursor = (int)buf.size(); inSelAll = false; qkeys.erase(qkeys.begin() + i); }
                else if (k == VK_BACK) {
                    if (inSelAll) { buf.clear(); inCursor = 0; inSelAll = false; }
                    else if (inCursor > 0) { buf.erase(inCursor - 1, 1); --inCursor; }
                    qkeys.erase(qkeys.begin() + i);
                }
                else if (k == VK_DELETE) {
                    if (inSelAll) { buf.clear(); inCursor = 0; inSelAll = false; }
                    else if (inCursor < (int)buf.size()) buf.erase(inCursor, 1);
                    qkeys.erase(qkeys.begin() + i);
                }
                else if (k == VK_RETURN) { submitted = true; inSelAll = false; qkeys.erase(qkeys.begin() + i); }
                // Tab：应用当前值并跳到下一个输入框（由调用方设置 pendingFocus）
                else if (k == VK_TAB) { submitted = true; tabPressed = true; inSelAll = false; qkeys.erase(qkeys.begin() + i); }
                else if (k == VK_ESCAPE) { activeInput = -1; focused = false; inSelAll = false; break; }
                else if (numeric && (k == VK_UP || k == VK_DOWN)) {
                    double v = _wtof(buf.c_str()) + (k == VK_UP ? 1.0 : -1.0);
                    wchar_t b[32]; swprintf_s(b, L"%.2f", v); buf = b;
                    inSelAll = false; inCursor = (int)buf.size();
                    qkeys.erase(qkeys.begin() + i);
                }
                else ++i;
            }
            if (inCursor > (int)buf.size()) inCursor = (int)buf.size();
            for (size_t i = 0; i < qchars.size();) {
                wchar_t ch = qchars[i];
                if (ctrl) { qchars.erase(qchars.begin() + i); continue; }    // Ctrl+字母不当成输入
                bool ok;
                if (dec1) {
                    // 数值框：只允许 0~9 和一个小数点，小数点后最多一位（不能输负号）
                    ok = (ch >= L'0' && ch <= L'9') || ch == L'.';
                    if (ok) {
                        std::wstring tmp = inSelAll ? std::wstring() : buf;
                        size_t pos = inSelAll ? 0 : (inCursor < (int)tmp.size() ? (size_t)inCursor : tmp.size());
                        tmp.insert(tmp.begin() + pos, ch);
                        int dots = 0;
                        for (wchar_t c2 : tmp) if (c2 == L'.') ++dots;
                        if (dots > 1) ok = false;
                        size_t d = tmp.find(L'.');
                        if (d != std::wstring::npos && tmp.size() - d - 1 > 1) ok = false;
                    }
                } else {
                    ok = numeric ? ((ch >= L'0' && ch <= L'9') || ch == L'.' || ch == L'-' || ch == L'+' ||
                                    (ch >= L'a' && ch <= L'f') || (ch >= L'A' && ch <= L'F') || ch == L'x' || ch == L'X')
                                 : (ch >= 32);
                }
                if (ok) {
                    if (inSelAll) { buf.clear(); inCursor = 0; inSelAll = false; }
                    if (inCursor > (int)buf.size()) inCursor = (int)buf.size();
                    buf.insert(buf.begin() + inCursor, ch);            // 在光标处插入
                    ++inCursor;
                    qchars.erase(qchars.begin() + i);
                } else ++i;
            }
        }
        if (focused && inSelAll && !buf.empty())
            Fill(x + S(3), y + S(3), TextW(buf) + S(6), h - S(6), RGB(40, 56, 84), 255);   // 选中高亮（不透明，避免把菜单打成半透明）
        Text(x + S(6), y + (h - LineH()) / 2, buf, kColText);
        if (focused) {
            int cx;
            if (inSelAll) cx = x + S(6) + TextW(buf) + 1;
            else { std::wstring pre = buf.substr(0, (size_t)inCursor); cx = x + S(6) + TextW(pre) + 1; }
            Fill(cx, y + S(4), S(2), h - S(8), kColAcc, 255);
        }
        if (in && in->lclick && !hover && activeInput == id) { activeInput = -1; inSelAll = false; }
        return submitted;
    }

    void Ui::Progress(int x, int y, int w, int h, int pct) {
        Fill(x, y, w, h, RGB(12, 14, 18));
        Border(x, y, w, h, kColLine);
        pct = ClampI(pct, 0, 100);
        Fill(x + 1, y + 1, (w - 2) * pct / 100, h - 2, kColAcc, 255);
    }

    // ---------------- 页面 ----------------
    // 功能页（按分类渲染：数值型 → 输入框 + 应用/恢复默认；其余 → 开关/动作按钮）
    void Ui::PageGame(int x, int y, int w, int h, const std::string& cat) {
        // 页面滚动：滚轮上下（输入框正在编辑时滚轮归输入框用，不动页面）
        if (activeInput < 0 && in && in->wheel != 0
            && in->mx >= x - S(8) && in->mx <= x + w + S(8)
            && in->my >= y - S(8) && in->my <= y + h + S(8)) {
            gscroll -= in->wheel * S(34);
            const_cast<FrameInput*>(in)->wheel = 0;
        }
        if (gscroll < 0) gscroll = 0;

        int lh = LineH() + S(6), cy = y;
        TextF(x, cy, kColDim, L"分类：%s    已开启 %zu/%zu    前缀 * 表示“客机（非主机）时无效”",
              W(cat.c_str()).c_str(), g_features.OnCount(), g_features.list.size());
        if (Button(x + w - S(170), cy - S(4), S(160), LineH() + S(8), L"回归善良人格")) {
            g_features.AllOff(); RestoreGameState();
            valBuf.clear();                      // 输入框缓存清掉，下一帧按默认值重新填充
            note = L"已回归善良人格（开关关闭、数值恢复默认）";
        }
        cy += lh + S(8);

        // ---- 数值区标题 + 列头（固定不动）----
        Text(x, cy, L"数值设置", kColAcc, 255, true);
        Text(x + S(86), cy + S(3), L"改完数值后：点「应用」/ 回车 / Tab 才生效（左右 ± 只改数值）", kColDim);
        cy += LineH() + S(8);
        Text(x + S(4), cy + S(3), L"功能", kColDim);
        Text(x + S(150), cy + S(3), L"单位", kColDim);
        Text(x + S(232), cy + S(3), L"数值", kColDim);
        Text(x + S(424), cy + S(3), L"操作", kColDim);
        Text(x + S(672), cy + S(3), L"状态", kColDim);
        cy += LineH() + S(4);
        Fill(x - S(4), cy - S(3), w - S(8), S(1), kColLine, 255);

        // ---- 以下内容可滚动 ----
        const int contentTop = cy;
        const int bandBot = y + h - S(2);
        int dcy = contentTop - gscroll;
        auto visRow = [&](int top, int rh) { return top >= contentTop - S(2) && top + rh <= bandBot; };

        // 能参与 Tab 的输入框（封印 / 未实现的、以及滚动到视野外的都不算）
        std::vector<size_t> rows;
        {
            int py = contentTop - gscroll;
            for (size_t i = 0; i < g_features.list.size(); ++i) {
                Feature& f = g_features.list[i];
                if (f.cat != cat || (f.ui != "value" && f.ui != "slider")) continue;
                if (visRow(py, lh) && !(f.sealed || (f.kind != "cheat" && f.addr == 0 && f.pattern.empty())))
                    rows.push_back(i);
                py += lh;
            }
        }

        size_t bindIdx = (size_t)-1;
        for (size_t i = 0; i < g_features.list.size(); ++i) {
            Feature& f = g_features.list[i];
            if (f.cat != cat || (f.ui != "value" && f.ui != "slider")) continue;
            if (!visRow(dcy, lh)) { dcy += lh; continue; }          // 滚出视野：只推进游标
            // 隔行底色 + 鼠标悬停高亮，长列表更好读
            {
                int rh = lh - S(4);
                bool hov = Hover(x - S(4), dcy - S(2), w - S(8), rh);
                if (hov) Fill(x - S(4), dcy - S(2), w - S(8), rh, RGB(48, 52, 68), 255);
                else if ((i & 1u) == 0) Fill(x - S(4), dcy - S(2), w - S(8), rh, RGB(29, 32, 41), 255);
            }
            bool changed = (f.cur != f.def);
            // 还没实现的功能：灰掉、不让点
            if ((f.sealed || (f.kind != "cheat" && f.addr == 0 && f.pattern.empty()))) {
                std::wstring dn = W(f.name.c_str());
                if (f.clientOnly) dn = L"*" + dn;
                Text(x, dcy + S(4), dn, kColDim);
                Text(x + S(150), dcy + S(4), W(f.unit.c_str()), kColDim);
                dcy += lh;
                continue;
            }
            std::wstring nm = W(f.name.c_str());
            if (f.clientOnly) nm = L"*" + nm;
            Text(x, dcy + S(4), nm, f.on ? kColAcc2 : kColText, 255, true);
            Text(x + S(150), dcy + S(4), W(f.unit.c_str()), kColDim);
            if (valBuf.size() <= i) valBuf.resize(i + 1);
            bool editing = (activeInput == 2000 + (int)i);
            bool ready = (f.kind == "cheat") || f.addr != 0;
            // 正在编辑时不要用旧值回填（否则清空输入框会"回弹"）
            if (valBuf[i].empty() && !editing) { wchar_t b[32]; swprintf_s(b, L"%.1f", f.cur); valBuf[i] = b; }
            // 输入框左侧的 － / ＋ 按钮：只改数值，不立刻下发
            {
                auto nudge = [&](double d) {
                    double v = _wtof(valBuf[i].c_str()) + d;
                    if (v < 0) v = 0;                       // 一律不允许负值
                    wchar_t b[32]; swprintf_s(b, L"%.1f", v);
                    valBuf[i] = b;
                    inSelAll = false; inCursor = (int)valBuf[i].size();
                    note = L"已改数值，按「应用」或回车才生效";
                };
                if (Button(x + S(248), dcy, S(26), LineH() + S(10), L"－", false, ready)) nudge(-1);
                if (Button(x + S(276), dcy, S(26), LineH() + S(10), L"＋", false, ready)) nudge(1);
            }
            bool entered = false;
            if (f.ui == "slider") {
                double vv = f.cur;
                wchar_t bb[32]; swprintf_s(bb, L"%.1f", vv); valBuf[i] = bb;
                bool moved = Slider(2000 + (int)i, x + S(310), dcy, S(150), LineH() + S(10), vv, f.lo, f.hi);
                bool typed = InputBox(3000 + (int)i, x + S(470), dcy, S(80), LineH() + S(10), valBuf[i], true, true);
                if ((moved || typed) && (f.kind == "cheat" || f.addr)) {
                    double use = moved ? vv : _wtof(valBuf[i].c_str());
                    g_features.SetValue(i, use);
                    if (moved) { wchar_t b2[32]; swprintf_s(b2, L"%.1f", vv); valBuf[i] = b2; }
                    note = L"已应用";
                }
            } else {
                entered = InputBox(2000 + (int)i, x + S(310), dcy, S(100), LineH() + S(10), valBuf[i], true, true);
            }
            if (entered) {
                if (f.kind == "cheat" || f.addr) { double v0=_wtof(valBuf[i].c_str()); if (v0<0) v0=0; wchar_t b0[32]; swprintf_s(b0,L"%.1f",v0); valBuf[i]=b0; g_features.SetValue(i, v0); note = L"已应用（回车）"; }
                else if (selected >= 0 && sc->Count() > 0) bindIdx = i;
                else note = L"先在「扫描」页选中一个数值，再点「绑定」";
            }
            if (tabPressed) {
                tabPressed = false;
                int myPos = -1;
                for (size_t k = 0; k < rows.size(); ++k) if (rows[k] == i) { myPos = (int)k; break; }
                if (myPos >= 0 && !rows.empty()) {
                    size_t nxt = rows[((size_t)myPos + 1) % rows.size()];
                    pendingFocus = 2000 + (int)nxt;
                    note = (nxt <= i) ? L"已应用；Tab 回到第一个输入框" : L"已应用；Tab 跳到下一个输入框";
                } else {
                    note = L"已应用";
                }
            }
            if (Button(x + S(420), dcy, S(110), LineH() + S(10), ready ? L"应用" : L"绑定", false, true)) {
                if (!ready) { if (selected >= 0 && sc->Count() > 0) bindIdx = i; else note = L"先在「扫描」页选中一个数值，再点「绑定」"; }
                else { double v0=_wtof(valBuf[i].c_str()); if (v0<0) v0=0; wchar_t b0[32]; swprintf_s(b0,L"%.1f",v0); valBuf[i]=b0; g_features.SetValue(i, v0); note = L"已应用"; }
            }
            if (Button(x + S(540), dcy, S(120), LineH() + S(10), L"恢复默认", false, ready && (changed || f.on))) {
                g_features.ResetValue(i); valBuf[i].clear(); note = L"已恢复默认";
            }
            if (ready) {
                if (changed || f.on) { Text(x + S(674), dcy + S(4), L"● 生效中", kColAcc2, 255, true); }
                else { Text(x + S(674), dcy + S(4), L"默认", kColDim); }
            } else {
                Text(x + S(674), dcy + S(4), L"未绑定", kColRed);
            }
            dcy += lh;
        }

        // ---- 开关 / 动作（分两组，等宽排列）----
        dcy += S(10);
        int gap = S(8);
        int perRow = (w - S(4)) / (S(190) + gap);
        if (perRow < 1) perRow = 1;
        int rowW = (w - S(4) - gap * (perRow - 1)) / perRow;
        if (rowW < S(90)) rowW = S(90);

        auto drawGrid = [&](const char* uiKind, const wchar_t* title, const wchar_t* hint) {
            bool any = false;
            for (size_t i = 0; i < g_features.list.size(); ++i) {
                Feature& f = g_features.list[i];
                if (f.cat == cat && f.ui == uiKind) { any = true; break; }
            }
            if (!any) return;
            int gh = LineH() + S(10);
            if (visRow(dcy, LineH())) {
                Text(x, dcy, title, kColAcc, 255, true);
                Text(x + S(70), dcy + S(2), hint, kColDim);
            }
            dcy += LineH() + S(2);
            int col = 0;
            for (size_t i = 0; i < g_features.list.size(); ++i) {
                Feature& f = g_features.list[i];
                if (f.cat != cat || f.ui != uiKind) continue;
                int bx = x + (col % perRow) * (rowW + gap);
                int by = dcy + (col / perRow) * gh;
                bool vis = (by >= contentTop - S(2) && by + gh <= bandBot);
                std::wstring label = W(f.name.c_str());
                if (f.clientOnly) label = L"*" + label;
                if (vis) {
                    if ((f.sealed || (f.kind != "cheat" && f.addr == 0 && f.pattern.empty()))) {
                        Button(bx, by, rowW, LineH() + S(8), label, false, false);
                    } else {
                        bool ready = (f.kind == "cheat") || f.addr != 0;
                        bool onNow = f.on;                         // 开关型的状态在 f.on 上（Toggle() 翻的就是它；加载时已按 def 初始化）
                        if (Button(bx, by, rowW, LineH() + S(8), label, f.ui == "toggle" && onNow, true)) {
                            if (!ready) {
                                if (selected >= 0 && sc->Count() > 0) bindIdx = i;
                                else note = L"此功能还没有数据：先用「扫描」选中数值，再点这个按钮即可绑定";
                            } else if (f.ui == "action") {
                                if (f.kind == "cheat") { g_features.Fire(i); }
                                else { g_features.Toggle(i); if (f.on) g_features.Toggle(i); }
                                note = L"已触发";
                            } else {
                                g_features.Toggle(i);
                                note = f.on ? L"已开启" : L"已关闭，已恢复原状";
                            }
                        }
                    }
                }
                ++col;
            }
            int rowsn = (col + perRow - 1) / perRow;
            if (rowsn < 1) rowsn = 1;
            dcy += rowsn * gh + S(2);
        };
        // 开关型（ui=toggle）单独一块网格：列表里只放数值型，排版才不乱
        drawGrid("toggle", L"开关", L"点一下开 / 再点一下关（关闭时恢复原状）");
        drawGrid("action", L"动作", L"点一下触发一次（立刻执行）");

        // ---- 内容高度 → 限制滚动 + 画滚动条 ----
        {
            int contentH = (dcy + gscroll) - contentTop;
            int visH = bandBot - contentTop;
            if (visH < S(40)) visH = S(40);
            gscrollMax = contentH - visH;
            if (gscrollMax < 0) gscrollMax = 0;
            if (gscroll > gscrollMax) gscroll = gscrollMax;
            if (gscrollMax > 0) {
                int barH = visH * visH / (contentH > 0 ? contentH : visH);
                if (barH < S(28)) barH = S(28);
                int barY = contentTop + (visH - barH) * gscroll / gscrollMax;
                Fill(x + w - S(1), contentTop, S(5), visH, RGB(32, 35, 44), 255);
                Fill(x + w - S(1), barY, S(5), barH, kColAcc, 255);
                TextF(x, y + h - LineH() - S(2), kColDim, L"滚轮上下滚动");
            }
        }


        if (bindIdx != (size_t)-1) {
            uintptr_t a = 0; double cur = 0; ValType t = VT_I32;
            { std::lock_guard<std::mutex> lk(sc->mtx);
              if (selected >= 0 && selected < (int)sc->hits.size()) { a = sc->hits[selected].addr; cur = sc->hits[selected].cur; t = sc->type; } }
            if (a && g_features.BindAddress(bindIdx, a, t, cur)) { g_features.Save(g_configPathW); note = L"已绑定该地址并写入 ini"; }
        }
    }

    void Ui::PageScan(int x, int y, int w, int h) {
        int lh = LineH() + S(6), cy = y;
        int bx = x + S(72);
        Text(x, cy + S(3), L"类型", kColDim);
        const wchar_t* tn[] = { L"字节", L"2字节", L"4字节", L"8字节", L"浮点", L"双浮点" };
        for (int i = 0; i < VT_COUNT; ++i) {
            int bw = S(70);
            if (Button(bx + i * (bw + S(4)), cy - S(3), bw, LineH() + S(6), tn[i], vtype == i)) vtype = (ValType)i;
        }
        cy += lh;

        Text(x, cy + S(3), L"数值", kColDim);
        InputBox(1, bx, cy - S(3), S(170), LineH() + S(6), scanValue, true, true);
        if (Button(bx + S(178), cy - S(3), S(110), LineH() + S(6), L"首次扫描", false, !sc->busy.load())) {
            sc->FirstScan(vtype, _wtof(scanValue.c_str()), scanModuleOnly);
            selected = -1; scroll = 0;
            note = L"已开始首次扫描";
        }
        if (Button(bx + S(296), cy - S(3), S(110), LineH() + S(6), L"再次扫描", false, !sc->busy.load() && sc->Count() > 0)) {
            sc->NextScan(_wtof(scanValue.c_str()), cmp);
            note = L"已开始再次扫描";
        }
        if (Button(bx + S(414), cy - S(3), S(90), LineH() + S(6), L"清空")) { sc->Clear(); selected = -1; scroll = 0; note = L"结果已清空"; }
        if (Button(bx + S(510), cy - S(3), S(150), LineH() + S(6), L"未知初始值扫描", false, !sc->busy.load())) {
            // 不知道数值时用：只保留“看起来合理”的数值，再用「减少/未变化」逐步筛
            if (vtype == VT_F32) sc->FirstScanUnknown(VT_F32, scanModuleOnly, 0.5, 5000.0);
            else if (vtype == VT_F64) sc->FirstScanUnknown(VT_F64, scanModuleOnly, 0.5, 5000.0);
            else sc->FirstScanUnknown(vtype, scanModuleOnly, 1, 100000);
            selected = -1; scroll = 0;
            note = L"未知初始值扫描已开始（血量：选浮点；弹药：选 4 字节）";
        }
        cy += lh;

        Text(x, cy + S(3), L"筛选", kColDim);
        for (int i = 0; i < CMP_MODE_COUNT; ++i) {
            int bw = S(76);
            if (Button(bx + i * (bw + S(4)), cy - S(3), bw, LineH() + S(6), CmpName((CmpMode)i), cmp == i)) cmp = (CmpMode)i;
        }
        cy += lh;

        Text(x, cy + S(3), L"写入", kColDim);
        InputBox(2, bx, cy - S(3), S(170), LineH() + S(6), writeValue, true, true);
        if (Button(bx + S(178), cy - S(3), S(110), LineH() + S(6), L"写入选中", false, selected >= 0)) {
            uintptr_t addr = 0;
            { std::lock_guard<std::mutex> lk(sc->mtx); if (selected >= 0 && selected < (int)sc->hits.size()) addr = sc->hits[selected].addr; }
            if (addr && WriteTypedValue(addr, vtype, _wtof(writeValue.c_str()))) note = L"写入成功";
            else note = L"写入失败";
        }
        if (Button(bx + S(296), cy - S(3), S(120), LineH() + S(6), L"全部写入", false, sc->Count() > 0 && sc->Count() <= 500)) {
            double v = _wtof(writeValue.c_str()); size_t n = 0;
            { std::lock_guard<std::mutex> lk(sc->mtx);
              for (auto& ht : sc->hits) { WriteTypedValue(ht.addr, sc->type, v); if (++n >= 500) break; } }
            note = L"已把全部候选写成该数值（HUD 立刻变化的那条才是真的）";
        }
        if (Button(bx + S(420), cy - S(3), S(150), LineH() + S(6), L"全部写入并锁定", false, sc->Count() > 0 && sc->Count() <= 200)) {
            double v = _wtof(writeValue.c_str()); size_t n = 0;
            { std::lock_guard<std::mutex> lk(sc->mtx);
              for (auto& ht : sc->hits) { WriteTypedValue(ht.addr, sc->type, v); fz->Set(ht.addr, sc->type, v); if (++n >= 200) break; } }
            note = L"已全部写入并锁定（回游戏验证是否满血/不掉血）";
        }
        if (Button(bx + S(296), cy - S(3) + LineH() + S(10), S(120), LineH() + S(6), L"加入冻结", false, selected >= 0)) {
            uintptr_t addr = 0; double cur = 0;
            { std::lock_guard<std::mutex> lk(sc->mtx); if (selected >= 0 && selected < (int)sc->hits.size()) { addr = sc->hits[selected].addr; cur = sc->hits[selected].cur; } }
            if (addr) { fz->Set(addr, vtype, cur); note = L"已加入冻结列表"; }
        }
        if (Button(bx + S(430), cy - S(3), S(150), LineH() + S(6), L"生成特征码", false, selected >= 0)) {
            uintptr_t a = 0;
            { std::lock_guard<std::mutex> lk(sc->mtx);
              if (selected >= 0 && selected < (int)sc->hits.size()) a = sc->hits[selected].addr; }
            if (a) { sigText = W(MakeSignature(a).c_str()); note = L"已生成特征码（可复制到 ini 的 pattern=）"; }
        }
        if (Button(bx + S(590), cy - S(3), S(120), LineH() + S(6), L"找指针", false, selected >= 0)) {
            uintptr_t a = 0;
            { std::lock_guard<std::mutex> lk(sc->mtx);
              if (selected >= 0 && selected < (int)sc->hits.size()) a = sc->hits[selected].addr; }
            if (a) {
                auto ptrs = FindPointersTo(a, 8);
                sigText = L"";
                for (auto p : ptrs) { wchar_t b[48]; swprintf_s(b, L"0x%llX  ", (unsigned long long)p); sigText += b; }
                if (ptrs.empty()) sigText = L"未找到指向该地址的指针";
                note = L"指针扫描完成";
            }
        }
        bool mo = scanModuleOnly;
        if (Checkbox(bx + S(430), cy - S(2), L"仅扫描主模块", mo)) scanModuleOnly = mo;
        cy += lh;

        // 预设数值
        Text(x, cy + S(3), L"预设", kColDim);
        for (int i = 0; i < (int)(sizeof(kPresets) / sizeof(kPresets[0])); ++i) {
            int row = i / 6, col = i % 6;
            int bw = S(76), bh = LineH() + S(2);
            if (Button(bx + col * (bw + S(4)), cy - S(3) + row * (bh + S(2)), bw, bh, kPresets[i].name,
                       vtype == kPresets[i].t && false)) {
                vtype = kPresets[i].t;
                scanValue = L"0";
                note = kPresets[i].hint;
            }
        }
        cy += (LineH() + S(2)) * 2 + S(6);

        int pw = w - S(420);
        Progress(x, cy, pw, S(14), sc->progress.load());
        TextF(x + pw + S(10), cy - S(6), sc->busy.load() ? kColAcc : kColDim, L"%s", W(sc->Status().c_str()).c_str());
        cy += S(24);

        if (!sigText.empty()) { TextF(x, cy - S(24), kColAcc2, L"结果：%s", sigText.c_str()); }
        int listH = y + h - cy;
        if (listH < LineH() * 2) listH = LineH() * 2;
        int rows = listH / LineH();
        Fill(x, cy, w, listH, RGB(12, 14, 18));
        Border(x, cy, w, listH, kColLine);
        {
            std::lock_guard<std::mutex> lk(sc->mtx);
            if (sc->hits.empty()) {
                Text(x + S(10), cy + S(6), L"暂无结果", kColDim);
            } else {
                int total = (int)sc->hits.size();
                int maxScroll = total - rows;
                if (in && Hover(x, cy, w, listH) && in->wheel != 0) scroll -= (in->wheel / 120) * 3;
                scroll = ClampI(scroll, 0, maxScroll > 0 ? maxScroll : 0);
                for (int i = 0; i < rows; ++i) {
                    int idx = scroll + i;
                    if (idx < 0 || idx >= total) break;
                    auto& hh = sc->hits[idx];
                    int ry = cy + i * LineH();
                    bool sel = (selected == idx);
                    if (sel) Fill(x + 1, ry, w - 2, LineH(), RGB(58, 68, 92));
                    else if (Hover(x, ry, w, LineH()) && in && in->lclick) { selected = idx; const_cast<FrameInput*>(in)->lclick = false; }
                    TextF(x + S(10), ry, sel ? kColAcc : kColText, L"0x%016llX", (unsigned long long)hh.addr);
                    TextF(x + S(290), ry, kColAcc2, L"%.7g", hh.cur);
                    if (fz->Has(hh.addr)) TextF(x + S(430), ry, kColAcc, L"[冻结中]");
                }
                TextF(x + w - S(260), cy + S(2), kColDim, L"共 %d 条  当前第 %d 行", total, scroll + 1);
            }
        }
    }

    void Ui::PageFreeze(int x, int y, int w, int h) {
        int lh = LineH() + S(6);
        Fill(x, y, w, h, RGB(12, 14, 18));
        Border(x, y, w, h, kColLine);
        std::vector<FreezeEntry> copy;
        { std::lock_guard<std::mutex> lk(fz->mtx); copy = fz->list; }
        if (copy.empty()) { Text(x + S(10), y + S(10), L"冻结列表为空：在「扫描」页选中地址后可加入冻结。", kColDim); return; }
        int rows = (h - S(12)) / lh;
        int removeIdx = -1;
        for (int i = 0; i < rows && i < (int)copy.size(); ++i) {
            int ry = y + S(6) + i * lh;
            auto e = copy[i];
            TextF(x + S(10), ry, kColText, L"0x%016llX", (unsigned long long)e.addr);
            Text(x + S(230), ry, TypeName(e.type), kColDim);
            wchar_t vb[64]; swprintf_s(vb, L"%.7g", e.value);
            std::wstring vs = vb;
            InputBox(1000 + i, x + S(300), ry - S(3), S(140), LineH() + S(6), vs, true);
            { std::lock_guard<std::mutex> lk(fz->mtx); if (i < (int)fz->list.size()) { fz->list[i].value = _wtof(vs.c_str()); fz->list[i].type = e.type; } }
            bool en = e.enabled;
            if (Checkbox(x + S(460), ry, L"锁定", en)) { std::lock_guard<std::mutex> lk(fz->mtx); if (i < (int)fz->list.size()) fz->list[i].enabled = en; }
            if (Button(x + S(540), ry - S(3), S(70), LineH() + S(6), L"删除")) removeIdx = i;
        }
        if (removeIdx >= 0) { fz->Remove((size_t)removeIdx); note = L"已从冻结列表移除"; }
    }

    void Ui::PagePatch(int x, int y, int w, int h) {
        (void)w;
        int lh = LineH() + S(6);
        Fill(x, y, w, h, RGB(12, 14, 18));
        Border(x, y, w, h, kColLine);
        if (pe->defs.empty()) {
            Text(x + S(10), y + S(10), L"未配置补丁。可在 FSDTrainer.ini 中追加：", kColDim);
            Text(x + S(10), y + S(10) + lh, L"[patch.无限弹药]", kColAcc2);
            Text(x + S(10), y + S(10) + lh * 2, L"pattern=48 8B 05 ?? ?? ?? ??", kColText);
            Text(x + S(10), y + S(10) + lh * 3, L"offset=0", kColText);
            Text(x + S(10), y + S(10) + lh * 4, L"bytes=90 90 90 90 90", kColText);
            Text(x + S(10), y + S(10) + lh * 5, L"hotkey=VK_F1", kColText);
            Text(x + S(10), y + S(10) + lh * 6, L"enabled=0", kColText);
            return;
        }
        for (size_t i = 0; i < pe->defs.size(); ++i) {
            auto& p = pe->defs[i];
            int ry = y + S(6) + (int)i * lh;
            Text(x + S(10), ry, W(p.name.c_str()), kColText);
            if (!p.addr) Text(x + S(220), ry, L"特征码未命中", kColRed);
            else TextF(x + S(220), ry, kColDim, L"0x%016llX", (unsigned long long)p.addr);
            if (p.hotkey) TextF(x + S(430), ry, kColDim, L"[%s]", W(HotkeyName(p.hotkey)));
            if (Button(x + S(540), ry - S(3), S(80), LineH() + S(6), p.applied ? L"关闭" : L"开启", p.applied, p.addr != 0)) {
                pe->Toggle(i);
                note = p.applied ? L"补丁已开启" : L"补丁已关闭";
            }
        }
    }

    void Ui::PageConsole(int x, int y, int w, int h) {
        (void)h;
        int lh = LineH() + S(6), cy = y;
        bool ready = game::ConsoleReady();
        Text(x, cy, L"UE 控制台命令（可选功能）", kColAcc, 255, true); cy += lh + S(4);
        TextF(x, cy, ready ? kColAcc2 : kColRed, L"状态：%s",
              ready ? L"已定位 GEngine / UEngine::Exec" : L"未配置（在 ini 填写 gengine_pattern 与 exec_pattern）"); cy += lh;
        bool submit = InputBox(3, x, cy, w - S(120), LineH() + S(8), cmdValue, false);
        if (Button(x + w - S(110), cy, S(100), LineH() + S(8), L"执行", false, ready) || submit) {
            if (cmdValue.empty()) { note = L"命令为空"; }
            else { game::ExecCommand(cmdValue.c_str()); note = L"命令已发送"; }
            cy += 0;
        }
        cy += lh + S(4);
        TextF(x, cy, kColText, L"结果：%s", game::LastConsoleResult().c_str()); cy += lh * 2;
        Text(x, cy, L"常用命令示例", kColAcc, 255, true); cy += lh;
        const wchar_t* cmds[] = {
            L"stat fps            显示帧率",
            L"stat unit           显示帧耗时分解",
            L"t.MaxFPS 165        帧率上限",
            L"r.ScreenPercentage 70   渲染分辨率百分比（提帧）",
            L"r.Streaming.PoolSize 4096   纹理流池大小（MB）",
            L"slomo 1.5           世界时间缩放（部分版本受限）",
        };
        for (auto c : cmds) { Text(x, cy, c, kColDim); cy += lh; }
    }

    // 与 Wand / WeMod《深岩银河》修改项的功能对照
    struct FeatLine { const wchar_t* text; bool header; };
    static const FeatLine kFeats[] = {
        { L"对照 Wand(WeMod)《深岩银河》72 项修改，说明本工具的实现路径", true },
        { L"【玩家】无限生命｜无限跳跃｜奔跑速度｜飞行｜重力强度 → 浮点扫描 + 冻结/改写", false },
        { L"【玩家】随处跳舞｜重置增益冷却｜额外特长 → 控制台命令 / 补丁", false },
        { L"【仓库】无限携带｜无限照明弹｜积分｜技能点｜资源数量｜Scrip → 数值扫描 + 写入", false },
        { L"【统计】加经验｜提升等级｜季节 XP → 数值扫描 + 写入", false },
        { L"【武器】无限弹药｜无需装弹｜无后坐力｜防止过热｜射速｜自动武器 → 冻结弹药计数 或 AOB 补丁", false },
        { L"【武器】一击必杀｜一击挖矿｜挖矿半径｜镐子无冷却 → AOB 补丁（改写参数常量）", false },
        { L"【武器】职业专属：钻探者燃料/炸药包、工程师哨戒炮、侦察兵抓钩、枪手滑索 → AOB 补丁", false },
        { L"【武器】解锁全部武器/升级/镐部件 → 需定位解锁逻辑（RE 后补丁）", false },
        { L"【载具】Doretta 燃料/速度/血量｜Molly 速度 → 数值扫描 + 冻结", false },
        { L"【敌人】消灭所有敌人｜选择小虫类型与数量 → 需调用生成/销毁函数（RE 后补丁）", false },
        { L"【内容】游戏速度｜生成降落舱｜召唤补给舱｜完成任务｜解锁啤酒/深潜 → 控制台或函数调用", false },
        { L"【位移】传送到激光标记 → 读标记坐标 + 写玩家坐标（RE 后补丁）", false },
        { L"", false },
        { L"落地优先级建议", true },
        { L"1) 数值类（资源/积分/经验/生命/弹药）：本工具内置扫描+冻结，即刻可用；", false },
        { L"2) 参数类（后坐力/射速/过热/半径）：在 FSDTrainer.ini 的 [patch.*] 段填特征码与字节即可开关；", false },
        { L"3) 逻辑类（一击必杀/解锁/生成/传送）：需逆向出函数地址，或用「命令」页的 UE 控制台替代。", false },
        { L"", false },
        { L"与 Wand 的差异：Wand 依赖随版本编译的固定偏移，游戏更新即失效；", false },
        { L"本工具是版本无关框架：偏移/特征码由你在 ini 中维护，扫描与补丁引擎不随更新失效。", false },
    };

    void Ui::PageFeatures(int x, int y, int w, int h) {
        int lh = LineH() + S(4);
        int rows = h / lh;
        int total = (int)(sizeof(kFeats) / sizeof(kFeats[0]));
        int maxScroll = total - rows;
        if (maxScroll < 0) maxScroll = 0;
        if (in && Hover(x, y, w, h) && in->wheel != 0) fscroll -= (in->wheel / 120) * 2;
        if (fscroll < 0) fscroll = 0;
        if (fscroll > maxScroll) fscroll = maxScroll;
        for (int i = 0; i < rows; ++i) {
            int idx = fscroll + i;
            if (idx >= total) break;
            const FeatLine& f = kFeats[idx];
            Text(x, y + i * lh, f.text, f.header ? kColAcc : kColText);
        }
        TextF(x + w - S(160), y + h - LineH() - S(4), kColDim, L"滚轮翻页 %d/%d", fscroll + 1, maxScroll + 1);
    }

    // 设置页：只保留「显示高级功能」「界面透明度」「重新载入 ini」，其余文案说明全部删掉
    void Ui::PageSettings(int x, int y, int w, int h) {
        (void)w; (void)h;
        int lh = LineH() + S(10), cy = y;

        // ---- 高级功能 ----
        {
            bool adv = advanced;
            if (Checkbox(x, cy, L"显示高级功能", adv)) {
                advanced = adv;
                g_advanced = adv;
                if (!adv && tab > 0) tab = 0;
                note = adv ? L"已显示高级功能页" : L"已隐藏高级功能页";
            }
            cy += lh + S(6);
        }

        // ---- 界面透明度（默认 95%）----
        {
            Text(x, cy + S(4), L"背景不透明", kColText, 255, true);
            auto saveOp = [&]() {
                wchar_t b[16] = L"";
                swprintf_s(b, L"%d", opacity);
                WritePrivateProfileStringW(L"general", L"ui_opacity", b, g_configPathW.c_str());
            };
            if (Button(x + S(150), cy - S(3), S(50), LineH() + S(8), L"-") && opacity > 30) {
                opacity -= 5; if (opacity < 30) opacity = 30;
                saveOp(); note = L"背景更透明了";
            }
            TextF(x + S(212), cy + S(4), kColAcc2, L"%d%%", opacity);
            if (Button(x + S(280), cy - S(3), S(50), LineH() + S(8), L"+") && opacity < 100) {
                opacity += 5; if (opacity > 100) opacity = 100;
                saveOp(); note = L"背景更不透明了";
            }
            if (Button(x + S(346), cy - S(3), S(100), LineH() + S(8), L"恢复100%")) {
                opacity = 100; saveOp(); note = L"背景不透明已恢复 100%（完全看不到后面的游戏画面）";
            }
            cy += lh + S(6);
        }

        // ---- 重新载入功能表 ----
        if (Button(x, cy - S(3), S(180), LineH() + S(8), L"重新载入 ini")) {
            g_features.Load(g_configPathW);
            valBuf.clear();
            tab = 0;
            note = L"已重新载入功能表（无需重启游戏）";
        }
        cy += lh + S(6);

        // ---- 菜单尺寸 ----
        {
            Text(x, cy + S(4), L"菜单大小", kColText, 255, true);
            TextF(x + S(150), cy + S(4), kColDim, L"%dx%d（也可以直接拖菜单边框缩放）",
                  winSizeW > 0 ? winSizeW : width, winSizeH > 0 ? winSizeH : height);
            if (Button(x + S(470), cy - S(3), S(120), LineH() + S(8), L"恢复自动")) {
                winSizeW = winSizeH = 0;
                WritePrivateProfileStringW(L"general", L"ui_win_w", L"0", g_configPathW.c_str());
                WritePrivateProfileStringW(L"general", L"ui_win_h", L"0", g_configPathW.c_str());
                note = L"菜单大小已恢复自动";
            }
        }
    }

    // 调试：FSDTRAINER_DUMP_UI=1 时周期性把画布导出为 BMP（可见/隐藏两条路径都会调用）
    void Ui::MaybeDump() {
        static ULONGLONG lastDump = 0;
        static int dumpOn = -1;
        if (dumpOn < 0) { wchar_t e[8] = L""; dumpOn = (GetEnvironmentVariableW(L"FSDTRAINER_DUMP_UI", e, 8) > 0) ? 1 : 0; }
        if (dumpOn != 1) return;
        ULONGLONG n3 = GetTickCount64();
        if (lastDump == 0 || n3 - lastDump > 1500) { lastDump = n3; DumpBmp(); }
    }

    // ---------------- ESP（显示敌人 / 显示采集）----------------
    // 数据来自游戏内 Lua 引擎（约 150ms 一轮）写出的 FSDTrainer_esp.txt：
    //   E <左> <上> <右> <下> <距离米>   敌人
    //   M <左> <上> <右> <下> <距离米>   矿物
    // 兼容旧的三元组格式 <屏幕x> <屏幕y> <距离>。坐标是游戏视口像素，画前按 viewW/viewH 换算。
    bool Ui::EspWanted() {
        for (auto& f : g_features.list) {
            if (f.kind != "cheat") continue;
            if ((f.key == "esp_enemies" || f.key == "esp_minerals" || f.key == "esp_team" || f.key == "esp_radar") && f.cur > 0) return true;
        }
        return false;
    }

    void Ui::LoadEsp() {
        ULONGLONG now = GetTickCount64();
        if (now - espReadAt < 120) return;
        espReadAt = now;
        std::wstring dir(LogPath());
        size_t p = dir.find_last_of(L"\\/");
        dir = (p == std::wstring::npos) ? std::wstring() : dir.substr(0, p + 1);
        std::wstring file = dir + L"FSDTrainer_esp.txt";
        WIN32_FILE_ATTRIBUTE_DATA fa = {};
        if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &fa)) return;
        ULONGLONG t = ((ULONGLONG)fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime;
        if (t == espFileTime) return;
        espFileTime = t;
        FILE* f = nullptr;
        if (_wfopen_s(&f, file.c_str(), L"rb") != 0 || !f) return;
        std::vector<EspDot> tmp;
        char line[240];
        while (fgets(line, sizeof(line), f)) {
            // E/M = 敌人/矿物（v55 起格式：x1 y1 x2 y2 距离 生命 护甲 名字）
            // T   = 队友（x1 y1 x2 y2 距离 生命 护甲 倒地 名字）  R = 雷达点（方位角 距离 类型）
            if (line[0] != 'E' && line[0] != 'M' && line[0] != 'T' && line[0] != 'R') continue;
            if (line[0] == 'R') {
                float ang = 0, dist = 0; char kind = 'E';
                if (sscanf_s(line + 1, "%f %f %c", &ang, &dist, &kind, 1) < 2) continue;
                EspDot e; e.kind = L'R'; e.x1 = ang; e.y1 = dist; e.d = (kind == 'M' ? 2.f : (kind == 'T' ? 3.f : 1.f));
                tmp.push_back(e);
                continue;
            }
            float a1 = 0, a2 = 0, a3 = 0, a4 = 0, a5 = 0, a6 = -1, a7 = -1, a8 = 0;
            int got = sscanf_s(line + 1, "%f %f %f %f %f %f %f %f", &a1, &a2, &a3, &a4, &a5, &a6, &a7, &a8);
            EspDot e; e.kind = (wchar_t)line[0];
            if (got >= 5) {
                e.x1 = a1; e.y1 = a2; e.x2 = a3; e.y2 = a4; e.d = a5;
                int skipN = 5;
                if (line[0] == 'T') { e.hp = a6; e.sh = a7; e.down = (int)a8; skipN = 8; }
                else { e.hp = a6; e.sh = a7; skipN = 7; }        // v55：第 6/7 项是生命/护甲
                const char* np = line + 1;
                for (int t = 0; t < skipN && *np; ++t) {
                    while (*np == ' ' || *np == '\t') ++np;
                    while (*np && *np != ' ' && *np != '\t') ++np;
                }
                while (*np == ' ' || *np == '\t') ++np;
                if (*np) {
                    std::string nm8(np);
                    while (!nm8.empty() && (nm8.back() == '\r' || nm8.back() == '\n' || nm8.back() == ' ')) nm8.pop_back();
                    if (!nm8.empty()) {
                        int wn = MultiByteToWideChar(CP_UTF8, 0, nm8.data(), (int)nm8.size(), nullptr, 0);
                        if (wn > 0) { std::wstring w(wn, L'\0'); MultiByteToWideChar(CP_UTF8, 0, nm8.data(), (int)nm8.size(), &w[0], wn); e.name = w; }
                    }
                }
            } else if (got == 3) {                 // 旧格式：中心点 + 距离 → 按距离折算一个方块
                int d = (int)a3; if (d < 1) d = 1; if (d > 60) d = 60;
                float sz = (float)(S(30) * 20 / d);
                if (sz < S(8)) sz = S(8);
                if (sz > S(56)) sz = S(56);
                e.x1 = a1 - sz / 2; e.y1 = a2 - sz / 2; e.x2 = a1 + sz / 2; e.y2 = a2 + sz / 2; e.d = a3;
            } else continue;
            tmp.push_back(e);
        }
        fclose(f);
        espDots.swap(tmp);
        espDirty = true;
    }

    void Ui::DrawEsp() {
        if (espDots.empty()) return;
        const int vw = viewW > 0 ? viewW : width;
        const int vh = viewH > 0 ? viewH : height;
        // ---- 雷达点先收集，最后统一画在左下角的小圆盘上 ----
        int radar[64][3], nRadar = 0;
        for (auto& e : espDots) {
            if (e.kind == L'R') {
                if (nRadar < 64) { radar[nRadar][0] = (int)e.x1; radar[nRadar][1] = (int)e.y1; radar[nRadar][2] = (int)e.d; nRadar++; }
                continue;
            }
            int x1 = MulDiv((int)e.x1, width, vw), y1 = MulDiv((int)e.y1, height, vh);
            int x2 = MulDiv((int)e.x2, width, vw), y2 = MulDiv((int)e.y2, height, vh);
            if (x2 < x1) { int t = x1; x1 = x2; x2 = t; }
            if (y2 < y1) { int t = y1; y1 = y2; y2 = t; }
            int w = x2 - x1, h = y2 - y1;
            if (w < S(4) || h < S(4)) continue;
            if (x1 >= width || y1 >= height || x2 < 0 || y2 < 0) continue;
            COLORREF c = (e.kind == L'E') ? RGB(255, 70, 70) : ((e.kind == L'T') ? RGB(90, 255, 120) : RGB(90, 225, 255));
            Border(x1, y1, w, h, RGB(0, 0, 0), 170);
            Border(x1 + 1, y1 + 1, w - 2, h - 2, c, 235);
            // 标签：名字 + 距离（+ v55：生命/护甲，仿 DX11 修改器的写法）
            std::wstring label = e.name;
            if (!label.empty()) label += L" ";
            wchar_t db[64] = L"";
            if (e.hp >= 0 || e.sh >= 0) {
                swprintf_s(db, L"%dm 生命%d 甲%d", (int)e.d, (int)(e.hp < 0 ? 0 : e.hp), (int)(e.sh < 0 ? 0 : e.sh));
            } else {
                swprintf_s(db, L"%dm", (int)e.d);
            }
            label += db;
            if (e.kind == L'T' && e.down) label = L"☠ " + label + L" 倒地";
            int tw = TextW(label);
            int lx = x1 + w / 2 - tw / 2;
            if (lx < 0) lx = 0;
            Text(lx, y1 - LineH(), label, c, 255, true);
            Text(lx + 1, y1 - LineH() + 1, label, RGB(0, 0, 0), 120, true);
        }
        // ---- 雷达：左下角圆盘（外圈 100m），正前方朝上 ----
        if (nRadar > 0) {
            int R = S(78), cx = S(20) + R, cy = height - S(20) - R;
            Fill(cx - R, cy - R, R * 2, R * 2, RGB(10, 12, 16), 120);
            // 圆盘边框（用点画圆，避免实心遮太多）
            for (int i = 0; i < 360; i += 3) {
                double a = i * 3.14159265 / 180.0;
                int px = cx + (int)(R * cos(a)), py = cy + (int)(R * sin(a));
                Fill(px - S(1), py - S(1), S(2), S(2), RGB(200, 210, 230), 200);
            }
            Fill(cx - S(1), cy - S(1), S(2), S(2), RGB(255, 255, 255), 230);
            Fill(cx - S(1), cy - R, S(2), S(5), RGB(255, 255, 255), 200);      // 正前方刻度
            for (int i = 0; i < nRadar; ++i) {
                double ang = radar[i][0] * 3.14159265 / 180.0;                 // 0=正前，顺时针
                double rr = radar[i][1] / 100.0; if (rr > 1.0) rr = 1.0;
                int px = cx + (int)(sin(ang) * R * rr), py = cy - (int)(cos(ang) * R * rr);
                COLORREF c = (radar[i][2] == 2) ? RGB(90, 225, 255) : ((radar[i][2] == 3) ? RGB(90, 255, 120) : RGB(255, 80, 80));
                Fill(px - S(2), py - S(2), S(4), S(4), c, 235);
            }
        }
    }

    // ---------------- 自瞄叠层（显示索敌范围）----------------
    // 数据来自游戏内 Lua 引擎（30ms 一轮）写出的 FSDTrainer_aim.txt：
    //   R <屏幕x> <屏幕y>                    索敌圈上的一个点（水平圆，半径 = 自瞄范围 米）
    //   A <左> <上> <右> <下> <距离> <名字>   当前锁定的敌人
    bool Ui::AimWanted() {
        for (auto& f : g_features.list) {
            if (f.kind != "cheat") continue;
            if (f.key == "aim_show_range" && f.cur > 0) return true;
        }
        return false;
    }

    void Ui::LoadAim() {
        ULONGLONG now = GetTickCount64();
        if (now - aimReadAt < 100) return;
        aimReadAt = now;
        std::wstring dir(LogPath());
        size_t p = dir.find_last_of(L"\\/");
        dir = (p == std::wstring::npos) ? std::wstring() : dir.substr(0, p + 1);
        std::wstring file = dir + L"FSDTrainer_aim.txt";
        WIN32_FILE_ATTRIBUTE_DATA fa = {};
        if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &fa)) return;
        ULONGLONG t = ((ULONGLONG)fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime;
        if (t == aimFileTime) return;
        aimFileTime = t;
        FILE* f = nullptr;
        if (_wfopen_s(&f, file.c_str(), L"rb") != 0 || !f) return;
        std::vector<EspDot> tmp;
        char line[220];
        while (fgets(line, sizeof(line), f)) {
            if (line[0] != 'R' && line[0] != 'A') continue;
            float a1 = 0, a2 = 0, a3 = 0, a4 = 0, a5 = 0;
            int got = sscanf_s(line + 1, "%f %f %f %f %f", &a1, &a2, &a3, &a4, &a5);
            EspDot e; e.kind = (wchar_t)line[0];
            if (line[0] == 'R') {
                if (got < 2) continue;
                e.x1 = a1; e.y1 = a2;
            } else {
                if (got < 5) continue;
                e.x1 = a1; e.y1 = a2; e.x2 = a3; e.y2 = a4; e.d = a5;
                const char* np = line + 1;                 // 第 6 项起是名字（UTF-8）
                for (int k = 0; k < 5 && *np; ++k) {
                    while (*np == ' ' || *np == '\t') ++np;
                    while (*np && *np != ' ' && *np != '\t') ++np;
                }
                while (*np == ' ' || *np == '\t') ++np;
                if (*np) {
                    std::string nm8(np);
                    while (!nm8.empty() && (nm8.back() == '\r' || nm8.back() == '\n' || nm8.back() == ' ')) nm8.pop_back();
                    if (!nm8.empty()) {
                        int wn = MultiByteToWideChar(CP_UTF8, 0, nm8.data(), (int)nm8.size(), nullptr, 0);
                        if (wn > 0) { std::wstring w(wn, L'\0'); MultiByteToWideChar(CP_UTF8, 0, nm8.data(), (int)nm8.size(), &w[0], wn); e.name = w; }
                    }
                }
            }
            tmp.push_back(e);
        }
        fclose(f);
        aimDots.swap(tmp);
        aimDirty = true;
    }

    void Ui::DrawAim() {
        // 1) 索敌范围：屏幕中心展开的白色空心细圈（半径由「自瞄范围」决定，只跟数值有关，不受目标影响）
        for (auto& f : g_features.list) {
            if (f.kind != "cheat") continue;
            if (f.key == "aim_show_range") {
                if (f.cur <= 0) break;                       // 开关没开：不画圈
                double rng = 30.0;
                for (auto& g : g_features.list)
                    if (g.kind == "cheat" && g.key == "aim_range") rng = g.cur;
                if (rng < 1) rng = 1;
                int rad = (int)(S(4) * rng);                 // 约 4 像素/米（跟着分辨率缩放）
                int maxR = (width < height ? width : height) / 2 - S(6);
                if (rad < S(24)) rad = S(24);
                if (rad > maxR) rad = maxR;
                int cx = width / 2, cy = height / 2;
                int dot = S(2);
                // 整数中点画圆（薄空心圈，不用数学库）
                auto plot = [&](int px, int py) {
                    if (px < 0 || py < 0 || px >= width || py >= height) return;
                    Fill(px - dot / 2, py - dot / 2, dot, dot, RGB(255, 255, 255), 230);
                };
                int x0 = rad, y0 = 0, err = 1 - rad;
                while (x0 >= y0) {
                    plot(cx + x0, cy + y0); plot(cx + y0, cy + x0);
                    plot(cx - y0, cy + x0); plot(cx - x0, cy + y0);
                    plot(cx - x0, cy - y0); plot(cx - y0, cy - x0);
                    plot(cx + y0, cy - x0); plot(cx + x0, cy - y0);
                    ++y0;
                    if (err < 0) err += 2 * y0 + 1;
                    else { --x0; err += 2 * (y0 - x0) + 1; }
                }
                break;
            }
        }
        // 2) 当前锁定的敌人（游戏内引擎写来的 A 行）
        if (aimDots.empty()) return;
        const int vw = viewW > 0 ? viewW : width;
        const int vh = viewH > 0 ? viewH : height;
        for (auto& e : aimDots) {
            if (e.kind == L'R') {
                int px = MulDiv((int)e.x1, width, vw), py = MulDiv((int)e.y1, height, vh);
                if (px < 0 || py < 0 || px >= width || py >= height) continue;
                int s = S(3);
                Fill(px - s / 2, py - s / 2, s, s, RGB(70, 255, 140), 205);
                continue;
            }
            int x1 = MulDiv((int)e.x1, width, vw), y1 = MulDiv((int)e.y1, height, vh);
            int x2 = MulDiv((int)e.x2, width, vw), y2 = MulDiv((int)e.y2, height, vh);
            if (x2 < x1) { int t = x1; x1 = x2; x2 = t; }
            if (y2 < y1) { int t = y1; y1 = y2; y2 = t; }
            int w = x2 - x1, h = y2 - y1;
            if (w < S(4) || h < S(4)) continue;
            if (x1 >= width || y1 >= height || x2 < 0 || y2 < 0) continue;
            COLORREF c = RGB(255, 215, 60);            // 锁定目标：黄色
            Border(x1, y1, w, h, RGB(0, 0, 0), 170);
            Border(x1 + 1, y1 + 1, w - 2, h - 2, c, 240);
            std::wstring label = L"锁 ";
            if (!e.name.empty()) { label += e.name; label += L" "; }
            wchar_t db[32] = L"";
            swprintf_s(db, L"%dm", (int)e.d);
            label += db;
            int tw = TextW(label);
            int lx = x1 + w / 2 - tw / 2;
            if (lx < 0) lx = 0;
            Text(lx, y1 - LineH(), label, c, 255, true);
            Text(lx + 1, y1 - LineH() + 1, label, RGB(0, 0, 0), 120, true);
        }
    }

    // ---------------- 主入口 ----------------
    // 菜单复位：Home 键调用，防止菜单被拖到屏幕外找不回来
    void Ui::ResetWindow() {
        posInit = false;                       // 下一帧重新居中
        dragging = false;
        resizeZone = 0;
        winSizeW = winSizeH = 0;               // 大小也恢复自动
        WritePrivateProfileStringW(L"general", L"ui_win_w", L"0", g_configPathW.c_str());
        WritePrivateProfileStringW(L"general", L"ui_win_h", L"0", g_configPathW.c_str());
        needClear = false;
        visible = true;                        // 顺便把菜单显示出来
        Log("菜单已复位（居中 + 恢复自动大小）");
        note = L"菜单已复位到屏幕中央";
    }

    void Ui::Toggle() {
        visible = !visible;
        if (visible) { dirty = true; note = L"菜单已呼出"; }
        else { needClear = true; }
        Log("菜单 %s", visible ? "打开" : "关闭");
    }

    bool Ui::Build(const FrameInput& fin) {
        in = &fin;
        qchars = fin.chars;
        qkeys = fin.keys;

        if (!visible) {
            // 菜单隐藏但 ESP / 自瞄叠层开着：只在数据更新时重画一次（约 1 次/秒），不做每帧上传
            bool wantE = EspWanted(), wantA = AimWanted();
            if (wantE || wantA) {
                if (wantE) LoadEsp();
                if (wantA) LoadAim();
                // 自瞄"显示范围"的白圈是常驻 HUD（跟数据无关）：菜单关掉后也要一直看得见，
                // 所以额外按 4Hz 心跳重画一次，防止被别的清屏/重绘吃掉。
                ULONGLONG t2 = GetTickCount64();
                bool beat = wantA && (t2 - aimDrawAt > 250);
                if (needClear || espDirty || aimDirty || beat) {
                    if (beat) aimDrawAt = t2;
                    needClear = false; espDirty = false; aimDirty = false;
                    ClearAll(); if (wantE) DrawEsp(); if (wantA) DrawAim(); dirty = true; MaybeDump(); return true;
                }
                return false;
            }
            if (needClear) { needClear = false; ClearAll(); dirty = true; return true; }
            return false;
        }
        ULONGLONG now = GetTickCount64();
        if (now - lastDraw < 40) return false;      // 25Hz 重绘
        lastDraw = now;

        // 页签 = 功能表的分类（来自 ini 的 cat=） + 设置（+ 高级页）
        // v61：「调试数据」不再是普通页签，归到"高级功能"里（「显示高级功能」关闭时看不到它）
        static std::vector<std::string> cats;
        cats = g_features.Categories();
        bool hasDebug = false;
        for (auto it = cats.begin(); it != cats.end(); ) {
            if (*it == "调试数据") { hasDebug = true; it = cats.erase(it); } else ++it;
        }
        const int nCat = (int)cats.size();
        const int nAdv = advanced ? (2 + (hasDebug ? 1 : 0)) : 0;
        const int nTab = nCat + 1 + nAdv;
        if (tab >= nTab) tab = 0;
        for (size_t i = 0; i < qkeys.size();) {
            if (qkeys[i] == VK_TAB && activeInput < 0) { tab = (tab + 1) % (nTab > 0 ? nTab : 1); qkeys.erase(qkeys.begin() + i); }
            else ++i;
        }

        ClearAll();
        int winW = width - S(40), winH = height - S(40);
        if (winW > S(1200)) winW = S(1200);
        if (winH > S(940)) winH = S(940);
        if (winW < S(600)) winW = width - S(20);
        if (winH < S(400)) winH = height - S(20);
        if (winSizeW > 0) { winW = winSizeW; if (winW > width - S(10)) winW = width - S(10); if (winW < S(520)) winW = S(520); }
        if (winSizeH > 0) { winH = winSizeH; if (winH > height - S(10)) winH = height - S(10); if (winH < S(320)) winH = S(320); }
        if (!posInit) { winPosX = (width - winW) / 2; winPosY = (height - winH) / 2; posInit = true; }
        if (winPosX < -winW + S(160)) winPosX = -winW + S(160);
        if (winPosX > width - S(160)) winPosX = width - S(160);
        if (winPosY < 0) winPosY = 0;
        if (winPosY > height - S(50)) winPosY = height - S(50);
        int wx = winPosX, wy = winPosY;
        int titleH = S(34);

        // ---- 拖边框缩放：判断鼠标是否贴在菜单边缘（可组合：1=右 2=下 4=左 8=上）----
        {
            const int G = S(7);
            int zone = 0;
            if (in && in->mx >= 0 && in->my >= 0) {
                bool inX = in->mx >= wx - G && in->mx <= wx + winW + G;
                bool inY = in->my >= wy - G && in->my <= wy + winH + G;
                if (inX && inY) {
                    if (in->mx >= wx + winW - G) zone |= 1;
                    if (in->my >= wy + winH - G) zone |= 2;
                    if (in->mx <= wx + G) zone |= 4;
                    if (in->my <= wy + G) zone |= 8;
                }
            }
            // 标题栏那条（除了最左/最右）留给"移动"，避免和缩放抢
            bool titleBand = (in && in->my > wy + G && in->my < wy + titleH && !(zone & 1) && !(zone & 4));
            if (zone && !titleBand && in && (in->lclick || in->ldown) && resizeZone == 0 && !dragging) {
                resizeZone = zone;
                rzW0 = winW; rzH0 = winH; rzX0 = wx; rzY0 = wy;
                rzMX0 = in->mx; rzMY0 = in->my;
                const_cast<FrameInput*>(in)->lclick = false;
                Log("缩放开始 区域=%d", zone);
            }
        }
        if (resizeZone && in) {
            if (in->ldown) {
                int dx = in->mx - rzMX0, dy = in->my - rzMY0;
                int nw = rzW0, nh = rzH0, nx = rzX0, ny = rzY0;
                if (resizeZone & 1) nw = rzW0 + dx;
                if (resizeZone & 2) nh = rzH0 + dy;
                if (resizeZone & 4) { nw = rzW0 - dx; nx = rzX0 + dx; }
                if (resizeZone & 8) { nh = rzH0 - dy; ny = rzY0 + dy; }
                if (nw < S(520)) { if (resizeZone & 4) nx = rzX0 + (rzW0 - S(520)); nw = S(520); }
                if (nh < S(320)) { if (resizeZone & 8) ny = rzY0 + (rzH0 - S(320)); nh = S(320); }
                if (nw > width - S(10)) nw = width - S(10);
                if (nh > height - S(10)) nh = height - S(10);
                winSizeW = nw; winSizeH = nh;
                winPosX = nx; winPosY = ny;
                winW = nw; winH = nh; wx = nx; wy = ny;
            } else {
                resizeZone = 0;
                wchar_t b[16] = L"";
                swprintf_s(b, L"%d", winSizeW);
                WritePrivateProfileStringW(L"general", L"ui_win_w", b, g_configPathW.c_str());
                swprintf_s(b, L"%d", winSizeH);
                WritePrivateProfileStringW(L"general", L"ui_win_h", b, g_configPathW.c_str());
                Log("缩放结束 菜单大小=%dx%d（已保存）", winSizeW, winSizeH);
                note = L"菜单大小已保存（下次启动仍生效）";
            }
        }

        // 按住标题栏拖动菜单
        bool onTitle = in && in->mx >= wx && in->mx < wx + winW && in->my >= wy && in->my < wy + titleH;
        if (in && (in->lclick || in->ldown) && onTitle && !dragging && resizeZone == 0) {
            dragging = true;
            dragGrabX = in->mx - wx;
            dragGrabY = in->my - wy;
            const_cast<FrameInput*>(in)->lclick = false;
            Log("拖动开始 鼠标=(%d,%d) 抓取=(%d,%d)", in->mx, in->my, dragGrabX, dragGrabY);
        }
        if (dragging) {
            if (in && in->ldown) {
                winPosX = in->mx - dragGrabX;
                winPosY = in->my - dragGrabY;
                // 夹紧到画布内，保证标题栏可见
                if (winPosX < -winW + S(160)) winPosX = -winW + S(160);
                if (winPosX > width - S(160)) winPosX = width - S(160);
                if (winPosY < 0) winPosY = 0;
                if (winPosY > height - S(50)) winPosY = height - S(50);
                wx = winPosX; wy = winPosY;     // 继续绘制：菜单跟随光标
            } else {
                dragging = false;
                Log("拖动结束 窗口=(%d,%d)", winPosX, winPosY);
            }
        }

        Fill(wx, wy, winW, winH, kColBg, 255);
        Border(wx, wy, winW, winH, kColAcc, 255);
        Fill(wx + 1, wy + 1, winW - 2, titleH, RGB(24, 26, 34), 255);
        Text(wx + S(12), wy + S(7), L"卡尔的大手 (Carlhand) · 深岩银河 DX12 修改器", kColAcc, 255, true);
        TextF(wx + S(370), wy + S(7), kColDim, L"[拖标题移动 · 拖边框缩放]");
        wchar_t infoBuf[160];
        swprintf_s(infoBuf, L"%s  菜单 %s  杀死 %s  结果 %zu", FSDTRAINER_VER,
                   W(HotkeyName(g_menuKey)).c_str(), W(HotkeyName(g_killKey)).c_str(), sc->Count());
        {
            std::wstring info = infoBuf;
            int iw = TextW(info);
            int ix = wx + winW - S(160) - iw;
            if (ix > wx + S(330)) Text(ix, wy + S(7), info, kColDim);
        }

        int tx = wx + S(12), ty = wy + S(42);
        int tw0 = S(92);
        int avail = (winW - S(24) - S(6) * (nTab - 1)) / (nTab > 0 ? nTab : 1);
        if (tw0 > avail) tw0 = avail < S(40) ? S(40) : avail;
        for (int i = 0; i < nTab; ++i) {
            std::wstring tmp;
            const wchar_t* nm = L"设置";
            if (i < nCat) { tmp = W(cats[i].c_str()); nm = tmp.c_str(); }
            else if (i > nCat) {
                static const wchar_t* advNames[] = { L"扫描", L"冻结", L"调试数据" };
                int k = i - nCat - 1;
                nm = (k >= 0 && k < 3) ? advNames[k] : L"?";
            }
            if (Button(tx + i * (tw0 + S(6)), ty, tw0, S(30), nm, tab == i)) tab = i;
        }

        int cx = wx + S(16), cy = ty + S(44);
        int cw = winW - S(32), ch = winH - (cy - wy) - S(44);
        Fill(cx - S(6), cy - S(6), cw + S(12), ch + S(12), kColPanel, 255);
        Border(cx - S(6), cy - S(6), cw + S(12), ch + S(12), kColLine);
        if (tab < nCat) PageGame(cx, cy, cw, ch, cats[tab]);
        else if (tab == nCat) PageSettings(cx, cy, cw, ch);
        else {
            // 高级页：扫描 / 冻结 / 调试数据（补丁 / 命令 / 参考三页按用户要求删除）
            switch (tab - nCat - 1) {
            case 0: PageScan(cx, cy, cw, ch); break;
            case 2: PageGame(cx, cy, cw, ch, "调试数据"); break;
            default: PageFreeze(cx, cy, cw, ch); break;
            }
        }
        if (!note.empty()) TextF(wx + S(16), wy + winH - S(30), kColAcc2, L"● %s", note.c_str());

        // ESP / 自瞄叠层画在最上层（菜单开着也显示）
        if (EspWanted()) { LoadEsp(); DrawEsp(); }
        if (AimWanted()) { LoadAim(); DrawAim(); }

        // 软件光标：全屏独占 / 游戏隐藏光标时菜单仍然可见可点
        if (in && in->mx >= 0 && in->my >= 0) {
            int cx = in->mx, cy = in->my;
            Fill(cx - S(1), cy - S(11), S(3), S(23), kColAcc, 235);
            Fill(cx - S(11), cy - S(1), S(23), S(3), kColAcc, 235);
            Fill(cx - S(3), cy - S(3), S(7), S(7), RGB(255, 255, 255), 245);
        }
        dirty = true;

        // 调试：设置了 FSDTRAINER_DUMP_UI 时周期性导出界面（便于自动验证）
        MaybeDump();
        pendingFocus = -1;      // 没被下一行输入框消费掉就作废，避免下一帧乱跳
        return true;
    }
}
