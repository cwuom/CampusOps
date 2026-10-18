# Runs capture scripts one after another: each entry is Script, URL (or "" for the menu), extra args, log tag.
param([string[]]$Only)
$ue = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$py = "E:\ComfyUI_windows_portable_nvidia\ComfyUI_windows_portable\python_embeded\python.exe"
$root = "E:\game-project"
$runs = @(
	@{ Tag = "fogsweep"; Script = "Shots_FogSweep.txt"; Url = "/Game/Campus/Maps/Map_Campus?game=TDM?Bots=0?Weather=5?Time=1"; Extra = @(); Sheet = "fs_*" },
	@{ Tag = "garden"; Script = "Shots_Garden.txt"; Url = "/Game/Campus/Maps/Map_Campus?game=TDM?Bots=0?Weather=0?Time=2"; Extra = @(); Sheet = "gd_*" },
	@{ Tag = "stairwalk"; Script = "Shots_StairGlassWalk.txt"; Url = "/Game/Campus/Maps/Map_Hotel?game=TDM?Bots=0?Weather=0?Time=1"; Extra = @(); Sheet = "sw_*" },
	@{ Tag = "fog1"; Script = "Shots_FogSchool.txt"; Url = "/Game/Campus/Maps/Map_Campus?game=TDM?Bots=0?Weather=5?Time=1"; Extra = @(); Sheet = "fg_*"; Suffix = "_t1" },
	@{ Tag = "fog2"; Script = "Shots_FogSchool.txt"; Url = "/Game/Campus/Maps/Map_Campus?game=TDM?Bots=0?Weather=5?Time=2"; Extra = @(); Sheet = "fg_*"; Suffix = "_t2" },
	@{ Tag = "padstair2"; Script = "Shots_PadStair2.txt"; Url = "/Game/Campus/Maps/Map_Hotel?game=TDM?Bots=0?Weather=0?Time=1"; Extra = @(); Sheet = "pt_*" },
	@{ Tag = "redreload"; Script = "Shots_EmptyReload3.txt"; Url = "/Game/Campus/Maps/Map_Hotel?game=TDM?Bots=0?Weather=0?Time=1"; Extra = @("-benchmark", "-fps=30"); Sheet = "e[rh][dx]_*" },
	@{ Tag = "padstair"; Script = "Shots_PadStair.txt"; Url = "/Game/Campus/Maps/Map_Hotel?game=TDM?Bots=0?Weather=0?Time=3"; Extra = @(); Sheet = "ps_*" },
	@{ Tag = "corestair"; Script = "Shots_CoreStair.txt"; Url = "/Game/Campus/Maps/Map_Hotel?game=TDM?Bots=0?Weather=0?Time=1"; Extra = @(); Sheet = "cs_*" },
	@{ Tag = "machinedoor"; Script = "Shots_MachineDoor.txt"; Url = "/Game/Campus/Maps/Map_Hotel?game=TDM?Bots=0?Weather=0?Time=1"; Extra = @(); Sheet = "md_*" },
	@{ Tag = "stairs2"; Script = "Shots_Stairs2.txt"; Url = "/Game/Campus/Maps/Map_Campus?game=TDM?Bots=0?Weather=0?Time=3"; Extra = @(); Sheet = "st_*" },
	@{ Tag = "mantle5"; Script = "Shots_Mantle5.txt"; Url = "/Game/Campus/Maps/Map_Hotel?game=TDM?Bots=0?Weather=0?Time=1"; Extra = @("-benchmark", "-fps=30"); Sheet = "m[ef]_*" },
	@{ Tag = "mantle4"; Script = "Shots_Mantle4.txt"; Url = "/Game/Campus/Maps/Map_Hotel?game=TDM?Bots=0?Weather=0?Time=1"; Extra = @("-benchmark", "-fps=30"); Sheet = "m[cd]_*" },
	@{ Tag = "nightroof"; Script = "Shots_NightRoof.txt"; Url = "/Game/Campus/Maps/Map_Hotel?game=TDM?Bots=0?Weather=0?Time=4"; Extra = @(); Sheet = "nr_*" },
	@{ Tag = "reload2"; Script = "Shots_EmptyReload2.txt"; Url = "/Game/Campus/Maps/Map_Hotel?game=TDM?Bots=0?Weather=0?Time=1"; Extra = @("-benchmark", "-fps=30"); Sheet = "e[wsc][acr]_*" },
	@{ Tag = "mantle3"; Script = "Shots_Mantle3.txt"; Url = "/Game/Campus/Maps/Map_Hotel?game=TDM?Bots=0?Weather=0?Time=1"; Extra = @("-benchmark", "-fps=30"); Sheet = "m[ab]_*" },
	@{ Tag = "mantle2"; Script = "Shots_Mantle2.txt"; Url = "/Game/Campus/Maps/Map_Hotel?game=TDM?Bots=0?Weather=0?Time=1"; Extra = @("-benchmark", "-fps=30"); Sheet = "m[jz]_*" },
	@{ Tag = "mantle"; Script = "Shots_Mantle.txt"; Url = "/Game/Campus/Maps/Map_Hotel?game=TDM?Bots=0?Weather=0?Time=1"; Extra = @("-benchmark", "-fps=30"); Sheet = "mt_*" },
	@{ Tag = "reload"; Script = "Shots_EmptyReload.txt"; Url = "/Game/Campus/Maps/Map_Hotel?game=TDM?Bots=0?Weather=0?Time=1"; Extra = @("-benchmark", "-fps=30"); Sheet = "er*" },
	@{ Tag = "signglass"; Script = "Shots_SignGlass.txt"; Url = "/Game/Campus/Maps/Map_Hotel?game=TDM?Bots=0?Weather=0?Time=1"; Extra = @(); Sheet = "sg_*" },
	@{ Tag = "vista3"; Script = "Shots_Vista.txt"; Url = "/Game/Campus/Maps/Map_Hotel?game=TDM?Bots=0?Weather=0?Time=3"; Extra = @(); Sheet = "vs_*"; Suffix = "_t3" },
	@{ Tag = "vista4"; Script = "Shots_Vista.txt"; Url = "/Game/Campus/Maps/Map_Hotel?game=TDM?Bots=0?Weather=0?Time=4"; Extra = @(); Sheet = "vs_*"; Suffix = "_t4" },
	@{ Tag = "light"; Script = "Shots_Light.txt"; Url = "/Game/Campus/Maps/Map_Campus?game=TDM?Bots=0?Weather=0?Time=4"; Extra = @(); Sheet = "lt_*" },
	@{ Tag = "keys"; Script = "Shots_Keys.txt"; Url = ""; Extra = @(); Sheet = "keys_*" }
)
foreach ($r in $runs) {
	if ($Only -and -not ($Only -contains $r.Tag)) { continue }
	$args = @('"E:\game-project\CampusOps.uproject"')
	if ($r.Url) { $args += "`"$($r.Url)`"" }
	$args += @('-game', '-windowed', '-ResX=1280', '-ResY=720', "-CampusScriptFile=E:/game-project/Tools/$($r.Script)", "-ABSLOG=E:/game-project/Saved/Logs/shot_$($r.Tag).log") + $r.Extra
	$t0 = Get-Date
	Start-Process $ue -ArgumentList $args -Wait
	if ($r.Suffix) {
		Get-ChildItem "$root\Saved\AutoShots\$($r.Sheet).png" | Where-Object { $_.Name -notmatch "_t\d\.png$" } | ForEach-Object { Move-Item $_.FullName ($_.FullName -replace "\.png$", "$($r.Suffix).png") -Force }
	}
	"{0} done in {1:0}s" -f $r.Tag, ((Get-Date) - $t0).TotalSeconds
}
Set-Location $root
