# v46：背景不透明 100% = 真正不透明（把会"打洞"的半透明填充改成不透明）+ 设置页改名
import io
p = r'D:\Deepseek-Harness\work\260918_143041\src\ui.cpp'
s = io.open(p, encoding='utf-8').read()

def rep(old, new, n=1):
    global s
    assert old in s, 'anchor: ' + old[:90]
    s = s.replace(old, new, n)

# ---- 1) 会"打洞"的半透明填充 → 不透明（否则会把下面那层的不透明度也改成半透明）----
rep('            Fill(x + S(3), y + S(3), TextW(buf) + S(6), h - S(6), kColAcc, 70);   // 选中高亮',
    '            Fill(x + S(3), y + S(3), TextW(buf) + S(6), h - S(6), RGB(40, 56, 84), 255);   // 选中高亮（不透明，避免把菜单打成半透明）')
rep('            Fill(cx, y + S(4), S(2), h - S(8), kColAcc, 220);',
    '            Fill(cx, y + S(4), S(2), h - S(8), kColAcc, 255);')
rep('        Fill(x + 1, y + 1, (w - 2) * pct / 100, h - 2, kColAcc, 220);',
    '        Fill(x + 1, y + 1, (w - 2) * pct / 100, h - 2, kColAcc, 255);')
rep('        Fill(x - S(4), cy - S(3), w - S(8), S(1), kColLine, 220);',
    '        Fill(x - S(4), cy - S(3), w - S(8), S(1), kColLine, 255);')
rep('                if (hov) Fill(x - S(4), dcy - S(2), w - S(8), rh, RGB(48, 52, 68), 160);\n'
    '                else if ((i & 1u) == 0) Fill(x - S(4), dcy - S(2), w - S(8), rh, RGB(29, 32, 41), 130);',
    '                if (hov) Fill(x - S(4), dcy - S(2), w - S(8), rh, RGB(48, 52, 68), 255);\n'
    '                else if ((i & 1u) == 0) Fill(x - S(4), dcy - S(2), w - S(8), rh, RGB(29, 32, 41), 255);')
rep('                Fill(x + w - S(1), contentTop, S(5), visH, RGB(32, 35, 44), 150);\n'
    '                Fill(x + w - S(1), barY, S(5), barH, kColAcc, 210);',
    '                Fill(x + w - S(1), contentTop, S(5), visH, RGB(32, 35, 44), 255);\n'
    '                Fill(x + w - S(1), barY, S(5), barH, kColAcc, 255);')
rep('        Fill(x + 1, y + 1, fw, h - 2, kColAcc, 170);',
    '        Fill(x + 1, y + 1, fw, h - 2, kColAcc, 255);')
# 菜单主体三层：从默认半透明改成完全不透明（再由"背景不透明"统一调暗）
rep('        Fill(wx, wy, winW, winH, kColBg, 235);', '        Fill(wx, wy, winW, winH, kColBg, 255);')
rep('        Border(wx, wy, winW, winH, kColAcc, 220);', '        Border(wx, wy, winW, winH, kColAcc, 255);')
rep('        Fill(wx + 1, wy + 1, winW - 2, titleH, RGB(24, 26, 34), 245);', '        Fill(wx + 1, wy + 1, winW - 2, titleH, RGB(24, 26, 34), 255);')
rep('        Fill(cx - S(6), cy - S(6), cw + S(12), ch + S(12), kColPanel, 210);', '        Fill(cx - S(6), cy - S(6), cw + S(12), ch + S(12), kColPanel, 255);')

# ---- 2) 设置页改名 + 文案 ----
rep('            Text(x, cy + S(4), L"界面透明度", kColText, 255, true);',
    '            Text(x, cy + S(4), L"背景不透明", kColText, 255, true);')
rep('            if (Button(x + S(150), cy - S(3), S(50), LineH() + S(8), L"-") && opacity > 30) {\n'
    '                opacity -= 5; if (opacity < 30) opacity = 30;\n'
    '                saveOp(); note = L"菜单更透明了";\n'
    '            }',
    '            if (Button(x + S(150), cy - S(3), S(50), LineH() + S(8), L"-") && opacity > 30) {\n'
    '                opacity -= 5; if (opacity < 30) opacity = 30;\n'
    '                saveOp(); note = L"背景更透明了";\n'
    '            }')
rep('                saveOp(); note = L"菜单更不透明了";', '                saveOp(); note = L"背景更不透明了";')
rep('                opacity = 100; saveOp(); note = L"透明度已恢复 100%（完全不透明）";',
    '                opacity = 100; saveOp(); note = L"背景不透明已恢复 100%（完全看不到后面的游戏画面）";')

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('v46 patched')
