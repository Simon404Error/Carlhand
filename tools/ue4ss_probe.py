import sys, re
p = sys.argv[1]
d = open(p, 'rb').read()
print('==', p, len(d), 'bytes')
keys = ['LuaMadeSimple', 'lua_pcall', 'luaL_error', 'Git SHA #', 'd935b5b', '409cd33',
        'Lua mod', 'Starting Lua mod', 'LuaEnable', 'ue4ss', 'UE4SSL', 'Scripts', 'main.lua',
        'mods.txt', 'main.dll']
for k in keys:
    i = d.find(k.encode('utf-8'))
    print('  %-18s %s' % (k, ('found @0x%X' % i) if i >= 0 else '-'))
# all printable strings containing 'SHA #'
for m in re.finditer(rb'Git SHA #[0-9a-fA-F]{4,}', d):
    print('   SHA string:', m.group(0).decode())
for m in re.finditer(rb'UE4SS - v[0-9.]+[^\x00]{0,40}', d):
    print('   version string:', m.group(0).decode('latin1'))
