# v44：修 奔跑/镐子/飞行 不生效（根本原因：applySpeed/applyAim/applyFly 里用的 cm 是 nil，
#       换成从 pc 取 CheatManager）+ 立刻补给 → 呼叫补给仓（InstantDropResuppyPod）
import io
p = r'D:\Deepseek-Harness\work\260918_143041\src\cheat_engine.h'
s = io.open(p, encoding='utf-8').read()

def rep(old, new, n=1):
    global s
    assert old in s, 'anchor: ' + old[:90]
    s = s.replace(old, new, n)

rep('R"FSDLUA(-- Carlhand 功能引擎 v43', 'R"FSDLUA(-- Carlhand 功能引擎 v44')
rep('''if _G.__FSDT_ENGINE_V43 then return end
_G.__FSDT_ENGINE_V43 = true''',
'''-- v44：① 修 奔跑/镐子/飞行"点了没反应"——根因是 applySpeed/applyAim/applyFly 里引用的 cm 其实是 nil
--   （那三个函数在 chunk 层，看不到主循环里的 local cm），于是 fastSet 直接 return、镐子/飞行只剩控制台兜底。
--   现在统一用 gp(pc,"CheatManager") 现取。② 立刻补给 → 呼叫补给仓：改发 InstantDropResuppyPod（在玩家旁空投补给仓），
--   顺带仍然 C_Resupply 补满弹药。
if _G.__FSDT_ENGINE_V44 then return end
_G.__FSDT_ENGINE_V44 = true''')
rep('W("\\n>> 功能引擎 v43 已载入 "', 'W("\\n>> 功能引擎 v44 已载入 "')

# ---- 1) fastSet：cm 可能为 nil 时也走控制台；先试带 bool 的形式 ----
rep('''local function fastSet(want,cm,pc)
  if not valid(cm) then return end
  local okq,cur=pcall(function() return cm:IsFastMovementActive() end)
  if okq and cur==want then fastState=(want and 1 or 0) return end
  if (want and fastState==1) or ((not want) and fastState==0) then return end
  fastState=want and 1 or 0
  local ok1=pcall(function() return cm:SetFastMovement() end)
  if not ok1 then pcall(function() return cm:SetFastMovement(want) end) end
  if not ok1 then pcall(cmd,"SetFastMovement",pc) end
  local st="?"
  local ok2,q=pcall(function() return cm:IsFastMovementActive() end)
  if ok2 then st=tostring(q) end
  W("  [奔跑] "..(want and "已开启" or "已关闭").."游戏自带快速移动，读回 IsFastMovementActive="..st)''',
'''local function fastSet(want,cm,pc)
  local okc=valid(cm)
  if okc then
    local okq,cur=pcall(function() return cm:IsFastMovementActive() end)
    if okq and cur==want then fastState=(want and 1 or 0) return end
  end
  if (want and fastState==1) or ((not want) and fastState==0) then return end
  fastState=want and 1 or 0
  local how="cm:SetFastMovement"
  local ok1=false
  if okc then
    ok1=pcall(function() return cm:SetFastMovement(want) end)      -- 需要 bool 参数的版本
    if not ok1 then ok1=pcall(function() return cm:SetFastMovement() end) end
  end
  if not ok1 then pcall(cmd,"SetFastMovement",pc) how="控制台" end
  local st="?"
  if okc then local ok2,q=pcall(function() return cm:IsFastMovementActive() end) if ok2 then st=tostring(q) end end
  W("  [奔跑] "..(want and "已开启" or "已关闭").."游戏自带快速移动（"..how.."），读回 IsFastMovementActive="..st)''')

# ---- 2) applySpeed / applyAim / applyFly 里现取 CheatManager ----
rep('''local function applySpeed(F,pawn,pc)
  local walkMult=tonumber(F.walk_speed) or 1''',
'''local function applySpeed(F,pawn,pc)
  local cm=gp(pc,"CheatManager")          -- 本函数在 chunk 层，看不到主循环的 local cm，必须现取
  local walkMult=tonumber(F.walk_speed) or 1''')
rep('''local function applyAim(F,pawn,pc)
  -- 镐子攻速：游戏自带 SetQuadPickAxe（属性探测从来没命中过）''',
'''local function applyAim(F,pawn,pc)
  local cm=gp(pc,"CheatManager")
  -- 镐子攻速：游戏自带 SetQuadPickAxe（属性探测从来没命中过）''')
rep('  flySet(F.fly=="1",cm,pc)', '  flySet(F.fly=="1",gp(pc,"CheatManager"),pc)')

# ---- 3) 镐子：日志里带上状态探测，便于下一轮判断 ----
rep('''    if not ok1 then pcall(cmd,"SetQuadPickAxe",pc) end
    W("  [镐子] 已开启 SetQuadPickAxe（四倍镐）")''',
'''    if not ok1 then pcall(cmd,"SetQuadPickAxe",pc) end
    local pr={}
    for _,pn in ipairs({"bQuadPickAxe","QuadPickAxe","bQuadPickAxeActive","bCheatQuadPickAxe","PickAxeMultiplier"}) do
      local v=gp(cm,pn) if v~=nil then pr[#pr+1]=pn.."="..tostring(v) end
    end
    W("  [镐子] 已开启 SetQuadPickAxe（四倍镐）"..(#pr>0 and ("；状态: "..table.concat(pr,", ")) or "；没找到状态字段"))''')

# ---- 4) 立刻补给 → 呼叫补给仓 ----
rep('  resupply="C_Resupply", suicide="C_KillPlayer", kill_team="C_KillTeam",',
    '''  suicide="C_KillPlayer", kill_team="C_KillTeam",
  resupply={ onFunc=function(pawn,pc)      -- 呼叫补给仓：在玩家旁空投一个补给仓（顺带补满弹药）
      local ok=pcall(cmd,"InstantDropResuppyPod",pc)
      W("  [补给仓] InstantDropResuppyPod ok="..tostring(ok))
      pcall(cmd,"C_Resupply",pc)
    end },''')

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('v44 patched')
