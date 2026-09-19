# PageGame 支持上下滚动（滚轮）+ 透明度默认 100% + Home 复位键
import io, re

root = r'D:\Deepseek-Harness\work\260918_143041'

# ---------- A) PageGame 重写（加滚动）----------
p = root + r'\src\ui.cpp'
lines = io.open(p, encoding='utf-8').read().split('\n')
start = 393 - 1          # void Ui::PageGame(...)
end = 574 - 1            # if (bindIdx != ...) 之前
assert 'void Ui::PageGame' in lines[start], lines[start]
assert 'if (bindIdx != (size_t)-1)' in lines[end], lines[end]

new = r'''    void Ui::PageGame(int x, int y, int w, int h, const std::string& cat) {
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
        if (Button(x + w - S(170), cy - S(4), S(160), LineH() + S(8), L"全部关闭（恢复）")) {
            g_features.AllOff(); RestoreGameState(); note = L"已全部关闭并恢复原状";
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
        Fill(x - S(4), cy - S(3), w - S(8), S(1), kColLine, 220);

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
                if (hov) Fill(x - S(4), dcy - S(2), w - S(8), rh, RGB(48, 52, 68), 160);
                else if ((i & 1u) == 0) Fill(x - S(4), dcy - S(2), w - S(8), rh, RGB(29, 32, 41), 130);
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
            if (valBuf[i].empty() && !editing) { wchar_t b[32]; swprintf_s(b, L"%.2f", f.cur); valBuf[i] = b; }
            // 输入框左侧的 － / ＋ 按钮：只改数值，不立刻下发
            {
                auto nudge = [&](double d) {
                    double v = _wtof(valBuf[i].c_str()) + d;
                    wchar_t b[32]; swprintf_s(b, L"%.2f", v);
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
                wchar_t bb[32]; swprintf_s(bb, L"%.2f", vv); valBuf[i] = bb;
                bool moved = Slider(2000 + (int)i, x + S(310), dcy, S(150), LineH() + S(10), vv, f.lo, f.hi);
                bool typed = InputBox(3000 + (int)i, x + S(470), dcy, S(80), LineH() + S(10), valBuf[i], true);
                if ((moved || typed) && (f.kind == "cheat" || f.addr)) {
                    double use = moved ? vv : _wtof(valBuf[i].c_str());
                    g_features.SetValue(i, use);
                    if (moved) { wchar_t b2[32]; swprintf_s(b2, L"%.2f", vv); valBuf[i] = b2; }
                    note = L"已应用";
                }
            } else {
                entered = InputBox(2000 + (int)i, x + S(310), dcy, S(100), LineH() + S(10), valBuf[i], true);
            }
            if (entered) {
                if (f.kind == "cheat" || f.addr) { g_features.SetValue(i, _wtof(valBuf[i].c_str())); note = L"已应用（回车）"; }
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
                else { g_features.SetValue(i, _wtof(valBuf[i].c_str())); note = L"已应用"; }
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
                        if (Button(bx, by, rowW, LineH() + S(8), label, f.ui == "toggle" && f.on, true)) {
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
                Fill(x + w - S(1), contentTop, S(5), visH, RGB(32, 35, 44), 150);
                Fill(x + w - S(1), barY, S(5), barH, kColAcc, 210);
                TextF(x, y + h - LineH() - S(2), kColDim, L"滚轮上下滚动");
            }
        }

'''
out = lines[:start] + new.split('\n') + lines[end:]
io.open(p, 'w', encoding='utf-8', newline='').write('\n'.join(out))
print('PageGame rewritten with scrolling')

# ---------- B) ui.h: 滚动成员 + 透明度默认 100 ----------
p = root + r'\src\ui.h'
s = io.open(p, encoding='utf-8').read()
s = s.replace('int   opacity = 95;          // 菜单整体不透明度（%），默认 95',
              'int   opacity = 100;         // 菜单整体不透明度（%），100=完全不透明（默认）')
s = s.replace('int          rzW0 = 0, rzH0 = 0, rzX0 = 0, rzY0 = 0, rzMX0 = 0, rzMY0 = 0;',
              'int          rzW0 = 0, rzH0 = 0, rzX0 = 0, rzY0 = 0, rzMX0 = 0, rzMY0 = 0;\n'
              '        int          gscroll = 0, gscrollMax = 0;   // 功能页滚动')
s = s.replace('        void Toggle();', '        void Toggle();\n        void ResetWindow();                            // 菜单复位（Home 键）')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ui.h patched')

# ---------- C) ui.cpp: 透明度默认值/复位函数 ----------
p = root + r'\src\ui.cpp'
s = io.open(p, encoding='utf-8').read()
s = s.replace('int op = GetPrivateProfileIntW(L"general", L"ui_opacity", 95, g_configPathW.c_str());',
              'int op = GetPrivateProfileIntW(L"general", L"ui_opacity", 100, g_configPathW.c_str());')
s = s.replace('if (Button(x + S(346), cy - S(3), S(90), LineH() + S(8), L"恢复95%")) {\n                opacity = 95; saveOp(); note = L"透明度已恢复 95%";',
              'if (Button(x + S(346), cy - S(3), S(100), LineH() + S(8), L"恢复100%")) {\n                opacity = 100; saveOp(); note = L"透明度已恢复 100%（完全不透明）";')
# 复位函数（放在 Toggle 后面）
anchor = '''    void Ui::Toggle() {'''
add = '''    // 菜单复位：Home 键调用，防止菜单被拖到屏幕外找不回来
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

'''
assert anchor in s
s = s.replace(anchor, add + anchor, 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('ui.cpp patched')
