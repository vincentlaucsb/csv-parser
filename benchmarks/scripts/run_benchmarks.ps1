param(
    [string] $BuildDir = "build/benchmarks",
    [string] $DataDir = "build/benchmarks/data",
    [string] $ResultsDir = "benchmarks/results",
    [int[]] $Rows = @(500000, 5000000),
    [double[]] $TargetSizeGb = @(),
    [string[]] $Profiles = @("clean", "quoted", "multiline"),
    [ValidateSet("standard", "wide")] [string] $RowShape = "standard",
    [int] $Repetitions = 5,
    [switch] $ForceDatasets,
    [string] $Config = "Release"
)

$ErrorActionPreference = "Stop"

function Format-NativeExitCode {
    param([int] $Code)

    if ($Code -lt 0) {
        $unsigned = [uint32]([int64] $Code + 0x100000000)
    } else {
        $unsigned = [uint32] $Code
    }

    $hex = "0x{0:X8}" -f $unsigned

    if ($unsigned -eq 0xC0000374) {
        return "$Code ($hex, Windows heap corruption)"
    }

    if ($unsigned -eq 0xC0000005) {
        return "$Code ($hex, access violation)"
    }

    return "$Code ($hex)"
}

function Invoke-Native {
    param(
        [string] $FilePath,
        [string[]] $Arguments
    )

    $quotedArgs = @($FilePath) + $Arguments | ForEach-Object {
        $arg = [string] $_
        if ($arg -match '[\s"&|<>^]') {
            '"' + ($arg -replace '"', '\"') + '"'
        } else {
            $arg
        }
    }

    $command = "set ""MYOLDPATH=%PATH%""&& set Path=&& set ""PATH=!MYOLDPATH!""&& " + ($quotedArgs -join " ")
    & cmd /d /v /c $command
    if ($LASTEXITCODE -ne 0) {
        $formattedExitCode = Format-NativeExitCode $LASTEXITCODE
        throw "$FilePath failed with exit code $formattedExitCode"
    }
}

function Format-SizeLabel {
    param([double] $SizeGb)

    return $SizeGb.ToString("0.######", [System.Globalization.CultureInfo]::InvariantCulture)
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Resolve-Path (Join-Path $scriptDir "..\..")
$dataPath = Join-Path $repoRoot $DataDir
$buildPath = Join-Path $repoRoot $BuildDir
$resultsPath = Join-Path $repoRoot $ResultsDir
$normalizedProfiles = @()
$datasetSpecs = @()

foreach ($profileValue in $Profiles) {
    foreach ($profile in ([string] $profileValue).Split(",", [System.StringSplitOptions]::RemoveEmptyEntries)) {
        $normalizedProfiles += $profile.Trim()
    }
}

if ($normalizedProfiles.Count -eq 0) {
    throw "At least one payload profile must be provided."
}

foreach ($profile in $normalizedProfiles) {
    if (($profile -ne "clean") -and ($profile -ne "quoted") -and ($profile -ne "multiline") -and ($profile -ne "realistic")) {
        throw "Unsupported profile '$profile'. Valid profiles are clean, quoted, multiline, and realistic."
    }
}

if ($PSBoundParameters.ContainsKey("Rows") -and $PSBoundParameters.ContainsKey("TargetSizeGb")) {
    throw "Specify either -Rows or -TargetSizeGb, not both."
}

if ($PSBoundParameters.ContainsKey("TargetSizeGb")) {
    foreach ($sizeGb in $TargetSizeGb) {
        if ($sizeGb -le 0) {
            throw "All -TargetSizeGb values must be greater than zero."
        }

        $formattedSize = Format-SizeLabel $sizeGb
        $sizeLabel = ($formattedSize -replace "\.", "_") + "gb_target"
        if ($RowShape -ne "standard") {
            $sizeLabel += "_$RowShape"
        }

        $datasetSpecs += [pscustomobject]@{
            Label = $sizeLabel
            DatasetToken = ($formattedSize -replace "\.", "_") + "gb_$RowShape"
            Arguments = @("--target-size-gb", $sizeGb.ToString([System.Globalization.CultureInfo]::InvariantCulture))
        }
    }
} else {
    foreach ($rowCount in $Rows) {
        $rowLabel = "${rowCount}_rows"
        if ($RowShape -ne "standard") {
            $rowLabel += "_$RowShape"
        }

        $datasetSpecs += [pscustomobject]@{
            Label = $rowLabel
            DatasetToken = "${rowCount}_rows_$RowShape"
            Arguments = @("--rows", "$rowCount")
        }
    }
}

New-Item -ItemType Directory -Force $resultsPath | Out-Null
Invoke-Native "cmake" @("-S", (Join-Path $repoRoot "benchmarks"), "-B", $buildPath, "-DCMAKE_BUILD_TYPE=$Config")
Invoke-Native "cmake" @("--build", $buildPath, "--config", $Config)

$exeDir = Join-Path $buildPath $Config
if (-not (Test-Path $exeDir)) {
    $exeDir = $buildPath
}

$benches = @(
    "csv_parser_read_bench",
    "csv_parser_multi_pass_bench",
    "fast_cpp_csv_parser_read_bench",
    "fast_cpp_csv_parser_multi_pass_bench",
    "dataframe_rapidcsv_roundtrip_bench"
)

foreach ($datasetSpec in $datasetSpecs) {
    $rowResultsRoot = Join-Path $resultsPath $datasetSpec.Label
    New-Item -ItemType Directory -Force $rowResultsRoot | Out-Null

    foreach ($legacyName in @(
        "benchmark_input.csv",
        "csv_parser_read_bench.json",
        "csv_parser_read_bench.json.tmp",
        "csv_parser_multi_pass_bench.json",
        "csv_parser_multi_pass_bench.json.tmp",
        "fast_cpp_csv_parser_read_bench.json",
        "fast_cpp_csv_parser_read_bench.json.tmp",
        "fast_cpp_csv_parser_multi_pass_bench.json",
        "fast_cpp_csv_parser_multi_pass_bench.json.tmp",
        "dataframe_rapidcsv_roundtrip_bench.json",
        "dataframe_rapidcsv_roundtrip_bench.json.tmp"
    )) {
        $legacyPath = Join-Path $rowResultsRoot $legacyName
        if (Test-Path $legacyPath) {
            Remove-Item -LiteralPath $legacyPath -Force
        }
    }

    foreach ($profile in $normalizedProfiles) {
        $datasetProfile = if ($profile -eq "realistic") { "multiline" } else { $profile }
        $datasetPath = Join-Path $dataPath "bench_8col_${datasetProfile}_$($datasetSpec.DatasetToken).csv"
        $datasetArgs = @(
            (Join-Path $scriptDir "generate_8col_dataset.py"),
            $datasetPath,
            "--profile",
            $datasetProfile,
            "--row-shape",
            $RowShape
        )
        $datasetArgs += $datasetSpec.Arguments
        if ($ForceDatasets) {
            $datasetArgs += "--force"
        }
        Invoke-Native "python" $datasetArgs

        $profileResultsPath = Join-Path $rowResultsRoot $datasetProfile
        $resultsDataset = Join-Path $profileResultsPath "benchmark_input.csv"
        New-Item -ItemType Directory -Force $profileResultsPath | Out-Null

        if (Test-Path $resultsDataset) {
            Remove-Item -LiteralPath $resultsDataset
        }
        Copy-Item -LiteralPath $datasetPath -Destination $resultsDataset
        Write-Host "Saved benchmark input $resultsDataset"

        foreach ($bench in $benches) {
            if (($datasetProfile -eq "multiline") -and (
                ($bench -eq "fast_cpp_csv_parser_read_bench") -or
                ($bench -eq "fast_cpp_csv_parser_multi_pass_bench")
            )) {
                Write-Host "Skipping $bench for multiline payload: fast-cpp-csv-parser does not support quoted line breaks."
                continue
            }

            $exe = Join-Path $exeDir "$bench.exe"
            if (Test-Path $exe) {
                $json = Join-Path $profileResultsPath "$bench.json"
                $tmpJson = "$json.tmp"

                if (Test-Path $tmpJson) {
                    Remove-Item -LiteralPath $tmpJson
                }

                & $exe `
                    --benchmark_format=console `
                    "--benchmark_repetitions=$Repetitions" `
                    --benchmark_report_aggregates_only=true `
                    --benchmark_display_aggregates_only=true `
                    "--benchmark_out=$tmpJson" `
                    --benchmark_out_format=json `
                    $resultsDataset

                if ($LASTEXITCODE -ne 0) {
                    $formattedExitCode = Format-NativeExitCode $LASTEXITCODE
                    throw "$bench failed with exit code $formattedExitCode. Partial diagnostic JSON, if any, is at $tmpJson"
                }

                try {
                    Get-Content -LiteralPath $tmpJson -Raw | ConvertFrom-Json | Out-Null
                } catch {
                    throw "$bench produced invalid JSON at $tmpJson"
                }

                Move-Item -LiteralPath $tmpJson -Destination $json -Force
                Write-Host "Saved $json"
            }
        }
    }
}
