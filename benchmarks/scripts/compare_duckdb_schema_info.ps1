param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string] $CsvPath,

    [int] $Runs = 5,

    [string] $DuckDB = "duckdb",

    [string] $CsvSchemaInfo = "csv_schema_info"
)

$ErrorActionPreference = "Stop"

if ($Runs -lt 1) {
    throw "-Runs must be at least 1."
}

function Format-NativeExitCode {
    param([int] $Code)

    if ($Code -lt 0) {
        $unsigned = [uint32]([int64] $Code + 0x100000000)
    } else {
        $unsigned = [uint32] $Code
    }

    return "$Code (0x{0:X8})" -f $unsigned
}

function Resolve-NativeCommand {
    param(
        [string] $Name,
        [string[]] $Fallbacks = @()
    )

    if (Test-Path -LiteralPath $Name) {
        return (Resolve-Path -LiteralPath $Name).Path
    }

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    foreach ($fallback in $Fallbacks) {
        if (Test-Path -LiteralPath $fallback) {
            return (Resolve-Path -LiteralPath $fallback).Path
        }
    }

    throw "Could not find '$Name'. Pass an explicit path with -DuckDB or -CsvSchemaInfo."
}

function Invoke-TimedNative {
    param(
        [string] $Label,
        [string] $FilePath,
        [string[]] $Arguments
    )

    $output = New-Object System.Collections.Generic.List[string]
    $timer = [System.Diagnostics.Stopwatch]::StartNew()

    & $FilePath @Arguments 2>&1 | ForEach-Object {
        $output.Add([string] $_)
    }
    $exitCode = $LASTEXITCODE

    $timer.Stop()

    if ($exitCode -ne 0) {
        $formattedExitCode = Format-NativeExitCode $exitCode
        $details = $output -join [Environment]::NewLine
        throw "$Label failed with exit code $formattedExitCode`n$details"
    }

    return $timer.Elapsed.TotalSeconds
}

function Get-Median {
    param([double[]] $Values)

    $sorted = @($Values | Sort-Object)
    $count = $sorted.Count

    if ($count -eq 0) {
        return 0.0
    }

    $middle = [int]($count / 2)
    if (($count % 2) -eq 1) {
        return $sorted[$middle]
    }

    return ($sorted[$middle - 1] + $sorted[$middle]) / 2.0
}

function Get-Stats {
    param(
        [string] $Tool,
        [double[]] $Samples
    )

    $average = ($Samples | Measure-Object -Average).Average
    $minimum = ($Samples | Measure-Object -Minimum).Minimum
    $maximum = ($Samples | Measure-Object -Maximum).Maximum

    return [pscustomobject]@{
        Tool = $Tool
        Runs = $Samples.Count
        AvgSeconds = [math]::Round($average, 6)
        MedianSeconds = [math]::Round((Get-Median $Samples), 6)
        MinSeconds = [math]::Round($minimum, 6)
        MaxSeconds = [math]::Round($maximum, 6)
    }
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..\..")).Path
$resolvedCsv = (Resolve-Path -LiteralPath $CsvPath).Path

$schemaFallbacks = @(
    (Join-Path $repoRoot "build\x64-Release\programs\csv_schema_info.exe"),
    (Join-Path $repoRoot "build\x64-Debug\programs\csv_schema_info.exe"),
    (Join-Path $repoRoot "build\Release\programs\csv_schema_info.exe"),
    (Join-Path $repoRoot "build\Debug\programs\csv_schema_info.exe"),
    (Join-Path $repoRoot "build\benchmarks\Release\csv_schema_info.exe"),
    (Join-Path $repoRoot "out\build\x64-Release\programs\csv_schema_info.exe")
)

$duckDbPath = Resolve-NativeCommand $DuckDB
$schemaInfoPath = Resolve-NativeCommand $CsvSchemaInfo $schemaFallbacks

$duckCsvPath = ($resolvedCsv -replace "\\", "/") -replace "'", "''"
$duckSql = "CREATE TEMP TABLE csv_input AS SELECT * FROM read_csv_auto('$duckCsvPath'); SELECT COUNT(*) FROM csv_input;"
$duckArgs = @(":memory:", "-noheader", "-csv", "-c", $duckSql)
$schemaArgs = @($resolvedCsv)

Write-Host "file=$resolvedCsv"
Write-Host "runs=$Runs warmup=1"
Write-Host "duckdb=$duckDbPath"
Write-Host "csv_schema_info=$schemaInfoPath"
Write-Host ""

Write-Host "Warm-up: csv_schema_info"
[void](Invoke-TimedNative "csv_schema_info warm-up" $schemaInfoPath $schemaArgs)

Write-Host "Warm-up: DuckDB"
[void](Invoke-TimedNative "DuckDB warm-up" $duckDbPath $duckArgs)

$schemaSamples = New-Object System.Collections.Generic.List[double]
$duckSamples = New-Object System.Collections.Generic.List[double]

for ($i = 1; $i -le $Runs; ++$i) {
    if (($i % 2) -eq 1) {
        $schemaSeconds = Invoke-TimedNative "csv_schema_info run $i" $schemaInfoPath $schemaArgs
        $duckSeconds = Invoke-TimedNative "DuckDB run $i" $duckDbPath $duckArgs
    } else {
        $duckSeconds = Invoke-TimedNative "DuckDB run $i" $duckDbPath $duckArgs
        $schemaSeconds = Invoke-TimedNative "csv_schema_info run $i" $schemaInfoPath $schemaArgs
    }

    $schemaSamples.Add($schemaSeconds)
    $duckSamples.Add($duckSeconds)

    Write-Host ("run={0} csv_schema_info={1:0.000000}s duckdb={2:0.000000}s" -f $i, $schemaSeconds, $duckSeconds)
}

Write-Host ""
@(
    (Get-Stats "csv_schema_info" $schemaSamples.ToArray()),
    (Get-Stats "duckdb_read_csv_auto_count" $duckSamples.ToArray())
) | Format-Table -AutoSize

