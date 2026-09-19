# v41 补丁：奔跑用游戏自带 SetFastMovement / 镐子用 SetQuadPickAxe / 夜视强化 /
#            ESP 显示怪物名与矿名 + 只显示未挖掘矿脉（探测类名）/ 排除掉落块 / 排除 Critters
import io, sys

root = r'D:\Deepseek-Harness\work\260918_143041'
p = root + r'\src\cheat_engine.h'
s = io.open(p, encoding='utf-8').read()

def rep(old, new, cnt=1):
    global s
    assert s.count(old) >= 1, '找不到锚点: ' + old[:80]
    s = s.replace(old, new, cnt)

# ---------- 0) 版本号 ----------
rep('R"FSDLUA(-- Carlhand 功能引擎 v40', 'R"FSDLUA(-- Carlhand 功能引擎 v41')
rep('''-- v40：ESP 改成独立 150ms 快循环 + 每 3 秒重扫目标 + 头顶/脚底两点定框（修"框不跟怪走"）。
if _G.__FSDT_ENGINE_V40 then return end
_G.__FSDT_ENGINE_V40 = true''',
'''-- v41：① 奔跑改用游戏自带 SetFastMovement（MaxWalkSpeed 会被游戏每帧覆盖，所以以前"没效果"）；
--   ② 镐子攻速改用 SetQuadPickAxe；③ 夜视关掉平方反比衰减（Intensity 才有视觉效果）+ 大半径/宽锥角；
--   ④ ESP 显示怪物中文名与矿名；只显示"未挖掘矿脉"（排除掉在地上的 ResourceChunk 掉落块），
--      并在进任务后做一次类名探测把矿脉真名写进日志；⑤ 不再把 /Game/Critters 里的小动物当敌人。
if _G.__FSDT_ENGINE_V41 then return end
_G.__FSDT_ENGINE_V41 = true''')
rep('W("\\n>> 功能引擎 v40 已载入 "', 'W("\\n>> 功能引擎 v41 已载入 "')

# ---------- 1) 奔跑：SetFastMovement ----------
rep('local function applySpeed(F,pawn)', 'local function applySpeed(F,pawn,pc)')
rep('''    target=sprintBase*sprintMult tag="奔跑"
    pcall(sprintProbe,pawn,c)''',
'''    target=sprintBase*sprintMult tag="奔跑"
    -- 关键：真正有效的是游戏自带的"快速移动"，不是我们写 MaxWalkSpeed（游戏每帧会覆盖回去）
    if fastState~=1 then
      fastState=1
      local how="命令"
      local ok2=pcall(function() return cm:SetFastMovement() end)
      if not ok2 then pcall(function() return cm:SetFastMovement(true) end) end
      if not ok2 then pcall(cmd,"SetFastMovement",pc) how="控制台" end
      local st="?"
      local okq,q=pcall(function() return cm:IsFastMovementActive() end)
      if okq then st=tostring(q) end
      W("  [奔跑] 已开启游戏自带快速移动("..how..")，IsFastMovementActive="..st)
    end
    pcall(sprintProbe,pawn,c)''')
rep('''  local c=mov(pawn) if not valid(c) then return end)FSDLUA",
R"FSDLUA(  local cur=num(gp(c,"MaxWalkSpeed"))''',
'''  if sprintMult<=1 and fastState==1 then
    fastState=0
    local ok2=pcall(function() return cm:SetFastMovement() end)
    if not ok2 then pcall(cmd,"SetFastMovement",pc) end
    W("  [奔跑] 已关闭游戏自带快速移动")
  end
  local c=mov(pawn) if not valid(c) then return end)FSDLUA",
R"FSDLUA(  local cur=num(gp(c,"MaxWalkSpeed"))''')

# 状态变量 + 镐子状态
rep('local walkBase, sprintBase, lastLogged, basesCaptured, sprintDbg = nil, nil, nil, false, 0',
    'local walkBase, sprintBase, lastLogged, basesCaptured, sprintDbg = nil, nil, nil, false, 0\nlocal fastState, quadState = 0, 0   -- 游戏自带"快速移动"/"四倍镐"的开关状态')

# ---------- 2) 镐子攻速：SetQuadPickAxe ----------
rep('''local function applyAim(F,pawn,pc)
  local c=mov(pawn)
  if not valid(c) then return end''',
'''local function applyAim(F,pawn,pc)
  -- 镐子攻速：游戏自带 SetQuadPickAxe（属性探测从来没命中过）
  local wantQ=(tonumber(F.pickaxe_speed) or 0)>1
  if wantQ and quadState~=1 then
    quadState=1
    local ok1=pcall(function() return cm:SetQuadPickAxe(true) end)
    if not ok1 then pcall(function() return cm:SetQuadPickAxe() end) end
    if not ok1 then pcall(cmd,"SetQuadPickAxe",pc) end
    W("  [镐子] 已开启 SetQuadPickAxe（四倍镐）")
  elseif (not wantQ) and quadState==1 then
    quadState=0
    local ok1=pcall(function() return cm:SetQuadPickAxe(false) end)
    if not ok1 then pcall(function() return cm:SetQuadPickAxe() end) end
    W("  [镐子] 已关闭 SetQuadPickAxe")
  end
  local c=mov(pawn)
  if not valid(c) then return end''')

# ---------- 3) 夜视强化 ----------
rep('''      for _,p in ipairs({"Intensity","AttenuationRadius","OuterConeAngle","InnerConeAngle","CastShadows","bCastShadows"}) do
        if lightOrig[p]==nil then lightOrig[p]=gp(o,p) end
      end
      sp(o,"CastShadows",false)          -- 关阴影，避免大范围光照导致崩溃
      sp(o,"bCastShadows",false)
      sp(o,"Intensity", 30000)           -- 提高一档（之前 200000 会崩，6000 看不出效果）
      sp(o,"AttenuationRadius", 15000)
      lightLogged=true
      W("  [夜视] 已应用 Intensity="..tostring(gp(o,"Intensity")).." 半径="..tostring(gp(o,"AttenuationRadius"))
        .." 阴影="..tostring(gp(o,"CastShadows")))''',
'''      for _,p in ipairs({"Intensity","AttenuationRadius","OuterConeAngle","InnerConeAngle","CastShadows",
                         "bCastShadows","bUseInverseSquaredFalloff","IntensityUnits","LightColor","Visible","bVisible"}) do
        if lightOrig[p]==nil then lightOrig[p]=gp(o,p) end
      end
      sp(o,"CastShadows",false)                    -- 关阴影（大范围光照+阴影容易崩）
      sp(o,"bCastShadows",false)
      -- 关键：UE4 灯光默认"平方反比衰减"，Intensity 的单位是坎德拉，衰减极快 → 改大也看不太出来。
      -- 关掉它 + 单位改成 Unitless，Intensity 才会线性生效。
      sp(o,"bUseInverseSquaredFalloff",false)
      sp(o,"IntensityUnits",0)
      sp(o,"Intensity", 100000)
      sp(o,"AttenuationRadius", 40000)
      sp(o,"OuterConeAngle", 88)
      sp(o,"InnerConeAngle", 45)
      sp(o,"Visible", true)
      sp(o,"bVisible", true)
      lightLogged=true
      W("  [夜视] 已应用 Intensity="..tostring(gp(o,"Intensity")).." 半径="..tostring(gp(o,"AttenuationRadius"))
        .." 平方衰减="..tostring(gp(o,"bUseInverseSquaredFalloff")).." 单位="..tostring(gp(o,"IntensityUnits"))
        .." 可见="..tostring(gp(o,"Visible")).." 组件="..nm(o))''')

# ---------- 4) ESP：名字表 + 矿脉探测 + 新格式 ----------
rep('''local ESP_FILE = "FSDTrainer_esp.txt"
local espTargets, espEnumAt = {}, 0
local espWorldAt, espSelfRound = nil, 0''',
'''local ESP_FILE = "FSDTrainer_esp.txt"
local espTargets, espEnumAt = {}, 0
local espWorldAt, espSelfRound = nil, 0
local oreList, oreProbeDone = {}, false     -- 未挖掘矿脉（进任务后探测一次收集）
-- ---- 类名 → 中文名（ESP 上要显示"是什么怪/什么矿"）----
local ENEMY_NAME = {
  {"Grunt_Attacker","战士虫·攻击者"},{"Grunt_Guard","战士虫·护卫"},{"Grunt","战士虫"},
  {"Swarm","蜂群虫"},{"Swarmer","蜂群虫"},{"Tank","禁卫虫"},{"Praetorian","禁卫虫"},
  {"Explod","自爆虫"},{"Bomber","轰炸虫"},{"Spitball","吐球虫"},{"Leech","洞穴水蛭"},
  {"Mactera_TripleShooter","三连喷吐虫"},{"Mactera_Shooter","喷吐虫"},{"Mactera_Spawn","孕母虫"},
  {"Mactera_Grabber","抓取虫"},{"Mactera_Brundle","飞虫"},{"Mactera","飞行虫"},
  {"Naedocyte","水母"},{"Breeder","巢母虫"},{"Hooli","呼利虫"},{"Qronar","幼兽"},
  {"Menace","威胁"},{"Barrage","弹幕虫"},{"Dreadnought","恐惧战舰"},{"Dreadnaught","恐惧战舰"},
  {"Bulk","暴君虫"},{"Fester","腐败虫"},{"Flea","跳蚤"},{"LootBug","战利虫"},
  {"Hexawing","六翼虫"},{"Stingtail","刺尾虫"},{"Trawler","拖网虫"},{"Nexus","巢穴虫"},
  {"Korlok","科洛克"},{"Sentinel","哨兵"},{"Rockpox","岩痘"},{"Glyphid","异虫"},
  {"Cave","洞穴虫"},{"Spider","异虫"},
}
local ORE_NAME = {
  {"Morkite","墨岩"},{"Nitra","硝石"},{"Bismor","铋矿"},{"Croppa","克洛帕"},{"Jadiz","玉石"},
  {"Magnite","磁铁矿"},{"Umanite","铀铁"},{"Enor","珍珠"},{"Gold","黄金"},{"Dystrum","碱液"},
  {"Bittergem","苦胆石"},{"Aquarq","水银晶"},{"Hollomite","霍洛石"},{"Plagueheart","疫心"},
  {"Crystal","水晶"},{"Gem","宝石"},{"Ore","矿石"},{"Mineral","矿物"},{"Resource","资源"},{"Vein","矿脉"},
}
local function mapName(tbl,s)
  for _,kv in ipairs(tbl) do
    if string.find(s,kv[1],1,true) then return kv[2] end
  end
  return nil
end
local function enemyName(c)
  local n=mapName(ENEMY_NAME,c)
  if n then
    if string.find(c,"Radioactive",1,true) then return n.."·辐射" end
    if string.find(c,"Kursite",1,true) or string.find(c,"Rockpox",1,true) then return n.."·岩痘" end
    return n
  end
  return (string.match(c,"([%w_]+)_C$") or "敌人")
end
local function oreName(c)
  local n=mapName(ORE_NAME,c)
  return n or (string.match(c,"([%w_]+)_C$") or "矿石")
end
-- 只有"长在墙上的矿脉"算矿石；掉在地上可捡的 ResourceChunk/Bit 是用户明确不要的
local function isOreClass(c)
  if string.find(c,"ResourceChunk",1,true) or string.find(c,"_Chunk",1,true) then return false end
  if string.find(c,"Bit",1,true) and string.find(c,"Resource",1,true) then return false end
  local k={"Vein","Deposit","Ore","Mineral","Crystal","Gem","Collectable","Aquarq","Morkite","Nitra",
           "Bismor","Croppa","Jadiz","Magnite","Umanite","Enor","Dystrum","Hollomite","Bittergem"}
  for _,w in ipairs(k) do if string.find(c,w,1,true) then return true end end
  return false
end
-- 进任务后做一次全 Actor 类名探测：把矿脉真名写进日志（DRG 的类名事先不知道，只能实测）
local function oreProbe()
  if oreProbeDone then return end
  oreProbeDone=true
  local ok,l=pcall(FindAllOf,"Actor")
  if not ok or type(l)~="table" then W("  [矿石探测] FindAllOf(Actor) 失败") return end
  local cnt,sample={},{}
  local k={"Resource","Vein","Deposit","Ore","Gem","Crystal","Mineral","Collectab","Chunk",
           "Aquarq","Morkite","Nitra","Gold","Bismor","Croppa","Jadiz","Magnite","Umanite","Enor"}
  for _,a in ipairs(l) do
    if valid(a) then
      local okc,c=pcall(function() return a:GetClass():GetFullName() end)
      if okc and type(c)=="string" then
        for _,w in ipairs(k) do
          if string.find(c,w,1,true) then cnt[c]=(cnt[c] or 0)+1 break end
        end
      end
    end
  end
  local n=0
  for c,v in pairs(cnt) do
    n=n+1
    if n<=45 then W("  [矿石探测] "..v.." × "..c) end
    if isOreClass(c) and #sample<1 then sample[#sample+1]=c end
  end
  W("  [矿石探测] 共 "..n.." 种；Actor 总数 "..#l.."；选了 "..#sample.." 类做矿脉")
  -- 收集矿脉 Actor（位置每轮再读）
  oreList={}
  for _,a in ipairs(l) do
    if #oreList<200 and valid(a) then
      local okc,c=pcall(function() return a:GetClass():GetFullName() end)
      if okc and type(c)=="string" and isOreClass(c) then
        oreList[#oreList+1]={a=a,name=oreName(c)}
      end
    end
  end
  W("  [矿石探测] 已收集未挖掘矿脉 "..#oreList.." 个")
end''')

# espRescan 重写（敌人只取 /Game/Enemies 且带名字；矿物用 oreList）
rep('''local function espRescan(dE,dM)
  local list={}
  if dE>0 then
    local ok,l=pcall(FindAllOf,"Pawn")
    if ok and type(l)=="table" then
      for _,p in ipairs(l) do
        if valid(p) then
          local okc,c=pcall(function() return p:GetClass():GetFullName() end)
          if okc and type(c)=="string" and string.find(c,"/Game/Enemies/",1,true) then
            if not espDbg then espDbg=true W("  [ESP] 敌人类名示例: "..c) end
            list[#list+1]={a=p,tag="E",lim=dE,hz=90}
          end
        end
      end
    end
  end
  if dM>0 then
    for _,cls in ipairs(ESP_MINE_C) do
      local ok,l=pcall(FindAllOf,cls)
      if ok and type(l)=="table" and #l>0 then
        if not espHitLogged then espHitLogged=true W("  [ESP] 矿物类名命中: "..cls.." 数量="..#l) end
        for _,o in ipairs(l) do if valid(o) then list[#list+1]={a=o,tag="M",lim=dM,hz=35} end end
        break
      end
    end
  end
  return list
end''',
'''local function espRescan(dE,dM)
  local list={}
  if dE>0 then
    local ok,l=pcall(FindAllOf,"Pawn")
    if ok and type(l)=="table" then
      for _,p in ipairs(l) do
        if valid(p) then
          local okc,c=pcall(function() return p:GetClass():GetFullName() end)
          -- 只要 /Game/Enemies/ 下的怪；/Game/Critters/（战利虫、跳蚤）不算敌人
          if okc and type(c)=="string" and string.find(c,"/Game/Enemies/",1,true) then
            if not espDbg then espDbg=true W("  [ESP] 敌人类名示例: "..c.." → "..enemyName(c)) end
            list[#list+1]={a=p,tag="E",lim=dE,hz=90,name=enemyName(c)}
          end
        end
      end
    end
  end
  if dM>0 then
    for _,o in ipairs(oreList) do
      if valid(o.a) then list[#list+1]={a=o.a,tag="M",lim=dM,hz=35,name=o.name} end
    end
  end
  return list
end''')

# 探测调用
rep('''    if now-(espEnumAt or 0)>=3 then
      espEnumAt=now''',
'''    if dM>0 and not oreProbeDone then oreProbe() end
    if now-(espEnumAt or 0)>=3 then
      espEnumAt=now''')

# 写出格式：带名字
rep('''              out[#out+1]=string.format("%s %.0f %.0f %.0f %.0f %.0f\\n",t.tag,x1,y1,x2,y2,d)''',
    '''              out[#out+1]=string.format("%s %.0f %.0f %.0f %.0f %.0f %s\\n",t.tag,x1,y1,x2,y2,d,t.name or "?")''')

# 换图时清掉矿脉缓存
rep('''      espTargets={} espEnumAt=0 espWorldAt=now      -- ESP 目标列表作废，重新计时''',
    '''      espTargets={} espEnumAt=0 espWorldAt=now      -- ESP 目标列表作废，重新计时
      oreList={} oreProbeDone=false                 -- 矿脉要重新探测
      fastState=0 quadState=0                       -- 重生后游戏自带的开关状态未知，重新确认一次''')

# 主循环调用点：applySpeed 需要 pc
rep('pcall(applySpeed,F,pawn)', 'pcall(applySpeed,F,pawn,pc)')

io.open(p, 'w', encoding='utf-8', newline='').write(s)
print('v41 patched OK')
