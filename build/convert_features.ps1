# 把 config\FSDTrainer.ini 里指定功能改成 kind=cheat（走游戏内引擎），并追加新功能
param([string]$Ini = "$PSScriptRoot\..\config\FSDTrainer.ini")
$ErrorActionPreference = 'Stop'
$t = [IO.File]::ReadAllText($Ini, [Text.Encoding]::Unicode)

# 功能名 -> 新属性（kind=cheat）。cat 沿用原节里的取值
$map = @{
  '移速设置'      = @{ ui='value';  key='walk_speed'; def='300'; unit='速度(cm/s)'; note='走路速度，默认300' }
  '无限跳跃'      = @{ ui='value';  key='jump_max';   def='1';   unit='次';          note='空中可跳次数，99=无限跳' }
  '无敌'          = @{ ui='toggle'; key='godmode' }
  '启用喷气靴子'  = @{ ui='toggle'; key='jetboots' }
  '杀戮光环'      = @{ ui='toggle'; key='killaura'; note='开启后持续清除敌人' }
  '瞬间复活'      = @{ ui='action'; key='revive' }
  '飞行模式'      = @{ ui='toggle'; key='fly' }
  '无限照明'      = @{ ui='toggle'; key='inf_flare' }
  '添加角色经验'  = @{ ui='action'; key='xp' }
  '解锁超频栏位'  = @{ ui='action'; key='unlock_overclock' }
  '解锁员工衣柜'  = @{ ui='action'; key='unlock_store' }
  '添加5000代币'  = @{ ui='action'; key='credits' }
  '解锁镐子配件'  = @{ ui='action'; key='unlock_pickaxe' }
  '解锁所有姿势'  = @{ ui='action'; key='unlock_poses' }
  '添加5000资源'  = @{ ui='action'; key='resources' }
  '解锁武器等级'  = @{ ui='action'; key='unlock_upgrades' }
  '解锁啤酒'      = @{ ui='action'; key='unlock_beers' }
  '添加5000节点'  = @{ ui='action'; key='crafting' }
  '解锁武器皮肤'  = @{ ui='action'; key='unlock_skins' }
  '解锁超频核心'  = @{ ui='action'; key='unlock_schematics' }
  '解锁所有特长'  = @{ ui='action'; key='unlock_perks' }
  '传送队友集合'  = @{ ui='action'; key='teleport_to_me' }
  '解锁所有武器'  = @{ ui='action'; key='unlock_weapons' }
  '锻造超频核心'  = @{ ui='action'; key='forge_xp' }
}

$rx = [regex]'(?ms)^\[feature\.(?<name>[^\]\r\n]+)\]\s*\r?\n(?<body>.*?)(?=^\[|\z)'
$count = 0
$t = $rx.Replace($t, {
  param($m)
  $name = $m.Groups['name'].Value
  if (-not $map.ContainsKey($name)) { return $m.Value }
  $script:count++
  $body = $m.Groups['body'].Value
  $cat = ([regex]'(?m)^cat=(.+)$').Match($body).Groups[1].Value.Trim()
  if (-not $cat) { $cat = '特殊功能' }
  $o = $map[$name]
  $sb = New-Object Text.StringBuilder
  [void]$sb.AppendLine("[feature.$name]")
  [void]$sb.AppendLine("cat=$cat")
  [void]$sb.AppendLine("kind=cheat")
  [void]$sb.AppendLine("ui=$($o.ui)")
  [void]$sb.AppendLine("key=$($o.key)")
  if ($o.def)  { [void]$sb.AppendLine("def=$($o.def)") }
  if ($o.unit) { [void]$sb.AppendLine("unit=$($o.unit)") }
  if ($o.note) { [void]$sb.AppendLine("note=$($o.note)") }
  [void]$sb.AppendLine("addr=0")
  return $sb.ToString()
})

# 追加新功能（若同名节还不存在）
$new = @(
  @{ n='跳跃高度';         cat='常规';     ui='value';  key='jump_z';          def='500'; unit='cm/s'; note='跳跃初速，默认500' },
  @{ n='重力';             cat='常规';     ui='value';  key='gravity';         def='1';   unit='倍率'; note='重力倍率，默认1' },
  @{ n='一键解锁全部';     cat='特殊功能'; ui='action'; key='unlock_all' },
  @{ n='添加赛季经验';     cat='特殊功能'; ui='action'; key='season_xp' },
  @{ n='添加时装精通经验'; cat='特殊功能'; ui='action'; key='vanity_xp' },
  @{ n='晋升一级';         cat='特殊功能'; ui='action'; key='level_up' },
  @{ n='清除改出的资源';   cat='特殊功能'; ui='action'; key='reset_cheat_res' },
  @{ n='生成怪潮';         cat='特殊功能'; ui='action'; key='spawn_wave' },
  @{ n='击碎敌人护甲';     cat='特殊功能'; ui='action'; key='break_armor' },
  @{ n='解除设施护盾';     cat='特殊功能'; ui='action'; key='disable_shields' },
  @{ n='回满血量';         cat='特殊功能'; ui='action'; key='set_health' },
  @{ n='无限弹药消耗';     cat='武器道具'; ui='toggle'; key='inf_ammo' },
  @{ n='全亮/关雾';        cat='特殊功能'; ui='toggle'; key='fullbright' }
)
$added = 0
foreach ($f in $new) {
  if ($t -match "(?m)^\[feature\.$([regex]::Escape($f.n))\]") { continue }
  $sb = New-Object Text.StringBuilder
  [void]$sb.AppendLine("")
  [void]$sb.AppendLine("[feature.$($f.n)]")
  [void]$sb.AppendLine("cat=$($f.cat)")
  [void]$sb.AppendLine("kind=cheat")
  [void]$sb.AppendLine("ui=$($f.ui)")
  [void]$sb.AppendLine("key=$($f.key)")
  if ($f.def)  { [void]$sb.AppendLine("def=$($f.def)") }
  if ($f.unit) { [void]$sb.AppendLine("unit=$($f.unit)") }
  if ($f.note) { [void]$sb.AppendLine("note=$($f.note)") }
  [void]$sb.AppendLine("addr=0")
  $t += $sb.ToString()
  $added++
}

[IO.File]::WriteAllText($Ini, $t, [Text.Encoding]::Unicode)
"已改写 $count 个功能节，追加 $added 个新节 -> $Ini"
"kind=cheat 节总数: " + ([regex]::Matches($t, '(?m)^kind=cheat$')).Count
"功能节总数: " + ([regex]::Matches($t, '(?m)^\[feature\.')).Count
