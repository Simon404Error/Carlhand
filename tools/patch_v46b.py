# v46：更细的 CheatManager 诊断 + 执行器改成优先创建 FSDCheatManager
import io
root = r'D:\Deepseek-Harness\work\260918_143041'

# ---------- 1) 引擎诊断加强 ----------
p = root + r'\src\cheat_engine.h'
s = io.open(p, encoding='utf-8').read()
def rep(old, new, n=1):
    global s
    assert old in s, 'anchor: ' + old[:90]
    s = s.replace(old, new, n)

rep('R"FSDLUA(-- Carlhand 功能引擎 v45', 'R"FSDLUA(-- Carlhand 功能引擎 v46')
rep('''if _G.__FSDT_ENGINE_V45 then return end
_G.__FSDT_ENGINE_V45 = true''', '''if _G.__FSDT_ENGINE_V46 then return end
_G.__FSDT_ENGINE_V46 = true''')
rep('W("\\n>> 功能引擎 v45 已载入 "', 'W("\\n>> 功能引擎 v46 已载入 "')

old_diag = '''  if not cmDiagDone then
    cmDiagDone = true
    local name="-"
    if c~=nil then local okn,nmv=pcall(function() return c:GetFullName() end) name = okn and tostring(nmv) or "取名字失败" end
    local okl,ll = pcall(FindAllOf,"FSDCheatManager")
    local cnt = (okl and type(ll)=="table") and #ll or -1
    local okc,cc = pcall(FindAllOf,"CheatManager")
    local cnt2 = (okc and type(cc)=="table") and #cc or -1
    W("  [诊断] pc.CheatManager="..type(c).." "..name.."  FSDCheatManager="..tostring(cnt).."  CheatManager="..tostring(cnt2))
  end'''
new_diag = '''  if not cmDiagDone then
    cmDiagDone = true
    local info={}
    info[#info+1]="type="..type(c)
    if c~=nil then
      local oka,ad=pcall(function() return c:GetAddress() end) info[#info+1]="addr="..(oka and string.format("0x%X",ad) or "失败")
      local okt,tp=pcall(function() return c:type() end) info[#info+1]="UE4SS类型="..(okt and tostring(tp) or "失败")
      local okc2,cl=pcall(function() return c:GetClass():GetFullName() end) info[#info+1]="类="..(okc2 and tostring(cl) or "失败")
      for _,fn in ipairs({"SetFastMovement","IsFastMovementActive","SetQuadPickAxe","SetFlyMode","IsFlyModeActive","SetGodMode"}) do
        local okf,v=pcall(function() return c[fn] end)
        info[#info+1]=fn.."="..(okf and type(v) or "取不到")
      end
    end
    W("  [诊断] "..table.concat(info,"  "))
  end'''
rep(old_diag, new_diag)

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('engine diag patched')

# ---------- 2) dllmain 里的执行器：优先创建 FSDCheatManager ----------
p = root + r'\src\dllmain.cpp'
s = io.open(p, encoding='utf-8').read()
old = '''    local CheatManagerClass = PlayerController.CheatClass
    if not CheatManagerClass:IsValid() then
        print("[CheatManager Creator] Controller:CheatClass is nullptr, using default CheatClass instead\\n")
        
        CheatManagerClass = StaticFindObject("/Script/Engine.CheatManager")
    end'''
new = '''    local function clsName(o)
        if not o then return "nil" end
        local ok,c=pcall(function() return o:GetClass():GetFullName() end)
        if ok and type(c)=="string" then return c end
        return "?"
    end
    -- 已经有游戏的 FSDCheatManager 就别重复造（否则会把真正带作弊函数的那个换掉）
    local existing = PlayerController.CheatManager
    if existing ~= nil then
        local okv,v = pcall(function() return existing:IsValid() end)
        if okv and v and string.find(clsName(existing) or "", "FSDCheatManager", 1, true) then
            print("[CheatManager Creator] 已有 FSDCheatManager，跳过\\n")
            return
        end
    end
    -- 优先游戏自己的 FSDCheatManager（C_XXX 那些函数都在它身上）
    local CheatManagerClass = StaticFindObject("/Script/FSD.FSDCheatManager")
    if not (CheatManagerClass and CheatManagerClass:IsValid()) then
        CheatManagerClass = PlayerController.CheatClass
    end
    if not (CheatManagerClass and CheatManagerClass:IsValid()) then
        print("[CheatManager Creator] Controller:CheatClass is nullptr, using default CheatClass instead\\n")
        CheatManagerClass = StaticFindObject("/Script/Engine.CheatManager")
    end'''
assert old in s, 'enabler anchor missing'
s = s.replace(old, new, 1)
s = s.replace('print(string.format("[CheatManager Creator] Constructed CheatManager [0x%X]\\n", CreatedCheatManager:GetAddress()))',
              'print(string.format("[CheatManager Creator] Constructed %s [0x%X]\\n", clsName(CreatedCheatManager), CreatedCheatManager:GetAddress()))', 1)
io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('dllmain enabler patched')
