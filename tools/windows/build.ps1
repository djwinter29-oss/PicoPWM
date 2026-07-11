param(
    [string]$BuildDir = "firmware/build",
    [string]$Generator,
    [string]$PicoSdkPath = $env:PICO_SDK_PATH
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($PicoSdkPath)) {
    throw "PICO_SDK_PATH is not set."
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
$sourceDir = Join-Path $repoRoot "firmware"
$buildDirPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))

if ([string]::IsNullOrWhiteSpace($Generator)) {
    if (Get-Command ninja -ErrorAction SilentlyContinue) {
        $Generator = "Ninja"
    } else {
        $Generator = "MinGW Makefiles"
    }
}

cmake -S $sourceDir -B $buildDirPath -G $Generator -DPICO_SDK_PATH=$PicoSdkPath
cmake --build $buildDirPath --parallel