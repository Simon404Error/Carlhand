# Generate Xenos64 injection profile (manual map into FSD-Win64-Shipping.exe)
param(
    [Parameter(Mandatory=$true)][string]$Dll,
    [Parameter(Mandatory=$true)][string]$Out,
    [string]$Proc = "FSD-Win64-Shipping.exe",
    [int]$InjectMode = 0,
    [int]$Delay = 0,
    [switch]$Hijack,
    [switch]$ErasePE,
    [switch]$KrnHandle
)

if (-not (Test-Path $Dll)) { Write-Error "DLL not found: $Dll"; exit 1 }

$xpr = @"
<XenosConfig>
	<imagePath>$Dll</imagePath>
	<manualMapFlags>0</manualMapFlags>
	<procName>$Proc</procName>
	<procNames>
		<procName>$Proc</procName>
	</procNames>
	<hijack>$(if($Hijack){1}else{0})</hijack>
	<unlink>0</unlink>
	<erasePE>$(if($ErasePE){1}else{0})</erasePE>
	<close>0</close>
	<krnHandle>$(if($KrnHandle){1}else{0})</krnHandle>
	<injIndef>0</injIndef>
	<processMode>0</processMode>
	<injectMode>$InjectMode</injectMode>
	<delay>$Delay</delay>
	<period>0</period>
	<skip>0</skip>
	<procCmdLine/>
	<initRoutine/>
	<initArgs/>
	<enableDump>0</enableDump>
	<enableLog>0</enableLog>
</XenosConfig>
"@
[System.IO.File]::WriteAllText($Out, $xpr, (New-Object System.Text.UTF8Encoding($false)))
Write-Host "generated profile: $Out"
