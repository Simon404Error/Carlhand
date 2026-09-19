# v45：CheatManager 取用修复 + 诊断（奔跑/镐子/飞行 之前 cm 是 nil）+ 补给仓多路尝试
import io
p = r'D:\Deepseek-Harness\work\260918_143041\src\cheat_engine.h'
s = io.open(p, encoding='utf-8').read()

def rep(old, new, n=1):
    global s
    assert old in s, 'anchor: ' + old[:90]
    s = s.replace(old, new, n)

rep('R"FSDLUA(-- Carlhand 功能引擎 v44', 'R"FSDLUA(-- Carlhand 功能引擎 v45')
rep('''if _G.__FSDT_ENGINE_V44 then return end
_G.__FSDT_ENGINE_V44 = true''',
'''-- v45：CheatManager 统一用 getCM() 取（先读 pc.CheatManager，再退到 FindAllOf("FSDCheatManager")），
--   并打印一次诊断；补给仓改成"cm 函数 + 控制台"多路尝试。
if _G.__FSDT_ENGINE_V45 then return end
_G.__FSDT_ENGINE_V45 = true''')
rep('W("\\n>> 功能引擎 v44 已载入 "', 'W("\\n>> 功能引擎 v45 已载入 "')

# 在 flySet 之前插入 getCM 与诊断
anchor = '-- 游戏自带"快速移动"的开关（带状态校验：IsFastMovementActive 能读回真实状态）'
add = '''-- 取 CheatManager：优先 pc.CheatManager；拿不到就找场上第一个 FSDCheatManager/CheatManager
local cmFixed, cmDiagDone = nil, false
local function getCM(pc)
  if valid(cmFixed) then return cmFixed end
  local c = pc and gp(pc,"CheatManager") or nil
  if not cmDiagDone then
    cmDiagDone = true
    local name="-"
    if c~=nil then local okn,nmv=pcall(function() return c:GetFullName() end) name = okn and tostring(nmv) or "取名字失败" end
    local okl,ll = pcall(FindAllOf,"FSDCheatManager")
    local cnt = (okl and type(ll)=="table") and #ll or -1
    local okc,cc = pcall(FindAllOf,"CheatManager")
    local cnt2 = (okc and type(cc)=="table") and #cc or -1
    W("  [诊断] pc.CheatManager="..type(c).." "..name.."  FSDCheatManager="..tostring(cnt).."  CheatManager="..tostring(cnt2))
  end
  if valid(c) then cmFixed=c return c end
  for _,cls in ipairs({"FSDCheatManager","CheatManager"}) do
    local ok,l=pcall(FindAllOf,cls)
    if ok and type(l)=="table" then
      for _,o in ipairs(l) do if valid(o) then cmFixed=o return o end end
    end
  end
  return nil
end

'''
rep(anchor, add + anchor)

# applySpeed/applyAim/applyFly 改用 getCM
rep('  local cm=gp(pc,"CheatManager")          -- 本函数在 chunk 层，看不到主循环的 local cm，必须现取',
    '  local cm=getCM(pc)                      -- 本函数在 chunk 层，看不到主循环的 local cm，必须现取')
rep('''local function applyAim(F,pawn,pc)
  local cm=gp(pc,"CheatManager")''',
'''local function applyAim(F,pawn,pc)
  local cm=getCM(pc)''')
rep('  flySet(F.fly=="1",gp(pc,"CheatManager"),pc)', '  flySet(F.fly=="1",getCM(pc),pc)')

# 补给仓：多路尝试
rep('''  resupply={ onFunc=function(pawn,pc)      -- 呼叫补给仓：在玩家旁空投一个补给仓（顺带补满弹药）
      local ok=pcall(cmd,"InstantDropResuppyPod",pc)
      W("  [补给仓] InstantDropResuppyPod ok="..tostring(ok))
      pcall(cmd,"C_Resupply",pc)
    end },''',
'''  resupply={ onFunc=function(pawn,pc)      -- 呼叫补给仓：先试 cm 上的 UFUNCTION，再试控制台
      local tries={}
      local cm=getCM(pc)
      if valid(cm) then
        local a=pcall(function() return cm:InstantDropResuppyPod() end) tries[#tries+1]="cm:InstantDropResuppyPod="..tostring(a)
      end
      local b=pcall(cmd,"InstantDropResuppyPod",pc) tries[#tries+1]="cmd:InstantDropResuppyPod="..tostring(b)
      local c2=pcall(cmd,"C_SpawnDropPodOnSelf",pc) tries[#tries+1]="cmd:C_SpawnDropPodOnSelf="..tostring(c2)
      pcall(cmd,"C_Resupply",pc)
      W("  [补给仓] "..table.concat(tries,"  "))
    end },''')

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('v45 patched')
