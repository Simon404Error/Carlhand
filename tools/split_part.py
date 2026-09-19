import io
p = r'D:\Deepseek-Harness\work\260918_143041\src\cheat_engine.h'
s = io.open(p, encoding='utf-8').read()
anchor = '-- ================= 移速 / 奔跑 ================='
assert anchor in s, 'anchor missing'
marker = ')FSDLUA",\nR"FSDLUA('
assert s.count(marker) == 4, s.count(marker)
s = s.replace(anchor, marker + anchor, 1)
s = s.replace('static const int kCheatEngineLuaPartCount = 5;', 'static const int kCheatEngineLuaPartCount = 6;')
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('split done')
