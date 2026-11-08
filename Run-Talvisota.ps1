[CmdletBinding()]
param(
    [ValidateSet("D3D12", "D3D11")]
    [string]$Backend = "D3D12",

    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

if (-not $IsWindows) {
    throw "The local Sakkola renderer currently requires Windows x64."
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "CMake 3.25 or newer is required. Install CMake and ensure cmake.exe is on PATH."
}

$root = $PSScriptRoot
$buildDirectory = Join-Path $root "build\local-game"
$executable = Join-Path $buildDirectory "$Configuration\talvisota_renderer_sample.exe"

Push-Location $root
try {
    Write-Host "Configuring Talvisota with the pinned VantaEngine renderer..."
    $configureArgs = @(
        "-S", $root,
        "-B", $buildDirectory,
        "-G", "Visual Studio 17 2022",
        "-A", "x64",
        "-DBUILD_TESTING=OFF",
        "-DTALVISOTA_WARNINGS_AS_ERRORS=ON",
        "-DTALVISOTA_ENABLE_VANTA_ENGINE=ON"
    )
    & cmake @configureArgs
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed. Install Visual Studio 2022 with the Desktop development with C++ workload and the Windows 10/11 SDK."
    }

    Write-Host "Building the Sakkola world renderer ($Configuration)..."
    & cmake --build $buildDirectory --config $Configuration --target talvisota_renderer_sample --parallel
    if ($LASTEXITCODE -ne 0) {
        throw "Talvisota build failed. See the compiler output above."
    }

    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "Build completed but the expected executable was not produced: $executable"
    }

    Write-Host "Launching the Sakkola world using $Backend..."
    & $executable "--backend=$($Backend.ToLowerInvariant())"
    if ($LASTEXITCODE -ne 0) {
        throw "Talvisota exited with code $LASTEXITCODE."
    }
}
finally {
    Pop-Location
}
