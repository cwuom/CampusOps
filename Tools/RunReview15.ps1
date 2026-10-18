param(
    [ValidateSet('movement', 'aim', 'hud', 'menu', 'lighting', 'stairs', 'vista', 'glass', 'wall', 'gear', 'tempered', 'perf', 'spacing', 'reload', 'clearance', 'mantle', 'mantlehands', 'hotelwall', 'actions', 'emptyactions', 'recovery')]
    [string[]]$Group = @('movement', 'aim', 'hud', 'menu', 'lighting', 'stairs', 'vista', 'glass', 'wall', 'gear'),
    [ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Label = 'after',
    [ValidateSet(30, 60, 120)][int]$Fps = 60,
    [switch]$Packaged,
    [switch]$Launcher,
    [switch]$Realtime,
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8'
)

$ErrorActionPreference = 'Stop'
if ($Group -contains 'perf' -and -not $Realtime) { throw 'Performance captures require -Realtime; fixed-step screenshots do not measure performance.' }
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectFile = Join-Path $projectRoot 'CampusOps.uproject'
$editorExe = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$packageExe = Join-Path $projectRoot $(if ($Launcher) { 'Build\Windows\CampusOps.exe' } else { 'Build\Windows\CampusOps\Binaries\Win64\CampusOps.exe' })
$exePath = if ($Packaged) { $packageExe } else { $editorExe }
if (-not (Test-Path -LiteralPath $exePath)) { throw "Executable missing: $exePath" }
$campus = '/Game/Campus/Maps/Map_Campus'
$hotel = '/Game/Campus/Maps/Map_Hotel'
$runs = @(
    @{ Group='movement'; Name='movement'; Script='Shots_Review15Movement.txt'; Url="${campus}?game=TDM?Bots=0?Weather=0?Time=1" },
    @{ Group='aim'; Name='aim_open'; Script='Shots_Review15Aim.txt'; Url="${hotel}?game=Gunfight?TeamSize=1?Bots=0?Custom=1?NoReload=1?InfAmmo=1?Weather=0?Time=1" },
    @{ Group='aim'; Name='aim_bots'; Script='Shots_Review15Aim.txt'; Url="${hotel}?game=Gunfight?TeamSize=1?Bots=1?Skill=0?Custom=1?BotHP=1000?NoReload=1?InfAmmo=1?Weather=0?Time=1" },
    @{ Group='hud'; Name='hud'; Script='Shots_Review15HUD.txt'; Url="${campus}?game=TDM?Bots=0?Weather=0?Time=1" },
    @{ Group='menu'; Name='menu'; Script='Shots_Review15Menu.txt'; Url='' },
    @{ Group='lighting'; Name='lighting'; Script='Shots_Review15Lighting.txt'; Url="${campus}?game=TDM?Bots=0?Weather=4?Time=4" },
    @{ Group='stairs'; Name='stairs'; Script='Shots_HotelStairs.txt'; Url="${hotel}?game=TDM?Bots=0?Weather=0?Time=1" },
    @{ Group='vista'; Name='vista'; Script='Shots_Vista.txt'; Url="${hotel}?game=TDM?Bots=0?Weather=0?Time=1" },
    @{ Group='glass'; Name='glass'; Script='Shots_GlassPolish.txt'; Url="${campus}?game=TDM?Bots=0?Weather=0?Time=1" },
    @{ Group='wall'; Name='wall'; Script='Shots_WallPolish.txt'; Url="${campus}?game=TDM?Bots=0?Weather=0?Time=1" },
    @{ Group='gear'; Name='gear'; Script='Shots_Gear.txt'; Url="${campus}?game=TDM?Bots=1?Weather=0?Time=1" }
    @{ Group='tempered'; Name='tempered'; Script='Shots_StairGlassWalk.txt'; Url="${hotel}?game=TDM?Bots=0?Weather=0?Time=1" },
    @{ Group='perf'; Name='perf'; Script='Shots_Review15Perf.txt'; Url="${hotel}?game=TDM?Bots=1?Weather=3?Time=1" },
    @{ Group='spacing'; Name='spacing'; Script='Shots_Review15Spacing.txt'; Url="${hotel}?game=Gunfight?TeamSize=1?Bots=1?Skill=0?Weather=0?Time=1" },
    @{ Group='reload'; Name='reload'; Script='Shots_Review15Reload.txt'; Url="${hotel}?game=Gunfight?TeamSize=1?Bots=0?Custom=1?InfAmmo=1?Weather=0?Time=1" }
    @{ Group='clearance'; Name='clearance'; Script='Shots_MotionClearance.txt'; Url="${campus}?game=TDM?Bots=0?Weather=0?Time=1" },
    @{ Group='mantle'; Name='mantle'; Script='Shots_MantleAll.txt'; Url="${hotel}?game=TDM?Bots=0?Weather=0?Time=1" }
    @{ Group='mantlehands'; Name='mantlehands'; Script='Shots_MantleHands.txt'; Url="${hotel}?game=TDM?Bots=0?Weather=0?Time=1" }
    @{ Group='hotelwall'; Name='hotelwall'; Script='Shots_HotelWallRepeat.txt'; Url="${hotel}?game=TDM?Bots=0?Weather=0?Time=1" },
    @{ Group='actions'; Name='actions'; Script='Shots_ActionMotion.txt'; Url="${hotel}?game=TDM?Bots=0?Weather=0?Time=1" }
    @{ Group='emptyactions'; Name='emptyactions'; Script='Shots_EmptyMotion.txt'; Url="${hotel}?game=TDM?Bots=0?Weather=0?Time=1" }
    @{ Group='recovery'; Name='recovery'; Script='Shots_WallRecovery.txt'; Url="${hotel}?game=TDM?Bots=0?Weather=0?Time=1" }
)
$results = @()
foreach ($run in $runs) {
    if ($Group -notcontains $run.Group) { continue }
    $runName = "Review15_${Label}_$($run.Name)_${Fps}fps"
    $userDir = Join-Path $projectRoot "Saved\$runName"
    $logPath = Join-Path $projectRoot "Saved\Logs\$runName.log"
    $scriptPath = Join-Path $PSScriptRoot $run.Script
    $captureArgs = @()
    if (-not $Packaged) { $captureArgs += ('"' + $projectFile + '"') }
    if ($run.Url) { $captureArgs += $run.Url }
    if (-not $Realtime) { $captureArgs += @('-benchmark', "-fps=$Fps") }
    $captureArgs += @('-game', '-windowed', '-ResX=1280', '-ResY=720', '-nosplash', '-unattended',
        ('"-UserDir=' + $userDir.Replace('\','/') + '"'), ('"-CampusScriptFile=' + $scriptPath.Replace('\','/') + '"'),
        ('"-ABSLOG=' + $logPath.Replace('\','/') + '"'))
    $started = Get-Date
    $captureProcess = Start-Process -FilePath $exePath -ArgumentList $captureArgs -WindowStyle Hidden -PassThru
    if (-not $captureProcess.WaitForExit(240000)) {
        if (-not $captureProcess.HasExited) { Stop-Process -Id $captureProcess.Id -Force }
        throw "Capture timed out: $runName. Log: $logPath"
    }
    $captureProcess.Refresh()
    $logText = Get-Content -LiteralPath $logPath -Raw
    $shotDir = Join-Path $userDir 'Saved\AutoShots'
    $shots = @(Get-ChildItem -LiteralPath $shotDir -Filter '*.png' -ErrorAction SilentlyContinue)
    $fatal = [regex]::Matches($logText, 'Fatal error:|Assertion failed:|Unhandled Exception:').Count
    $finished = $logText -match '\[CampusScript\].*quit'
    $slideObserved = $logText -match 'Speed=[\d.]+ Slide=1'
    $adsDrops = 0
    foreach ($match in [regex]::Matches($logText, 'AimHeld=1 CanAim=1 AimBlock=none RawADS=([\d.]+) EffectiveADS=([\d.]+)')) {
        if ([double]$match.Groups[1].Value -ge 0.99 -and [double]$match.Groups[2].Value -lt 0.95) { $adsDrops++ }
    }
    $result = [pscustomobject]@{
        Name=$runName; ExitCode=$captureProcess.ExitCode; Seconds=[Math]::Round(((Get-Date)-$started).TotalSeconds,2)
        ScreenshotCount=$shots.Count; ScriptFinished=$finished; FatalCount=$fatal; Log=$logPath; Screenshots=$shotDir
        SlideObserved=$slideObserved; HeldADSDrops=$adsDrops; Realtime=[bool]$Realtime
    }
    $results += $result
    $result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $userDir 'result.json') -Encoding utf8
    $result | ConvertTo-Json -Compress | Write-Output
    if ($captureProcess.ExitCode -ne 0 -or $fatal -ne 0 -or -not $finished -or $shots.Count -eq 0) {
        throw "Capture failed: $runName. Log: $logPath"
    }
    if ($run.Group -eq 'movement' -and -not $slideObserved) { throw "No actual slide observed: $runName" }
    if ($run.Group -eq 'aim' -and $adsDrops -gt 0) { throw "Held ADS dropped $adsDrops times: $runName" }
}
$groupTag = $Group -join '-'
$manifestPath = Join-Path $projectRoot "Saved\Review15_${Label}_${groupTag}_${Fps}fps.json"
$results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath -Encoding utf8
Write-Output "Capture manifest: $manifestPath"
