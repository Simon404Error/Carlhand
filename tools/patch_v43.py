# v43：新功能（设置血量/立刻补给/自杀/杀某个队友/杀全队/醉酒）+ 飞行完善
import io
p = r'D:\Deepseek-Harness\work\260918_143041\src\cheat_engine.h'
s = io.open(p, encoding='utf-8').read()

def rep(old, new, n=1):
    global s
    assert old in s, 'anchor: ' + old[:80]
    s = s.replace(old, new, n)

rep('R"FSDLUA(-- Carlhand 功能引擎 v42', 'R"FSDLUA(-- Carlhand 功能引擎 v43')
rep('''if _G.__FSDT_ENGINE_V42 then return end
_G.__FSDT_ENGINE_V42 = true''',
'''-- v43：新增 设置血量 / 立刻补给 / 自杀 / 杀某个队友 / 杀全队 / 醉酒度（走 CheatManager 命令）；
--   飞行改用游戏自带 SetFlyMode + IsFlyModeActive 校验；奔跑/镐子补状态日志。
if _G.__FSDT_ENGINE_V43 then return end
_G.__FSDT_ENGINE_V43 = true''')
rep('W("\\n>> 功能引擎 v42 已载入 "', 'W("\\n>> 功能引擎 v43 已载入 "')

# 1) ACT：去掉写死的 set_health（改成按输入值发送），加三个动作
rep('  set_health="Cheat_SetHealth 9999", mule_speed="C_Escort_SetMuleSpeed 1000",',
    '  mule_speed="C_Escort_SetMuleSpeed 1000",\n  resupply="C_Resupply", suicide="C_KillPlayer", kill_team="C_KillTeam",')

# 2) 主循环：ACT 之后插入"按值发送"的参数型命令
anchor = '''    if F.auto_revive=="1" then'''
add = '''    -- ===== 参数型命令（值变化才发一次；0 / 负 = 不发送）=====
    do
      local sh=tonumber(F.set_health) or 0
      if sh>0 then
        if last.set_health~=tostring(sh) then last.set_health=tostring(sh) pcall(cmd,"Cheat_SetHealth "..math.floor(sh),pc) end
      else last.set_health=nil end
      local ks=tonumber(F.kill_slot) or 0
      if ks>0 then
        if last.kill_slot~=tostring(ks) then
          last.kill_slot=tostring(ks)
          pcall(cmd,"C_KillPlayer "..math.floor(ks),pc)     -- 游戏若不支持编号参数，会落到本地玩家身上
        end
      else last.kill_slot=nil end
      local ip=tonumber(F.intox_player) or 0
      local iv=tonumber(F.intox_value) or 0
      if iv>0 or ip>0 then
        local tag=tostring(ip).."|"..tostring(iv)
        if last.intox~=tag then
          last.intox=tag
          if ip>0 then pcall(cmd,"C_Intoxication_Set "..math.floor(ip).." "..math.floor(iv),pc)
          else pcall(cmd,"C_Intoxication_SetAll "..math.floor(iv),pc) end
        end
      else last.intox=nil end
    end
    if F.auto_revive=="1" then'''
rep(anchor, add)

# 3) 飞行完善
rep('''local function applyFly(F,pawn,pc)
  local want = (F.fly=="1")
  if want and flyState~=1 then
    flyState=1
    pcall(cmd,"C_SetFlyMode",pc)
    W("  [飞行] 已发送 C_SetFlyMode（开）")
  elseif (not want) and flyState==1 then
    flyState=0
    pcall(cmd,"C_SetFlyMode",pc)
    W("  [飞行] 已发送 C_SetFlyMode（关）")
  end
end''',
'''local function flySet(want,cm,pc)
  if (want and flyState==1) or ((not want) and flyState==-1) then return end
  local okq,cur=pcall(function() return cm:IsFlyModeActive() end)
  if okq and cur==want then flyState=want and 1 or -1 return end      -- 状态已经是目标值，不动
  flyState = want and 1 or -1
  local how="cm:SetFlyMode()"
  local ok1=pcall(function() return cm:SetFlyMode() end)
  if not ok1 then pcall(function() return cm:SetFlyMode(want) end) end
  if not ok1 then pcall(cmd,"C_SetFlyMode",pc) how="控制台 C_SetFlyMode" end
  local st="?"
  local ok2,q=pcall(function() return cm:IsFlyModeActive() end)
  if ok2 then st=tostring(q) end
  W("  [飞行] "..(want and "已开启" or "已关闭").."（"..how.."）IsFlyModeActive="..st)
end
local function applyFly(F,pawn,pc)
  if flyState==0 and F.fly~="1" then return end      -- 从没开过且当前关闭：什么都不做
  flySet(F.fly=="1",cm,pc)
end''')

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('v43 patched')
