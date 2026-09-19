$ErrorActionPreference = 'Continue'
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
Write-Host "运行身份: $([Security.Principal.WindowsIdentity]::GetCurrent().Name)"
$isAdmin = (New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
Write-Host "管理员: $isAdmin"

Add-Type -TypeDefinition @'
using System;using System.IO;using System.Runtime.InteropServices;
public class Dumper{
 [DllImport("dbghelp.dll",SetLastError=true)] static extern bool MiniDumpWriteDump(IntPtr h,uint pid,IntPtr f,uint type,IntPtr a,IntPtr b,IntPtr c);
 [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr OpenProcess(int a,bool i,int p);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
 public static string Dump(int pid,string outp,uint type){
   IntPtr h=OpenProcess(0x1F0FFF,false,pid);
   if(h==IntPtr.Zero){ h=OpenProcess(0x0410,false,pid); if(h==IntPtr.Zero) return "OpenProcess 失败 err="+Marshal.GetLastWin32Error(); }
   using(var fs=new FileStream(outp,FileMode.Create,FileAccess.Write)){
     bool ok=MiniDumpWriteDump(h,(uint)pid,fs.SafeFileHandle.DangerousGetHandle(),type,IntPtr.Zero,IntPtr.Zero,IntPtr.Zero);
     int err=Marshal.GetLastWin32Error();
     CloseHandle(h);
     if(!ok) { try{fs.Close();}catch{}; return "MiniDumpWriteDump 失败 err="+err; }
   }
   long len=0; try{ len=new FileInfo(outp).Length; }catch{}
   return "OK "+(len/1048576)+" MB -> "+outp;
 }
}
'@

$targets = @()
$c = Get-Process -Name FSDConsole -ErrorAction SilentlyContinue
if ($c) { $targets += $c }
$g = Get-Process -Name FSD-Win64-Shipping -ErrorAction SilentlyContinue
if ($g) { $targets += $g }

if ($targets.Count -eq 0) { Write-Host "没有找到 FSDConsole.exe / FSD-Win64-Shipping.exe（请先启动修改器或游戏）"; Start-Sleep 5; exit }

foreach ($p in $targets) {
  $name = $p.ProcessName
  $out  = Join-Path $dir ("dump_" + $name + ".dmp")
  Write-Host "转储 $name (PID=$($p.Id)) ..."
  # type: 2 = MiniDumpWithFullMemory
  $r = [Dumper]::Dump($p.Id, $out, 2)
  Write-Host "  $r"
}
Write-Host "全部完成，按任意键关闭窗口"
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
