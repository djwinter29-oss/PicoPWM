param(
    [string]$BuildDir = "firmware/build",
    [string]$Generator,
    [string]$PicoSdkPath = $env:PICO_SDK_PATH,
    [string]$FirmwareVersion = $env:PICO_PWM_FIRMWARE_VERSION
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

$cmakeArgs = @(
    "-S", $sourceDir,
    "-B", $buildDirPath,
    "-G", $Generator,
    "-DPICO_SDK_PATH=$PicoSdkPath"
)

if (-not [string]::IsNullOrWhiteSpace($FirmwareVersion)) {
    $cmakeArgs += "-DPICO_PWM_FIRMWARE_VERSION=$FirmwareVersion"
}

& cmake @cmakeArgs
cmake --build $buildDirPath --parallel