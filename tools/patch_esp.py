# 把 cheat_engine.h 里 513..592 行（旧 espCollect）替换成新的 ESP 独立循环实现
import io

p = r'D:\Deepseek-Harness\work\260918_143041\src\cheat_engine.h'
lines = io.open(p, encoding='utf-8').read().split('\n')

new = r'''R"FSDLUA(-- ================= ESP：独立快循环（150ms ≈ 6.7Hz）+ 每 3 秒重扫目标 =================
-- v40 修"框不跟怪走"：原来 ESP 挂在 300ms 主循环里，而且 os.time() 只有秒级精度，
-- 实际每秒才更新 1 次 → 怪一动框就掉队（1 秒能走好几米）。现在单独一条 150ms 循环，
-- 并把"枚举目标（要 GetClass():GetFullName()，贵）"和"取坐标+投影（纯数学）"拆开：
-- 目标列表每 3 秒刷一次，坐标每轮都取，画框用"头顶/脚底"两个投影点定高度。
local ESP_FILE = "FSDTrainer_esp.txt"
local espTargets, espEnumAt = {}, 0
local espWorldAt, espSelfRound = nil, 0
local function espBox(view,pawn,x,y,z,hz)
  if view==nil then return nil end
  local tx,ty=projWith(view,pawn,x,y,z+hz)
  local bx,by=projWith(view,pawn,x,y,z-hz)
  if not tx or not bx then return nil end
  local top,bot=ty,by
  if top>bot then top,bot=bot,top end
  local h=bot-top
  if h<4 then return nil end
  local cx=projWith(view,pawn,x,y,z)
  local mx=cx or tx
  local w=h*0.62
  if w<h*0.3 then w=h*0.3 end
  return mx-w/2, top, mx+w/2, bot
end
local function espRescan(dE,dM)
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
end
LoopAsync(150, function()
  if _G.__FSDT_GEN~=MYGEN then return true end
  local ok,e=pcall(function()
    local F=readF()
    if next(F)==nil then return end
    local dE=tonumber(F.esp_enemies) or 0
    local dM=tonumber(F.esp_minerals) or 0
    if dE<=0 and dM<=0 then
      if #espTargets>0 then espTargets={} wf(G..ESP_FILE,"# ESP 关闭\n") end
      return
    end
    if dE>120 then dE=120 end
    if dM>120 then dM=120 end
    local pc,cm,pawn=ctx()
    if not (valid(pc) and valid(pawn)) then return end
    local now=os.time()
    if espWorldAt==nil then espWorldAt=now return end
    if now-espWorldAt<15 then return end          -- 换图/进任务后稳定 15 秒才开工
    local view=prepareView(pc)
    if view==nil then return end
    local px,py,pz=locOf(pawn)
    if not px then return end
    if now-(espEnumAt or 0)>=3 then
      espEnumAt=now
      espTargets=espRescan(dE,dM)
      W("  [ESP] 目标重扫: "..#espTargets.." 个（敌人 "..dE.."m / 矿物 "..dM.."m）")
    end
    local out,n={},0
    for _,t in ipairs(espTargets) do
      if n>=60 then break end
      if valid(t.a) then
        local x,y,z=locOf(t.a)
        if x then
          local d=math.sqrt((x-px)^2+(y-py)^2+(z-pz)^2)/100.0
          if d<=t.lim then
            local x1,y1,x2,y2=espBox(view,pawn,x,y,z,t.hz)
            if x1 and x2>0 and y2>0 and x1<vpW and y1<vpH then
              out[#out+1]=string.format("%s %.0f %.0f %.0f %.0f %.0f\n",t.tag,x1,y1,x2,y2,d)
              n=n+1
            end
          end
        end
      end
    end
    espSelfRound=espSelfRound+1
    if espSelfRound%20==1 then
      local saw,say=projWith(view,pawn,px+view.F[1]*1000,py+view.F[2]*1000,pz+view.F[3]*1000+60)
      W("  [ESP自检] 方式="..tostring(projMode).." 视口="..vpW.."x"..vpH.." 目标="..#espTargets
        .." 写出="..n.." 正前方10米="..(saw and (math.floor(saw)..","..math.floor(say)) or "失败")
        .."（期望 ≈ "..math.floor(vpW/2)..","..math.floor(vpH/2).."）")
    end
    wf(G..ESP_FILE,"# Carlhand ESP "..os.date("%H:%M:%S").." 目标"..#espTargets.."\n"..table.concat(out))
  end)
  if not ok then W("  !!! ESP 循环异常(已继续): "..tostring(e)) end
  return false
end)'''

# 1-based 行号 513..592 → 索引 512..591
assert 'local function espCollect' in lines[512], lines[512]
assert lines[591].strip() == 'end', lines[591]
out = lines[:512] + new.split('\n') + lines[592:]
io.open(p, 'w', encoding='utf-8', newline='').write('\n'.join(out))
print('replaced, new line count:', len(out))
