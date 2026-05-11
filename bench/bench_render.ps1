# Bench script: launches hive_launcher twice (inline vs render-thread), aggregates
# the per-window CSV output FrameBench writes, and prints a comparison.
#
# Usage:
#   pwsh bench/bench_render.ps1 [-Grid 5] [-Duration 15] [-Interval 2]
#                               [-Project projects/sponza_demo/project.hive]
#                               [-Build out/build/hive-editor]

param(
    [int]$Grid = 5,
    [int]$Duration = 15,
    [double]$Interval = 2.0,
    [string]$Project = "projects/sponza_demo/project.hive",
    [string]$Build = "out/build/hive-editor",
    [int]$Warmup = 3
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$benchDir = Join-Path $repoRoot "bench"
$launcher = Join-Path $repoRoot "$Build/bin/Debug/hive_launcher.exe"

if (-not (Test-Path $launcher)) {
    Write-Error "Launcher not found at $launcher - build first with cmake --build $Build"
    exit 1
}

function Invoke-BenchRun {
    param([string]$Label, [string]$CsvPath, [bool]$RenderThread)

    Write-Host ""
    Write-Host "=== Running [$Label] grid=${Grid}x${Grid} duration=${Duration}s ===" -ForegroundColor Cyan

    if (Test-Path $CsvPath) { Remove-Item $CsvPath -Force }

    $env:HIVE_SPONZA_GRID        = "$Grid"
    $env:HIVE_BENCH_DURATION     = "$Duration"
    $env:HIVE_BENCH_LOG_INTERVAL = "$Interval"
    $env:HIVE_BENCH_LABEL        = $Label
    $env:HIVE_BENCH_REPORT       = $CsvPath

    $launchArgs = @("--editor", $Project)
    if ($RenderThread) { $launchArgs += "--render-thread" }

    & $launcher @launchArgs | Out-Null
    $exit = $LASTEXITCODE

    Remove-Item Env:HIVE_SPONZA_GRID -ErrorAction SilentlyContinue
    Remove-Item Env:HIVE_BENCH_DURATION -ErrorAction SilentlyContinue
    Remove-Item Env:HIVE_BENCH_LOG_INTERVAL -ErrorAction SilentlyContinue
    Remove-Item Env:HIVE_BENCH_LABEL -ErrorAction SilentlyContinue
    Remove-Item Env:HIVE_BENCH_REPORT -ErrorAction SilentlyContinue

    if ($exit -ne 0) {
        Write-Warning "Launcher exited with code $exit for [$Label]"
    }

    if (-not (Test-Path $CsvPath)) {
        Write-Error "No bench report written at $CsvPath - did the scene load?"
        return $null
    }
    return Import-Csv $CsvPath
}

function Get-Summary {
    param([array]$Rows, [int]$Warmup)

    if ($null -eq $Rows -or $Rows.Count -eq 0) { return $null }

    $valid = $Rows | Where-Object { [int]$_.window -gt $Warmup }
    if ($valid.Count -eq 0) { $valid = $Rows }

    $fpsMean    = ($valid | Measure-Object -Property fps_mean       -Average).Average
    $fpsLow1    = ($valid | Measure-Object -Property fps_1pct_low   -Average).Average
    $fpsHigh99  = ($valid | Measure-Object -Property fps_99pct_high -Average).Average
    $frameMean  = ($valid | Measure-Object -Property frame_ms_mean  -Average).Average
    $buildMean  = ($valid | Measure-Object -Property build_ms_mean  -Average).Average
    $execMean   = ($valid | Measure-Object -Property execute_ms_mean -Average).Average

    return [pscustomobject]@{
        Windows         = $valid.Count
        FpsMean         = [math]::Round($fpsMean, 1)
        Fps1pctLow      = [math]::Round($fpsLow1, 1)
        Fps99pctHigh    = [math]::Round($fpsHigh99, 1)
        FrameMsMean     = [math]::Round($frameMean, 3)
        BuildMsMean     = [math]::Round($buildMean, 3)
        ExecuteMsMean   = [math]::Round($execMean, 3)
    }
}

$inlineCsv  = Join-Path $benchDir "render_inline.csv"
$threadCsv  = Join-Path $benchDir "render_thread.csv"

$inlineRows = Invoke-BenchRun -Label "inline"        -CsvPath $inlineCsv -RenderThread $false
$threadRows = Invoke-BenchRun -Label "render_thread" -CsvPath $threadCsv -RenderThread $true

$inlineStats = Get-Summary -Rows $inlineRows -Warmup $Warmup
$threadStats = Get-Summary -Rows $threadRows -Warmup $Warmup

Write-Host ""
Write-Host "=== Bench results (windows after warmup #$Warmup) ===" -ForegroundColor Green
Write-Host ""
Write-Host "Inline path:"
$inlineStats | Format-List
Write-Host "Render-thread path:"
$threadStats | Format-List

if ($null -ne $inlineStats -and $null -ne $threadStats -and $inlineStats.FpsMean -gt 0) {
    $delta = (($threadStats.FpsMean - $inlineStats.FpsMean) / $inlineStats.FpsMean) * 100
    $sign = if ($delta -ge 0) { "+" } else { "" }
    Write-Host ("FPS delta (render-thread vs inline): {0}{1:F1}%" -f $sign, $delta) -ForegroundColor Yellow
}
