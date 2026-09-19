import io, re
p = r'D:\Deepseek-Harness\work\260918_143041\out\verify\FSDTrainer.ini'
s = io.open(p, encoding='utf-16').read()
# 只在 [feature.显示敌人] 节里把 def=0 改成 def=30，把 menu_open 改成 0（隐藏菜单，测 ESP 隐藏路径）
lines = s.split('\n')
cur = None
out = []
for ln in lines:
    st = ln.strip()
    if st.startswith('[feature.'):
        cur = st
    if cur == '[feature.显示敌人]' and st == 'def=0':
        ln = ln.replace('def=0', 'def=30')
    if st.startswith('menu_open='):
        ln = ln.replace('menu_open=1', 'menu_open=0')
    out.append(ln)
s2 = '\n'.join(out)
io.open(p, 'w', encoding='utf-16', newline='').write(s2)
print([l for l in s2.split('\n') if 'menu_open' in l])
print(s2.split('[feature.显示敌人]')[1][:120])
