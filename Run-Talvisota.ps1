[CmdletBinding()]
param(
    [ValidateSet("D3D12", "D3D11")]
    [string]$Backend = "D3D12",

    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"

if ($env:OS -ne "Windows_NT") {
    throw "The local Sakkola renderer currently requires Windows x64."
}

function Resolve-CMake {
    $candidates = [System.Collections.Generic.List[string]]::new()

    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($command) {
        $candidates.Add($command.Source)
    }

    $programFilesX86 = [Environment]::GetFolderPath("ProgramFilesX86")
    $programFiles = @(
        $env:ProgramFiles,
        $programFilesX86,
        (Join-Path $env:LOCALAPPDATA "Programs")
    ) | Where-Object { $_ }

    foreach ($base in $programFiles) {
        $candidates.Add((Join-Path $base "CMake\bin\cmake.exe"))
    }

    $vswhere = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $vsCMake = & $vswhere -latest -products "*" -find "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" 2>$null
        if ($LASTEXITCODE -eq 0) {
            foreach ($path in $vsCMake) {
                if ($path) { $candidates.Add($path) }
            }
        }
    }

    foreach ($path in ($candidates | Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }

        $versionOutput = & $path --version 2>$null
        if ($LASTEXITCODE -ne 0 -or $versionOutput -notmatch "cmake version (\d+)\.(\d+)\.(\d+)") {
            continue
        }

        $version = [version]("$($Matches[1]).$($Matches[2]).$($Matches[3])")
        if ($version -ge [version]"3.25.0") {
            return $path
        }
    }

    throw @"
CMake 3.25 or newer was not found.
Install it with:
  winget install --id Kitware.CMake --exact
Then CLOSE this PowerShell window, open a new one, and run:
  cmake --version
  .\Run-Talvisota.ps1
If CMake is already installed, add its 'bin' directory to PATH.
"@
}

$cmakeExe = Resolve-CMake

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
    & $cmakeExe @configureArgs
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed. Install Visual Studio 2022 with the Desktop development with C++ workload and the Windows 10/11 SDK."
    }

    Write-Host "Building the Sakkola world renderer ($Configuration)..."
    & $cmakeExe --build $buildDirectory --config $Configuration --target talvisota_renderer_sample --parallel
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
