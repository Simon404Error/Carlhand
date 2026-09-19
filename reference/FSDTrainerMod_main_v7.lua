-- ===== 执行器 v7 =====
-- 相对 v6 的修改（修"引擎莫名其妙不再运行"）：
--   1) 命令文件里没有可执行行时，完全不去碰游戏对象（v6 每 300ms 都 FindAllOf，早期初始化阶段可能打死 UE4SS 的 Lua 线程）
--   2) 只有 lock/cheat 这类真的需要对象的命令才去取 PlayerController/Pawn
--   3) 每 30 秒写一次心跳，日志里能直接看出执行器是否还活着
--   4) 所有异常都被 pcall 兜住并记录，循环永不退出
local DIR = "E:/SteamLibrary/steamapps/common/Deep Rock Galactic/FSD/Binaries/Win64/"
local CMD, LOG = DIR.."FSDTrainer_cmd.txt", DIR.."FSDTrainer_dump.txt"
local function W(s) local ok,f=pcall(io.open,LOG,"a") if ok and f then f:write(tostring(s).."\n") f:flush() f:close() end end
local function readf(p) local ok,f=pcall(io.open,p,"r") if not ok or not f then return "" end local s=f:read("*a") f:close() return s or "" end
local function nm(o) if not o then return end local ok,v=pcall(function() return o:GetFullName() end) if ok then return v end end
local function gp(o,n) if not o then return end local ok,v=pcall(function() return o:GetPropertyValue(n) end) if ok then return v end end
local function sp(o,n,v) if not o then return end pcall(function() o:SetPropertyValue(n,v) end) end
local function first(c) local ok,a=pcall(FindAllOf,c) if ok and type(a)=="table" and #a>0 then return a[1] end end
local function localPawn()
  local pc=first("PlayerController"); if pc then local p=gp(pc,"Pawn"); if p and nm(p) then return p,pc end end
  local a=first("PlayerCharacter"); if a then local n=nm(a) if n and string.find(n,"Navigator") then return a,first("PlayerController") end return a,first("PlayerController") end
end
local CLS={"HealthComponent","ShieldComponent","CharacterMovementComponent","PlayerMovementComponent","InventoryComponent"}
local function compOf(short,cls) local ok,l=pcall(FindAllOf,cls) if not ok or type(l)~="table" then return end
  for _,o in ipairs(l) do local n=nm(o) if n and string.find(n,short,1,true) then return o end end end
local function shortOf(p) local n=nm(p) return n and (string.match(n,"PersistentLevel%.([^%.]+)$") or n) end
local function resolve(p,short,prop)
  local v=gp(p,prop); if v~=nil and type(v)=="number" then return p end
  for _,c in ipairs(CLS) do local o=compOf(short,c) if o then local vv=gp(o,prop) if vv~=nil and type(vv)=="number" then return o end end end
end
local function doCheat(p,pc,cmd)
  W("  [cheat] "..cmd)
  local cm = pc and gp(pc,"CheatManager")
  if cm and nm(cm) then local ok,r=pcall(function() return cm[cmd](cm) end) W("    cm."..cmd.."() -> ok="..tostring(ok).." r="..tostring(r)) end
  local k=StaticFindObject("/Script/Engine.Default__KismetSystemLibrary")
  if k then local ok,r=pcall(function() return k:ExecuteConsoleCommand(p,cmd,nil) end) W("    Kismet -> ok="..tostring(ok).." r="..tostring(r)) end
  if pc then local ok,r=pcall(function() return pc:ConsoleCommand(cmd) end) W("    ConsoleCommand -> ok="..tostring(ok).." r="..tostring(r)) end
end

local locks,lastCmd,lastPawn,lastBeat = {}, "", "", os.time()
W("\n===== 执行器 v7 "..os.date("%H:%M:%S").." =====")
LoopAsync(300, function()
  local ok,err = pcall(function()
    if os.time()-lastBeat >= 30 then lastBeat=os.time() W("  <心跳> 执行器存活 "..os.date("%H:%M:%S")) end
    local txt = readf(CMD)
    if txt ~= lastCmd then
      lastCmd = txt
      local lines, needObj = {}, false
      for line in txt:gmatch("[^\r\n]+") do
        local a = line:match("^%s*(%a+)")
        if a then
          lines[#lines+1] = line
          if a=="lock" or a=="cheat" then needObj = true end
        end
      end
      W("[命令] "..txt:gsub("\r?\n"," | "))
      local p, pc
      if needObj then p, pc = localPawn() end
      for _,line in ipairs(lines) do
        local cmd, rest = line:match("^%s*(%a+)%s*(.*)$")
        if cmd=="clear" then locks={} W("  清空")
        elseif cmd=="off" then local pr=rest:match("^(%S+)"); locks[pr]=nil W("  解除 "..tostring(pr))
        elseif cmd=="cheat" then local c=rest:match("^([%w_]+)"); if c and p then doCheat(p,pc,c) end
        elseif cmd=="lua" then
          local f=rest:match("^(%S+)"); local src=readf(f)
          if src=="" then W("  lua 文件为空/不存在: "..tostring(f)) else
            local fn,e1=load(src,"hotload")
            if fn then local ok2,e2=pcall(fn) W("  lua "..f.." -> ok="..tostring(ok2).." "..tostring(e2)) else W("  lua 编译失败: "..tostring(e1)) end
          end
        elseif cmd=="lock" then
          local prop,mode,val = rest:match("^(%S+)%s+(%a+)%s*(%S*)$")
          if prop and p then
            local short = shortOf(p)
            local o = resolve(p,short,prop)
            if not o then W("  锁定失败："..prop)
            elseif not locks[prop] or locks[prop].orig==nil then
              locks[prop]={obj=o,mode=mode or "mult",val=tonumber(val) or 1.5,orig=gp(o,prop)}
              W("  锁定 "..prop.." "..tostring(mode).."="..tostring(val).." 原始="..tostring(locks[prop].orig))
            end
          end
        end
      end
    end
    if next(locks) then
      local p = localPawn()
      if p then
        local short = shortOf(p)
        for prop,L in pairs(locks) do
          if L.obj and L.orig~=nil then
            local t = L.orig
            if L.mode=="mult" then t=L.orig*L.val elseif L.mode=="set" then t=L.val end
            sp(L.obj,prop,t)
          elseif not L.obj then
            local o = resolve(p,short,prop)
            if o then L.obj=o L.orig=gp(o,prop) end
          end
        end
      end
    end
  end)
  if not ok then W("  !!! 执行器异常(已继续): "..tostring(err)) end
  return false
end)
