import re
s = open(r'D:\Deepseek-Harness\work\260918_143041\src\cheat_engine.h', encoding='utf-8').read()
parts = re.findall(r'R"FSDLUA\((.*?)\)FSDLUA"', s, re.S)
lua = ''.join(parts)
open(r'D:\Deepseek-Harness\work\260918_143041\build\obj\engine_check.lua', 'w', encoding='utf-8', newline='\n').write(lua)
print('written', len(lua.encode('utf-8')), 'bytes,', lua.count('\n')+1, 'lines')
