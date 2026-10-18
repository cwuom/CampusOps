# One scripted capture run in the editor build: .\RunShot.ps1 <script name> <map URL> <log tag> [extra args...]
# Waits for the game to quit, then lists the material compile failures from its log.
param([string]$Script, [string]$Url, [string]$Tag, [string[]]$Extra = @())
$ue = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$log = "E:/game-project/Saved/Logs/shot_$Tag.log"
$args = @('"E:\game-project\CampusOps.uproject"')
if ($Url) { $args += "`"$Url`"" }
$args += @('-game', '-windowed', '-ResX=1920', '-ResY=1080', "-CampusScriptFile=E:/game-project/Tools/$Script", "-ABSLOG=$log") + $Extra
$t0 = Get-Date
$p = Start-Process $ue -ArgumentList $args -PassThru
$p.WaitForExit()
"{0} done in {1:0}s (exit {2})" -f $Tag, ((Get-Date) - $t0).TotalSeconds, $p.ExitCode
Select-String -Path $log -Pattern "Failed to compile|error:|Fatal|Unhandled Exception" | Select-Object -First 10 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(300, $_.Line.Length)) }
