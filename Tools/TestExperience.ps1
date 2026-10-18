param([string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8')

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$runTag = Get-Date -Format 'yyyyMMdd-HHmmss'
$reportPath = Join-Path $projectRoot "Saved\ExperienceTests\$runTag"
$logPath = Join-Path $projectRoot "Saved\Logs\experience_tests_$runTag.log"
$consolePath = Join-Path $projectRoot "Saved\Logs\experience_tests_${runTag}_console.log"
$editorCmd = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $logPath) | Out-Null
& $editorCmd (Join-Path $projectRoot 'CampusOps.uproject') '-ExecCmds=Automation RunTests CampusOps.Experience' `
    '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$reportPath" "-ABSLOG=$logPath" `
    -NullRHI -unattended -nopause -nosplash *> $consolePath
$engineResult = $LASTEXITCODE
if ($engineResult -ne 0) {
    throw "Automation process failed ($engineResult). Log: $logPath"
}
$reportFile = Join-Path $reportPath 'index.json'
if (-not (Test-Path -LiteralPath $reportFile)) {
    throw "Automation did not produce a report. Log: $logPath"
}
$report = Get-Content -LiteralPath $reportFile -Raw | ConvertFrom-Json
if ($report.failed -ne 0 -or $report.notRun -ne 0 -or $report.inProcess -ne 0 -or $report.succeeded -lt 9) {
    throw "Experience tests incomplete: passed=$($report.succeeded), failed=$($report.failed), notRun=$($report.notRun). Report: $reportFile"
}
Write-Output "Experience tests passed: $($report.succeeded). Report: $reportFile"
