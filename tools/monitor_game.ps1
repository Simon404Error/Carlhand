# Monitor the user's DRG session while he plays with his own DX11 trainer.
# ASCII only (PowerShell 5.1 reads .ps1 as ANSI without a UTF-8 BOM).
$g   = 'E:\SteamLibrary\steamapps\common\Deep Rock Galactic\FSD\Binaries\Win64'
$logs= 'E:\SteamLibrary\steamapps\common\Deep Rock Galactic\FSD\Saved\Logs'
$crashDir = 'E:\SteamLibrary\steamapps\common\Deep Rock Galactic\FSD\Saved\Crashes'
$dump= "$g\FSDTrainer_dump.txt"
$ue4 = "$g\UE4SS.log"
$flog= "$logs\FSD.log"
$ourDll = 'D:\Deepseek-Harness\work\260918_143041\out\FSDTrainer.dll'
$out = 'D:\Deepseek-Harness\work\260918_143041\out\monitor.log'
$pidFile = 'D:\Deepseek-Harness\work\260918_143041\out\monitor.pid'
$seenCrash = @{}
$pos = @{}
foreach ($k in @($dump, $ue4, $flog)) { $pos[$k] = -1 }
$lastMem = 0
$lastChange = Get-Date
"$PID" | Set-Content $pidFile -Encoding ASCII
function W($m) { Add-Content -Path $out -Value ("[{0}] {1}" -f (Get-Date -Format 'HH:mm:ss'), $m) -Encoding UTF8 }
W "=== monitor start (pid $PID) ==="
function TailNew($file, $tag, $filter) {
    if (-not (Test-Path $file)) { return }
    $all = @(Get-Content $file -Encoding UTF8 -ErrorAction SilentlyContinue)
    if ($pos[$file] -lt 0) { $pos[$file] = $all.Count; W ("$tag : attach at line " + $all.Count); return }
    if ($all.Count -le $pos[$file]) { return }
    $new = $all[$pos[$file]..($all.Count - 1)]
    $pos[$file] = $all.Count
    if ($filter -eq '') { $new | ForEach-Object { W ("$tag : " + $_) } }
    else { $new | Where-Object { $_ -match $filter } | ForEach-Object { W ("$tag : " + $_) } }
}
while ($true) {
    try {
        $procs = @(Get-Process FSD-Win64-Shipping -ErrorAction SilentlyContinue)
        if ($procs.Count -eq 0) {
            W "game not running"
            $lastMem = 0
        } else {
            foreach ($p in $procs) {
                $mb = [int]($p.WorkingSet64 / 1MB)
                $th = 0
                try { $th = $p.Threads.Count } catch {}
                W ("game PID={0} mem={1}MB threads={2} title='{3}'" -f $p.Id, $mb, $th, $p.MainWindowTitle)
            }
            $big = @($procs | Where-Object { $_.WorkingSet64 -gt 100MB })
            if ($big.Count -gt 0) {
                $mb = [int](($big | Measure-Object WorkingSet64 -Maximum).Maximum / 1MB)
                if ([Math]::Abs($mb - $lastMem) -gt 20) { $lastMem = $mb; $lastChange = Get-Date }
                elseif (((Get-Date) - $lastChange).TotalSeconds -gt 180) { W ("NOTE: mem unchanged for >180s (current {0}MB)" -f $mb); $lastChange = Get-Date }
            }
        }
        Get-ChildItem $g -Filter 'crash_*.dmp' -ErrorAction SilentlyContinue | ForEach-Object {
            if (-not $seenCrash[$_.Name]) { $seenCrash[$_.Name] = $true; W ("NEW CRASH: {0} ({1}MB)" -f $_.Name, [int]($_.Length/1MB)) }
        }
        # DRG's own crash report folder (UE4CC-*) contains ErrorMessage - more useful than crash_*.dmp
        Get-ChildItem $crashDir -Directory -ErrorAction SilentlyContinue | ForEach-Object {
            if (-not $seenCrash[$_.Name]) {
                $seenCrash[$_.Name] = $true
                $xml = Join-Path $_.FullName 'CrashContext.runtime-xml'
                $msg = '?'
                if (Test-Path $xml) {
                    $t = Get-Content $xml -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
                    if ($t -match '<ErrorMessage>(.*?)</ErrorMessage>') { $msg = $Matches[1] }
                    if ($t -match '<CrashType>(.*?)</CrashType>') { $msg = $Matches[1] + ' / ' + $msg }
                }
                W ("NEW UE CRASH: {0} :: {1}" -f $_.Name, ($msg -replace "`r?`n", ' '))
            }
        }
        try { $fs = [IO.File]::OpenWrite($ourDll); $fs.Close() } catch { W "our FSDTrainer.dll is LOCKED (injected into the game)" }
        TailNew $flog 'FSD.log' 'Error|Warning|RHI|D3D11|D3D12|Fatal|Crash'
        TailNew $ue4 'UE4SS' 'error|Error|Lua|Exception'
        TailNew $dump 'CARLHAND' 'ESP|Exception|Engine v|CRASH'
    } catch { W ("monitor error: " + $_.Exception.Message) }
    Start-Sleep -Seconds 10
}
